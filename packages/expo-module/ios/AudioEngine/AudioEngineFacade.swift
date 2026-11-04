import AVFoundation

/// Public orchestration layer used by Expo. All mutable state is owned by one queue.
final class AudioEngineFacade {
  private let queue = DispatchQueue(label: "sezo.audioengine.state")
  private let state = EngineState()

  private let eventEmitter = EngineEventEmitter()
  private let sessionCoordinator = AudioSessionCoordinator()
  private let backgroundController = BackgroundMediaController()
  private let extractionController = ExtractionController()

  private lazy var runtime = EngineRuntime(queue: queue)
  private lazy var recordingController = RecordingController(queue: queue)

  var onPlaybackStateChange: ((String, Double, Double) -> Void)? {
    get { eventEmitter.onPlaybackStateChange }
    set { eventEmitter.onPlaybackStateChange = newValue }
  }

  var onPlaybackComplete: ((Double, Double) -> Void)? {
    get { eventEmitter.onPlaybackComplete }
    set { eventEmitter.onPlaybackComplete = newValue }
  }

  var onError: ((String, String, [String: Any]?) -> Void)? {
    get { eventEmitter.onError }
    set { eventEmitter.onError = newValue }
  }

  /// Called (on the state queue) with the finished take when the engine ends a
  /// recording on its own, e.g. because an audio session interruption began.
  var onRecordingInterrupted: (([String: Any]) -> Void)?

  /// Called (on the state queue) with an `inputPitch` payload for every window
  /// the input monitor analyses.
  var onInputPitch: (([String: Any]) -> Void)?

  /// Called (on the state queue) when the engine stops the input monitor on its
  /// own; the argument says why (`interrupted`).
  var onInputMonitorStopped: ((String) -> Void)?

  init() {
    wireSubsystemCallbacks()
  }

  func initialize(config: [String: Any]) {
    let parsedConfig = AudioEngineConfig(dictionary: config)
    queue.sync {
      state.lastConfig = parsedConfig
      let configured = state.backgroundPlaybackEnabled
        ? sessionCoordinator.configureBackground(with: parsedConfig)
        : sessionCoordinator.configureForeground(with: parsedConfig)
      if !configured {
        eventEmitter.emitError(
          code: "AUDIO_SESSION_FAILED",
          message: sessionCoordinator.lastErrorDescription() ?? "Audio session configuration failed",
          details: ["backgroundPlaybackEnabled": state.backgroundPlaybackEnabled]
        )
      }

      _ = recordingController.prepareIfNeeded(
        runtime: runtime,
        sessionCoordinator: sessionCoordinator,
        config: parsedConfig,
        state: state
      )
      runtime.prepare(masterVolume: state.masterVolume)
      state.isInitialized = true
      backgroundController.updateRemoteCommandStates(state: state)
    }
  }

  func releaseResources() {
    queue.sync {
      recordingController.stopInputMonitor(state: state)
      runtime.stopEngineIfRunning()
      runtime.stopAllPlayers(state: state)
      recordingController.stopRecordingInternal(state: state)
      runtime.detachAllTracks(state: state)
      state.tracks.removeAll()
      recordingController.detach(runtime: runtime)

      state.isPlaying = false
      state.isRecording = false
      state.currentPositionMs = 0.0
      state.durationMs = 0.0
      state.playbackStartHostTime = nil
      state.playbackStartPositionMs = 0.0
      state.activePlaybackCount = 0
      state.playbackToken = UUID()
      state.inputLevel = 0.0
      state.isInitialized = false

      backgroundController.disable(state: state)
      sessionCoordinator.deactivate()
    }
  }

