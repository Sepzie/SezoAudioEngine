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
      return recordingController.stopRecording(state: state)
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
    runtime.stopEngineIfRunning()
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
    runtime.stopEngineIfRunning()
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
    runtime.stopEngineIfRunning()
    syncBackgroundNowPlaying()
    eventEmitter.emitPlaybackState(
      .stopped,
      positionMs: state.currentPositionMs,
      durationMs: state.durationMs
    )
    eventEmitter.emitPlaybackComplete(positionMs: state.currentPositionMs, durationMs: state.durationMs)
  }

  private func handleSessionInterruptionBegan() {
    if state.isPlaying {
      pauseInternal()
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