  func loadTracks(_ trackInputs: [[String: Any]]) -> [String: Any]? {
    return queue.sync {
      ensureInitializedIfNeeded()
      runtime.stopEngineIfRunning()
      runtime.stopAllPlayers(state: state)

      var failedInputs: [[String: Any]] = []

      for input in trackInputs {
        guard let track = AudioTrack(input: input) else {
          failedInputs.append(input)
          continue
        }

        if let existing = state.tracks[track.id] {
          runtime.detachTrack(existing)
        }

        runtime.attachTrack(track, state: state)
        state.tracks[track.id] = track
      }

      runtime.applyMixingForAllTracks(state: state)
      runtime.applyPitchSpeedForAllTracks(state: state)
      runtime.recalculateDuration(state: state)
      runtime.prepare(masterVolume: state.masterVolume)
      // Loading stops the engine to attach the players; the input tap needs it
      // running again when the monitor or a take is using it.
      if state.isInputMonitoring || state.isRecording {
        runtime.startEngineIfNeeded(state: state, sessionCoordinator: sessionCoordinator, eventEmitter: eventEmitter)
      }
      backgroundController.updateRemoteCommandStates(state: state)
      syncBackgroundNowPlaying()

      if failedInputs.isEmpty {
        return nil
      }

      var details: [String: Any] = ["failedCount": failedInputs.count]
      let failedIds = failedInputs.compactMap { $0["id"] as? String }
      if !failedIds.isEmpty {
        details["failedTrackIds"] = failedIds
      }
      let failedUris = failedInputs.compactMap { $0["uri"] as? String }
      if !failedUris.isEmpty {
        details["failedTrackUris"] = failedUris
      }
      return details
    }
  }

  func unloadTrack(_ trackId: String) {
    queue.sync {
      if let track = state.tracks.removeValue(forKey: trackId) {
        runtime.detachTrack(track)
      }
      runtime.applyMixingForAllTracks(state: state)
      runtime.recalculateDuration(state: state)
      backgroundController.updateRemoteCommandStates(state: state)
      syncBackgroundNowPlaying()
    }
  }

  func unloadAllTracks() {
    queue.sync {
      runtime.stopAllPlayers(state: state)
      runtime.detachAllTracks(state: state)
      state.tracks.removeAll()
      runtime.recalculateDuration(state: state)
      backgroundController.updateRemoteCommandStates(state: state)
      syncBackgroundNowPlaying()
    }
  }

  func getLoadedTracks() -> [[String: Any]] {
    return queue.sync {
      state.tracks.values.map { $0.asDictionary() }
    }
  }

  func play() {
    queue.sync { playInternal() }
  }

  func pause() {
    queue.sync { pauseInternal() }
  }

  func stop() {
    queue.sync { stopInternal() }
  }

  func seek(positionMs: Double) {
    queue.sync { seekInternal(positionMs: positionMs) }
  }

  func isPlaying() -> Bool {
    return queue.sync { state.isPlaying }
  }

  func getCurrentPosition() -> Double {
    return queue.sync {
      if state.isPlaying {
        return runtime.currentPlaybackPositionMs(state: state)
      }
      return state.currentPositionMs
    }
  }

  func getDuration() -> Double {
    return queue.sync { state.durationMs }
  }

  func setTrackVolume(trackId: String, volume: Double) {
    queue.sync {
      guard let track = state.tracks[trackId] else { return }
      track.volume = volume
      runtime.applyMixingForAllTracks(state: state)
      syncBackgroundNowPlaying()
    }
  }

  func setTrackMuted(trackId: String, muted: Bool) {
    queue.sync {
      guard let track = state.tracks[trackId] else { return }
      track.muted = muted
      runtime.applyMixingForAllTracks(state: state)
      syncBackgroundNowPlaying()
    }
  }

  func setTrackSolo(trackId: String, solo: Bool) {
    queue.sync {
      guard let track = state.tracks[trackId] else { return }
      track.solo = solo
      runtime.applyMixingForAllTracks(state: state)
      syncBackgroundNowPlaying()
    }
  }

  func setTrackPan(trackId: String, pan: Double) {
    queue.sync {
      guard let track = state.tracks[trackId] else { return }
      track.pan = pan
      runtime.applyMixingForAllTracks(state: state)
    }
  }

  func setTrackPitch(trackId: String, semitones: Double) {
    queue.sync {
      guard let track = state.tracks[trackId] else { return }
      track.pitch = semitones
      runtime.applyPitchSpeedForAllTracks(state: state)
    }
  }

  func getTrackPitch(trackId: String) -> Double {
    return queue.sync {
      state.tracks[trackId]?.pitch ?? 0.0
    }
  }

  func setTrackSpeed(trackId: String, rate: Double) {
    queue.sync {
      guard let track = state.tracks[trackId] else { return }
      track.speed = rate
      runtime.applyPitchSpeedForAllTracks(state: state)
    }
  }

  func getTrackSpeed(trackId: String) -> Double {
    return queue.sync {
      state.tracks[trackId]?.speed ?? 1.0
    }
  }

  func setMasterVolume(_ volume: Double) {
    queue.sync {
      state.masterVolume = volume
      runtime.setMasterVolume(volume)
    }
  }

  func getMasterVolume() -> Double {
    return queue.sync { state.masterVolume }
  }

  func setPitch(_ semitones: Double) {
    queue.sync {
      state.globalPitch = semitones
      runtime.applyPitchSpeedForAllTracks(state: state)
    }
  }

  func getPitch() -> Double {
    return queue.sync { state.globalPitch }
  }

  func setSpeed(_ rate: Double) {
    queue.sync {
      state.globalSpeed = rate
      runtime.applyPitchSpeedForAllTracks(state: state)
    }
  }

  func getSpeed() -> Double {
    return queue.sync { state.globalSpeed }
  }

  func setTempoAndPitch(tempo: Double, pitch: Double) {
    queue.sync {
      state.globalSpeed = tempo
      state.globalPitch = pitch
      runtime.applyPitchSpeedForAllTracks(state: state)
    }
  }

  /// Starts a recording. The controller configures `.playAndRecord` and
  /// activates the session (`setActive(true)`); the session and the engine
  /// then stay active until `stopRecording`, an interruption or `release`:
  /// pausing or stopping playback does not stop the engine while recording,
  /// so the take keeps receiving input and, with `UIBackgroundModes: audio`,
  /// the app keeps running in the background.
  @discardableResult
  func startRecording(config: [String: Any]?) -> Bool {
    return queue.sync {
      ensureInitializedIfNeeded()
      return recordingController.startRecording(
        config: config,
        runtime: runtime,
        sessionCoordinator: sessionCoordinator,
        eventEmitter: eventEmitter,
        state: state
      )
    }
  }

  func stopRecording() -> [String: Any] {
    return queue.sync {
      let result = recordingController.stopRecording(state: state)
      // The engine was kept running for the take; release the input once
      // nothing else (playback, the input monitor) needs it.
      if !state.isPlaying && !state.isInputMonitoring {
        runtime.stopEngineIfRunning()
      }
      return result
    }
  }

  func isRecording() -> Bool {
    return queue.sync { state.isRecording }
  }

  func setRecordingVolume(_ volume: Double) {
    queue.sync {
      recordingController.setRecordingVolume(volume, state: state)
    }
  }

  func getLastRecordingError() -> String? {
    return queue.sync {
      recordingController.getLastRecordingError(state: state)
    }
  }

  func extractTrack(trackId: String, config: [String: Any]?) throws -> [String: Any] {
    return try queue.sync {
      try extractionController.extractTrack(
        trackId: trackId,
        config: config,
        runtime: runtime,
        state: state,
        ensureInitialized: { self.ensureInitializedIfNeeded() }
      )
    }
  }

  func extractAllTracks(config: [String: Any]?) throws -> [[String: Any]] {
    return try queue.sync {
      try extractionController.extractAllTracks(
        config: config,
        runtime: runtime,
        state: state,
        ensureInitialized: { self.ensureInitializedIfNeeded() }
      )
    }
  }

  func cancelExtraction(jobId: Double?) -> Bool {
    _ = jobId
    return false
  }

  /// Starts the input pitch monitor, or applies a new config to a running one.
  /// Returns nil on success, otherwise the error code and message.
  func startInputMonitor(config: [String: Any]?) -> (code: String, message: String)? {
    return queue.sync {
      ensureInitializedIfNeeded()
      var monitorConfig = InputMonitorConfig()
      if let minHz = (config?["minHz"] as? NSNumber)?.floatValue {
        monitorConfig.minHz = minHz
      }
      if let maxHz = (config?["maxHz"] as? NSNumber)?.floatValue {
        monitorConfig.maxHz = maxHz
      }
      if let intervalMs = (config?["intervalMs"] as? NSNumber)?.doubleValue {
        guard intervalMs.isFinite else {
          return ("INPUT_MONITOR_START_FAILED", "Failed to start input monitor: intervalMs must be a finite number")
        }
        monitorConfig.intervalMs = Int(intervalMs.rounded())
      }

      let failure = recordingController.startInputMonitor(
        config: monitorConfig,
        runtime: runtime,
        sessionCoordinator: sessionCoordinator,
        eventEmitter: eventEmitter,
        state: state
      )
      switch failure {
      case .none:
        return nil
      case .permissionDenied:
        return ("INPUT_MONITOR_PERMISSION_DENIED", "Microphone permission is not granted")
      case .startFailed(let detail):
        return ("INPUT_MONITOR_START_FAILED", "Failed to start input monitor: \(detail)")
      }
    }
  }

  func stopInputMonitor() {
    queue.sync {
      guard state.isInputMonitoring else { return }
      recordingController.stopInputMonitor(state: state)
      // Release the input once nothing else needs the engine.
      stopEngineUnlessInputActive()
    }
  }

  func isInputMonitoring() -> Bool {
    return queue.sync { state.isInputMonitoring }
  }

  func getInputLevel() -> Double {
    return queue.sync {
      recordingController.getInputLevel(state: state)
    }
  }

  func getOutputLevel() -> Double {
    return 0.0
  }

  func getTrackLevel(trackId: String) -> Double {
    _ = trackId
    return 0.0
  }

  func enableBackgroundPlayback(metadata: [String: Any]) {
    queue.sync {
      let configured = sessionCoordinator.configureBackground(with: state.lastConfig)
      if !configured {
        eventEmitter.emitError(
          code: "AUDIO_SESSION_FAILED",
          message: sessionCoordinator.lastErrorDescription() ?? "Audio session configuration failed",
          details: ["backgroundPlaybackEnabled": true]
        )
      }
      backgroundController.enable(metadata: metadata, state: state)
      syncBackgroundNowPlaying()
    }
  }

  func updateNowPlayingInfo(metadata: [String: Any]) {
    queue.sync {
      backgroundController.updateMetadata(metadata, state: state)
      syncBackgroundNowPlaying()
    }
  }

  func disableBackgroundPlayback() {
    queue.sync {
      backgroundController.disable(state: state)
      let configured = sessionCoordinator.configureForeground(with: state.lastConfig)
      if !configured {
        eventEmitter.emitError(
          code: "AUDIO_SESSION_FAILED",
          message: sessionCoordinator.lastErrorDescription() ?? "Audio session configuration failed",
          details: ["backgroundPlaybackEnabled": false]
        )
      }
    }
  }

  private func wireSubsystemCallbacks() {
    runtime.onPlaybackCompleted = { [weak self] in
      self?.handlePlaybackCompleted()
    }

    recordingController.onInputPitchWindow = { [weak self] estimate in
      self?.emitInputPitch(estimate)
    }

    backgroundController.onPlayRequested = { [weak self] in
      self?.queue.async { self?.playInternal() }
    }
    backgroundController.onPauseRequested = { [weak self] in
      self?.queue.async { self?.pauseInternal() }
    }
    backgroundController.onToggleRequested = { [weak self] in
      self?.queue.async {
        guard let self = self else { return }
        if self.state.isPlaying {
          self.pauseInternal()
        } else {
          self.playInternal()
        }
      }
    }
    backgroundController.onSeekRequested = { [weak self] positionMs in
      self?.queue.async { self?.seekInternal(positionMs: positionMs) }
    }
    backgroundController.onSkipRequested = { [weak self] deltaMs in
      self?.queue.async { self?.seekByIntervalInternal(deltaMs: deltaMs) }
    }

    sessionCoordinator.onInterruptionBegan = { [weak self] in
      self?.queue.async { self?.handleSessionInterruptionBegan() }
    }
    sessionCoordinator.onInterruptionEnded = { [weak self] shouldResume in
      self?.queue.async { self?.handleSessionInterruptionEnded(shouldResume: shouldResume) }
    }
    sessionCoordinator.onRouteChanged = { [weak self] _ in
      self?.queue.async { self?.syncBackgroundNowPlaying() }
    }
  }

  private func ensureInitializedIfNeeded() {
    if state.isInitialized {
      return
    }

    let configured = state.backgroundPlaybackEnabled
      ? sessionCoordinator.configureBackground(with: state.lastConfig)
      : sessionCoordinator.configureForeground(with: state.lastConfig)

    if !configured {
      eventEmitter.emitError(
        code: "AUDIO_SESSION_FAILED",
        message: sessionCoordinator.lastErrorDescription() ?? "Audio session configuration failed",
        details: ["backgroundPlaybackEnabled": state.backgroundPlaybackEnabled]
      )
    }

    _ = recordingController.prepareIfNeeded(
      runtime: runtime,
      sessionCoordinator: sessionCoordinator,
      config: state.lastConfig,
      state: state
    )
    runtime.prepare(masterVolume: state.masterVolume)
    state.isInitialized = true
  }

  private func playInternal() {
    guard !state.tracks.isEmpty else {
      eventEmitter.emitError(
        code: "NO_TRACKS_LOADED",
        message: "Cannot start playback without loaded tracks."
      )
      return
    }

    ensureInitializedIfNeeded()
    if state.isPlaying {
      return
    }

    guard runtime.startEngineIfNeeded(state: state, sessionCoordinator: sessionCoordinator, eventEmitter: eventEmitter) else {
      return
    }

    runtime.schedulePlayback(at: state.currentPositionMs, state: state)
    state.isPlaying = true
    syncBackgroundNowPlaying()
    eventEmitter.emitPlaybackState(
      .playing,
      positionMs: runtime.currentPlaybackPositionMs(state: state),
      durationMs: state.durationMs
    )
  }

  private func pauseInternal() {
    guard state.isPlaying else { return }
    state.currentPositionMs = runtime.currentPlaybackPositionMs(state: state)
    state.isPlaying = false
    runtime.stopAllPlayers(state: state)
    stopEngineUnlessInputActive()
    syncBackgroundNowPlaying()
    eventEmitter.emitPlaybackState(
      .paused,
      positionMs: state.currentPositionMs,
      durationMs: state.durationMs
    )
  }

  private func stopInternal() {
    let shouldEmit = state.isPlaying || state.currentPositionMs != 0.0
    state.isPlaying = false
    state.currentPositionMs = 0.0
    state.playbackStartHostTime = nil
    state.playbackStartPositionMs = 0.0
    state.playbackToken = UUID()
    runtime.stopAllPlayers(state: state)
    stopEngineUnlessInputActive()
    syncBackgroundNowPlaying()
    if shouldEmit {
      eventEmitter.emitPlaybackState(
        .stopped,
        positionMs: state.currentPositionMs,
        durationMs: state.durationMs
      )
    }
  }

  private func seekInternal(positionMs: Double) {
    state.currentPositionMs = max(0.0, positionMs)
    if state.isPlaying {
      guard runtime.startEngineIfNeeded(state: state, sessionCoordinator: sessionCoordinator, eventEmitter: eventEmitter) else {
        return
      }
      runtime.schedulePlayback(at: state.currentPositionMs, state: state)
    }
    syncBackgroundNowPlaying()
  }

  private func seekByIntervalInternal(deltaMs: Double) {
    let current = state.isPlaying ? runtime.currentPlaybackPositionMs(state: state) : state.currentPositionMs
    let maxPosition = state.durationMs > 0 ? state.durationMs : Double.greatestFiniteMagnitude
    let target = min(max(0.0, current + deltaMs), maxPosition)
    seekInternal(positionMs: target)
  }

  private func handlePlaybackCompleted() {
    if !state.isPlaying {
      return
    }

    state.isPlaying = false
    state.currentPositionMs = state.durationMs
    state.playbackStartHostTime = nil
    state.playbackStartPositionMs = 0.0
    runtime.stopAllPlayers(state: state)
    stopEngineUnlessInputActive()
    syncBackgroundNowPlaying()
    eventEmitter.emitPlaybackState(
      .stopped,
      positionMs: state.currentPositionMs,
      durationMs: state.durationMs
    )
    eventEmitter.emitPlaybackComplete(positionMs: state.currentPositionMs, durationMs: state.durationMs)
  }

  /// Playback paths call this instead of stopping the engine directly: while a
  /// take is in progress or the input monitor runs, the engine (and with it the
  /// active session and the input tap) must keep running.
  private func stopEngineUnlessInputActive() {
    if state.isRecording || state.isInputMonitoring || state.isPlaying {
      return
    }
    runtime.stopEngineIfRunning()
  }

  /// Runs on the state queue, once per analysed window.
  private func emitInputPitch(_ estimate: PitchEstimate) {
    guard let onInputPitch = onInputPitch else { return }
    onInputPitch([
      "hz": Double(estimate.hz),
      "confidence": Double(estimate.confidence),
      "level": Double(estimate.level),
      "positionMs": inputMonitorPositionMs(),
      "timestampMs": Date().timeIntervalSince1970 * 1000.0
    ])
  }

  /// Transport position for `inputPitch`: -1 when nothing is loaded or the
  /// transport is stopped (not playing, at position 0).
  private func inputMonitorPositionMs() -> Double {
    if state.tracks.isEmpty {
      return -1.0
    }
    if state.isPlaying {
      return runtime.currentPlaybackPositionMs(state: state)
    }
    return state.currentPositionMs > 0 ? state.currentPositionMs : -1.0
  }

  private func handleSessionInterruptionBegan() {
    if state.isInputMonitoring {
      // The system stopped the engine; the tap delivers nothing until it is
      // restarted, so end the monitor and let the consumer restart it.
      recordingController.stopInputMonitor(state: state)
      onInputMonitorStopped?("interrupted")
    }
    if state.isRecording {
      // The system has stopped the engine (a call, Siri, another app's
      // session). Finalise the take now so the file is complete and the app
      // learns about it, instead of leaving an open file that receives no input.
      let result = recordingController.stopRecording(state: state)
      onRecordingInterrupted?(result)
    }
    if state.isPlaying {
      pauseInternal()
    } else {
      runtime.stopEngineIfRunning()
    }
  }

  private func handleSessionInterruptionEnded(shouldResume: Bool) {
    if shouldResume && !state.isPlaying && state.currentPositionMs > 0 {
      playInternal()
    }
  }

  private func syncBackgroundNowPlaying() {
    guard state.backgroundPlaybackEnabled else { return }

    let position = state.isPlaying ? runtime.currentPlaybackPositionMs(state: state) : state.currentPositionMs
    backgroundController.updateNowPlayingInfo(
      state: state,
      positionMs: position,
      durationMs: state.durationMs,
      isPlaying: state.isPlaying
    )
  }
}
