import AVFoundation
import MediaPlayer
import UIKit

/// Swift wrapper around `AVAudioEngine` that backs the Expo module API.
/// Keeps state on a single serial queue to avoid thread-safety issues.
final class NativeAudioEngine {
  private enum PlaybackState: String {
    case stopped
    case playing
    case paused
    case recording
  }

  /// Serial queue to protect engine state and avoid races with audio callbacks.
  private let queue = DispatchQueue(label: "sezo.audioengine.state")
  /// Core iOS audio graph.
  private let engine = AVAudioEngine()
  /// Manages the shared `AVAudioSession` setup.
  private let sessionManager = AudioSessionManager()
  /// Loaded tracks keyed by their JS `id`.
  private var tracks: [String: AudioTrack] = [:]
  /// Master controls and global effects.
  private var masterVolume: Double = 1.0
  private var globalPitch: Double = 0.0
  private var globalSpeed: Double = 1.0
  /// Transport and playback state.
  private var isPlayingFlag = false
  private var isRecordingFlag = false
  private var currentPositionMs: Double = 0.0
  private var durationMs: Double = 0.0
  /// Recording state and simple metering.
  private var recordingVolume: Double = 1.0
  private var isInitialized = false
  private var playbackStartHostTime: UInt64?
  private var playbackStartPositionMs: Double = 0.0
  private var activePlaybackCount = 0
  private var playbackToken = UUID()
  private var recordingState: RecordingState?
  private var lastRecordingError: String?
  private var lastEngineError: String?
  private let recordingMixer = AVAudioMixerNode()
  private var recordingMixerConnected = false
  private var recordingTapInstalled = false
  private var inputLevel: Double = 0.0
  private var backgroundPlaybackEnabled = false
  private var nowPlayingMetadata: [String: Any] = [:]
  private var playCommandTarget: Any?
  private var pauseCommandTarget: Any?
  private var toggleCommandTarget: Any?
  private var lastConfig = AudioEngineConfig(dictionary: [:])
  var onPlaybackStateChange: ((String, Double, Double) -> Void)?
  var onPlaybackComplete: ((Double, Double) -> Void)?
  var onError: ((String, String, [String: Any]?) -> Void)?

  /// Bookkeeping for a live recording session.
  private struct RecordingState {
    let url: URL
    let file: AVAudioFile
    let format: String
    let sampleRate: Double
    let channels: Int
    var startTimeMs: Double
    var startTimeSamples: Int64
    var startTimeResolved: Bool
  }

  /// Initializes the audio session and prepares the engine.
  func initialize(config: [String: Any]) {
    let parsedConfig = AudioEngineConfig(dictionary: config)
    queue.sync {
      lastConfig = parsedConfig
      _ = sessionManager.configure(with: parsedConfig)
      _ = ensureRecordingMixerConnected(
        sampleRate: AVAudioSession.sharedInstance().sampleRate,
        channels: AVAudioSession.sharedInstance().inputNumberOfChannels
      )
      engine.mainMixerNode.outputVolume = Float(masterVolume)
      engine.prepare()
      isInitialized = true
    }
  }

  /// Stops audio, clears state, and releases resources.
  func releaseResources() {
    queue.sync {
      stopEngineIfRunning()
      stopAllPlayers()
      stopRecordingInternal()
      detachAllTracks()
      tracks.removeAll()
      detachRecordingMixer()
      isPlayingFlag = false
      isRecordingFlag = false
      currentPositionMs = 0.0
      durationMs = 0.0
      playbackStartHostTime = nil
      playbackStartPositionMs = 0.0
      activePlaybackCount = 0
      playbackToken = UUID()
      inputLevel = 0.0
      isInitialized = false
      sessionManager.deactivate()
    }
  }

  /// Loads and attaches tracks into the engine graph.
  func loadTracks(_ trackInputs: [[String: Any]]) -> [String: Any]? {
    return queue.sync {
      ensureInitializedIfNeeded()
      stopEngineIfRunning()
      stopAllPlayers()
      var failedInputs: [[String: Any]] = []
      for input in trackInputs {
        guard let track = AudioTrack(input: input) else {
          failedInputs.append(input)
          continue
        }
        if let existing = tracks[track.id] {
          detachTrack(existing)
        }
        attachTrack(track)
        tracks[track.id] = track
      }
      applyMixingForAllTracks()
      applyPitchSpeedForAllTracks()
      recalculateDuration()
      engine.prepare()
      updateRemoteCommandStates()
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

  /// Unloads a single track by ID.
  func unloadTrack(_ trackId: String) {
    queue.sync {
      if let track = tracks.removeValue(forKey: trackId) {
        detachTrack(track)
      }
      applyMixingForAllTracks()
      recalculateDuration()
      updateRemoteCommandStates()
    }
  }

  /// Unloads all tracks and clears the graph.
  func unloadAllTracks() {
    queue.sync {
      stopAllPlayers()
      detachAllTracks()
      tracks.removeAll()
      recalculateDuration()
      updateRemoteCommandStates()
    }
  }

  /// Returns track metadata for JS.
  func getLoadedTracks() -> [[String: Any]] {
    return queue.sync {
      return tracks.values.map { $0.asDictionary() }
    }
  }

  /// Starts playback from the current position.
  func play() {
    queue.sync { playInternal() }
  }

  /// Pauses playback and stores the current position.
  func pause() {
    queue.sync { pauseInternal() }
  }

  /// Stops playback and resets position to zero.
  func stop() {
    queue.sync { stopInternal() }
  }

  /// Seeks to a position in milliseconds.
  func seek(positionMs: Double) {
    queue.sync { seekInternal(positionMs: positionMs) }
  }

  /// Returns whether playback is active.
  func isPlaying() -> Bool {
    return queue.sync { isPlayingFlag }
  }

  /// Returns the current playback position in milliseconds.
  func getCurrentPosition() -> Double {
    return queue.sync {
      if isPlayingFlag {
        return currentPlaybackPositionMs()
      }
      return currentPositionMs
    }
  }

  /// Returns the total duration across tracks in milliseconds.
  func getDuration() -> Double {
    return queue.sync { durationMs }
  }

  /// Per-track volume (0.0 - 2.0).
  func setTrackVolume(trackId: String, volume: Double) {
    queue.sync {
      guard let track = tracks[trackId] else { return }
      track.volume = volume
      applyMixingForAllTracks()
    }
  }

  /// Per-track mute toggle.
  func setTrackMuted(trackId: String, muted: Bool) {
    queue.sync {
      guard let track = tracks[trackId] else { return }
      track.muted = muted
      applyMixingForAllTracks()
    }
  }

  /// Per-track solo toggle.
  func setTrackSolo(trackId: String, solo: Bool) {
    queue.sync {
      guard let track = tracks[trackId] else { return }
      track.solo = solo
      applyMixingForAllTracks()
    }
  }

  /// Per-track stereo pan (-1.0 left, 1.0 right).
  func setTrackPan(trackId: String, pan: Double) {
    queue.sync {
      guard let track = tracks[trackId] else { return }
      track.pan = pan
      applyMixingForAllTracks()
    }
  }

  /// Per-track pitch in semitones.
  func setTrackPitch(trackId: String, semitones: Double) {
    queue.sync {
      guard let track = tracks[trackId] else { return }
      track.pitch = semitones
      applyPitchSpeed(for: track)
    }
  }

  /// Reads the current pitch for a track.
  func getTrackPitch(trackId: String) -> Double {
    return queue.sync {
      return tracks[trackId]?.pitch ?? 0.0
    }
  }

  /// Per-track time-stretch rate (1.0 = normal).
  func setTrackSpeed(trackId: String, rate: Double) {
    queue.sync {
      guard let track = tracks[trackId] else { return }
      track.speed = rate
      applyPitchSpeed(for: track)
    }
  }

  /// Reads the current speed for a track.
  func getTrackSpeed(trackId: String) -> Double {
    return queue.sync {
      return tracks[trackId]?.speed ?? 1.0
    }
  }

  /// Master volume for the whole engine.
  func setMasterVolume(_ volume: Double) {
    queue.sync {
      masterVolume = volume
      engine.mainMixerNode.outputVolume = Float(volume)
    }
  }

  /// Reads the master volume.
  func getMasterVolume() -> Double {
    return queue.sync { masterVolume }
  }

  /// Global pitch applied to all tracks.
  func setPitch(_ semitones: Double) {
    queue.sync {
      globalPitch = semitones
      applyPitchSpeedForAllTracks()
    }
  }

  /// Reads global pitch.
  func getPitch() -> Double {
    return queue.sync { globalPitch }
  }

  /// Global speed applied to all tracks.
  func setSpeed(_ rate: Double) {
    queue.sync {
      globalSpeed = rate
      applyPitchSpeedForAllTracks()
    }
  }

  /// Reads global speed.
  func getSpeed() -> Double {
    return queue.sync { globalSpeed }
  }

  /// Sets global tempo (speed) and pitch together.
  func setTempoAndPitch(tempo: Double, pitch: Double) {
    queue.sync {
      globalSpeed = tempo
      globalPitch = pitch
      applyPitchSpeedForAllTracks()
    }
  }

  /// Starts recording from the input node into AAC or WAV.
  @discardableResult
  func startRecording(config: [String: Any]?) -> Bool {
    return queue.sync {
      guard !isRecordingFlag else { return true }
      lastRecordingError = nil
      let session = AVAudioSession.sharedInstance()
      let permission = session.recordPermission
      if permission != .granted {
        if permission == .undetermined {
          requestRecordPermission()
          lastRecordingError = "microphone permission not determined"
        } else {
          lastRecordingError = "microphone permission denied"
        }
        return false
      }
      if !sessionManager.configure(with: lastConfig) {
        lastRecordingError = sessionManager.lastErrorDescription() ?? "audio session configuration failed"
        return false
      }
      if !session.isInputAvailable {
        let routeInfo = describeRoute(session)
        lastRecordingError = "audio input not available (category=\(session.category.rawValue), route=\(routeInfo))"
        return false
      }
      ensureInitializedIfNeeded()

      guard startEngineIfNeeded() else {
        lastRecordingError = lastEngineError ?? "audio engine failed to start"
        return false
      }

      let sampleRate = session.sampleRate
      let channels = max(1, session.inputNumberOfChannels)
      if !ensureRecordingMixerConnected(sampleRate: sampleRate, channels: channels) {
        return false
      }

      let tapFormat = recordingMixer.outputFormat(forBus: 0)
      guard tapFormat.sampleRate > 0, tapFormat.channelCount > 0 else {
        let routeInfo = describeRoute(session)
        lastRecordingError = "input format invalid (sampleRate=\(tapFormat.sampleRate), channels=\(tapFormat.channelCount), sessionRate=\(session.sampleRate), sessionChannels=\(session.inputNumberOfChannels), route=\(routeInfo))"
        return false
      }
      let requestedFormat = config?["format"] as? String ?? "aac"
      let bitrate = resolveBitrate(config: config)
      let formatInfo = resolveRecordingFormat(requestedFormat: requestedFormat)
      let outputURL = resolveOutputURL(
        prefix: "recording",
        fileExtension: formatInfo.fileExtension,
        outputDir: config?["outputDir"] as? String
      )

      let settings: [String: Any]
      if formatInfo.format == "wav" {
        settings = [
          AVFormatIDKey: kAudioFormatLinearPCM,
          AVSampleRateKey: sampleRate,
          AVNumberOfChannelsKey: channels,
          AVLinearPCMBitDepthKey: 16,
          AVLinearPCMIsBigEndianKey: false,
          AVLinearPCMIsFloatKey: false
        ]
      } else {
        settings = [
          AVFormatIDKey: kAudioFormatMPEG4AAC,
          AVSampleRateKey: sampleRate,
          AVNumberOfChannelsKey: channels,
          AVEncoderBitRateKey: bitrate
        ]
      }

      do {
        let file = try AVAudioFile(forWriting: outputURL, settings: settings)

        // Capture start time (finalized on first tap buffer when playing).
        let startTimeMs: Double
        let startTimeSamples: Int64
        let startTimeResolved: Bool

        if isPlayingFlag {
          // Use the current playback position from the mix timeline.
          startTimeMs = currentPlaybackPositionMs()
          startTimeSamples = Int64(startTimeMs / 1000.0 * sampleRate)
          startTimeResolved = false
        } else {
          startTimeSamples = 0
          startTimeMs = 0.0
          startTimeResolved = true
        }

        recordingState = RecordingState(
          url: outputURL,
          file: file,
          format: formatInfo.format,
          sampleRate: sampleRate,
          channels: channels,
          startTimeMs: startTimeMs,
          startTimeSamples: startTimeSamples,
          startTimeResolved: startTimeResolved
        )

        isRecordingFlag = true
        return true
      } catch {
        lastRecordingError = "recording setup failed: \(error.localizedDescription)"
        recordingState = nil
        return false
      }
    }
  }

  private func requestRecordPermission() {
    let session = AVAudioSession.sharedInstance()
    let requestBlock = {
      session.requestRecordPermission { _ in }
    }
    if Thread.isMainThread {
      requestBlock()
    } else {
      DispatchQueue.main.async(execute: requestBlock)
    }
  }

  /// Stops recording and returns metadata about the output file.
  func stopRecording() -> [String: Any] {
    return queue.sync {
      guard let state = recordingState else {
        isRecordingFlag = false
        return [
          "uri": "",
          "duration": 0,
          "startTimeMs": 0,
          "sampleRate": 44100,
          "channels": 1,
          "format": "aac",
          "fileSize": 0
        ]
      }

      stopRecordingInternal()

      let durationMs = (Double(state.file.length) / state.sampleRate) * 1000.0
      let fileSize = resolveFileSize(url: state.url)

      return [
        "uri": state.url.absoluteString,
        "duration": durationMs,
        "startTimeMs": state.startTimeMs,
        "startTimeSamples": state.startTimeSamples,
        "sampleRate": state.sampleRate,
        "channels": state.channels,
        "format": state.format,
        "fileSize": fileSize
      ]
    }
  }

  /// Returns whether recording is active.
  func isRecording() -> Bool {
    return queue.sync { isRecordingFlag }
  }

  /// Applies a simple gain to recorded input before writing.
  func setRecordingVolume(_ volume: Double) {
    queue.sync {
      recordingVolume = volume
    }
  }

  func getLastRecordingError() -> String? {
    return queue.sync { lastRecordingError }
  }

  /// Offline export for a single track.
  func extractTrack(trackId: String, config: [String: Any]?) throws -> [String: Any] {
    return try queue.sync {
      guard let track = tracks[trackId] else {
        throw AudioEngineError(
          "TRACK_NOT_FOUND",
          "Track not found: \(trackId).",
          details: ["trackId": trackId]
        )
      }

      let durationMs = track.startTimeMs + track.durationMs
      return try renderOffline(
        tracksToRender: [track],
        totalDurationMs: durationMs,
        config: config,
        trackId: trackId
      )
    }
  }

  /// Offline export for all tracks (one file per track).
  func extractAllTracks(config: [String: Any]?) throws -> [[String: Any]] {
    return try queue.sync {
      if tracks.isEmpty {
        throw AudioEngineError(
          "NO_TRACKS_LOADED",
          "Cannot extract without loaded tracks."
        )
      }
      var results: [[String: Any]] = []
      for track in tracks.values {
        let durationMs = track.startTimeMs + track.durationMs
        let result = try renderOffline(
          tracksToRender: [track],
          totalDurationMs: durationMs,
          config: config,
          trackId: track.id
        )
        results.append(result)
      }
      return results
    }
  }

  /// Placeholder for cancelation (not implemented yet).
  func cancelExtraction(jobId: Double?) -> Bool {
    _ = jobId
    return false
  }

  /// Returns the last computed input RMS level.
  func getInputLevel() -> Double {
    return inputLevel
  }

  /// Output metering placeholder.
  func getOutputLevel() -> Double {
    return 0.0
  }

  /// Per-track metering placeholder.
  func getTrackLevel(trackId: String) -> Double {
    _ = trackId
    return 0.0
  }

  /// Background playback placeholder (to be implemented).
  func enableBackgroundPlayback(metadata: [String: Any]) {
    queue.sync {
      backgroundPlaybackEnabled = true
      nowPlayingMetadata.merge(metadata) { _, new in new }
      _ = sessionManager.enableBackgroundPlayback(with: lastConfig)
      setRemoteControlEventsEnabled(true)
      configureRemoteCommandsIfNeeded()
      updateNowPlayingInfoInternal()
    }
  }

  /// Now Playing updates placeholder (to be implemented).
  func updateNowPlayingInfo(metadata: [String: Any]) {
    queue.sync {
      nowPlayingMetadata.merge(metadata) { _, new in new }
      updateNowPlayingInfoInternal()
    }
  }

  /// Background playback teardown placeholder.
  func disableBackgroundPlayback() {
    queue.sync {
      backgroundPlaybackEnabled = false
      nowPlayingMetadata.removeAll()
      removeRemoteCommands()
      MPNowPlayingInfoCenter.default().nowPlayingInfo = nil
      if #available(iOS 13.0, *) {
        MPNowPlayingInfoCenter.default().playbackState = .stopped
      }
      setRemoteControlEventsEnabled(false)
      _ = sessionManager.configure(with: lastConfig)
    }
  }

  /// Updates cached total duration based on track offsets.
  private func recalculateDuration() {
    durationMs = tracks.values.reduce(0.0) { current, track in
      let endMs = track.startTimeMs + track.durationMs
      return max(current, endMs)
    }
  }

  /// Ensures the engine and session are initialized before use.
  private func ensureInitializedIfNeeded() {
    if isInitialized {
      return
    }
    let config = lastConfig
    let configured: Bool
    if backgroundPlaybackEnabled {
      configured = sessionManager.enableBackgroundPlayback(with: config)
    } else {
      configured = sessionManager.configure(with: config)
    }
    if !configured {
      emitError(
        code: "AUDIO_SESSION_FAILED",
        message: sessionManager.lastErrorDescription() ?? "Audio session configuration failed",
        details: ["backgroundPlaybackEnabled": backgroundPlaybackEnabled]
      )
    }
    _ = ensureRecordingMixerConnected(
      sampleRate: AVAudioSession.sharedInstance().sampleRate,
      channels: AVAudioSession.sharedInstance().inputNumberOfChannels
    )
    engine.mainMixerNode.outputVolume = Float(masterVolume)
    engine.prepare()
    isInitialized = true
  }

  /// Schedules all tracks from a given position with shared host time.
  private func schedulePlayback(at positionMs: Double) {
    stopAllPlayers()
    playbackToken = UUID()
    activePlaybackCount = 0

    let baseHostTime = mach_absolute_time() + AVAudioTime.hostTime(forSeconds: 0.05)
    let token = playbackToken

    for track in tracks.values {
      scheduleTrack(
        track,
        baseHostTime: baseHostTime,
        positionMs: positionMs,
        token: token
      )
    }

    playbackStartHostTime = baseHostTime
    playbackStartPositionMs = positionMs
  }

  /// Schedules a single track into the realtime engine.
  private func scheduleTrack(
    _ track: AudioTrack,
    baseHostTime: UInt64,
    positionMs: Double,
    token: UUID
  ) {
    let localStartMs = positionMs - track.startTimeMs
    if localStartMs >= track.durationMs {
      return
    }

    let fileOffsetMs = max(0.0, localStartMs)
    let delayMs = max(0.0, -localStartMs)
    let sampleRate = track.file.processingFormat.sampleRate
    if sampleRate <= 0 {
      return
    }

    let startFrame = AVAudioFramePosition((fileOffsetMs / 1000.0) * sampleRate)
    let framesRemaining = track.file.length - startFrame
    if framesRemaining <= 0 {
      return
    }

    let frameCount = AVAudioFrameCount(framesRemaining)
    activePlaybackCount += 1

    track.playerNode.scheduleSegment(
      track.file,
      startingFrame: startFrame,
      frameCount: frameCount,
      at: nil
    ) { [weak self] in
      self?.queue.async {
        guard let self = self else { return }
        if self.playbackToken != token {
          return
        }
        self.activePlaybackCount -= 1
        if self.activePlaybackCount <= 0 {
          self.handlePlaybackCompleted()
        }
      }
    }

    let hostTimeDelay = AVAudioTime.hostTime(forSeconds: delayMs / 1000.0)
    let hostTime = baseHostTime + hostTimeDelay
    track.playerNode.play(at: AVAudioTime(hostTime: hostTime))
  }

  /// Schedules a single track for offline rendering.
  private func scheduleTrackForManualRendering(
    _ track: AudioTrack,
    positionMs: Double,
    sampleRate: Double
  ) -> Bool {
    let localStartMs = positionMs - track.startTimeMs
    if localStartMs >= track.durationMs {
      return false
    }

    let fileOffsetMs = max(0.0, localStartMs)
    let delayMs = max(0.0, -localStartMs)
    let trackSampleRate = track.file.processingFormat.sampleRate
    if trackSampleRate <= 0 {
      return false
    }

    let startFrame = AVAudioFramePosition((fileOffsetMs / 1000.0) * trackSampleRate)
    let framesRemaining = track.file.length - startFrame
    if framesRemaining <= 0 {
      return false
    }

    let frameCount = AVAudioFrameCount(framesRemaining)
    let startSample = AVAudioFramePosition((delayMs / 1000.0) * sampleRate)
    let startTime = AVAudioTime(sampleTime: startSample, atRate: sampleRate)

    track.playerNode.scheduleSegment(
      track.file,
      startingFrame: startFrame,
      frameCount: frameCount,
      at: startTime,
      completionHandler: nil
    )
    return true
  }

  /// Handles end-of-playback cleanup once all tracks finish.
  private func handlePlaybackCompleted() {
    if !isPlayingFlag {
      return
    }
    isPlayingFlag = false
    currentPositionMs = durationMs
    playbackStartHostTime = nil
    playbackStartPositionMs = 0.0
    stopAllPlayers()
    stopEngineIfRunning()
    updateNowPlayingInfoInternal()
    emitPlaybackState(.stopped, positionMs: currentPositionMs)
    onPlaybackComplete?(currentPositionMs, durationMs)
  }

  /// Computes playback position from host time.
  private func currentPlaybackPositionMs() -> Double {
    guard let startHostTime = playbackStartHostTime else {
      return currentPositionMs
    }

    let nowSeconds = AVAudioTime.seconds(forHostTime: mach_absolute_time())
    let startSeconds = AVAudioTime.seconds(forHostTime: startHostTime)
    let elapsedMs = max(0.0, (nowSeconds - startSeconds) * 1000.0)
    let position = playbackStartPositionMs + elapsedMs
    return min(position, durationMs)
  }

  /// Stops the input tap and clears recording state.
  private func stopRecordingInternal() {
    recordingState = nil
    isRecordingFlag = false
  }

  private func ensureRecordingMixerConnected(sampleRate: Double, channels: Int) -> Bool {
    if recordingMixerConnected {
      return true
    }
    if engine.isRunning {
      lastRecordingError = "recording mixer unavailable while engine is running"
      return false
    }
    let session = AVAudioSession.sharedInstance()
    if !session.isInputAvailable {
      let routeInfo = describeRoute(session)
      lastRecordingError = "audio input not available (category=\(session.category.rawValue), route=\(routeInfo))"
      return false
    }

    let inputFormat = engine.inputNode.inputFormat(forBus: 0)
    let inputSampleRate = inputFormat.sampleRate
    let inputChannels = Int(inputFormat.channelCount)
    guard inputSampleRate > 0, inputChannels > 0 else {
      lastRecordingError = "input hw format invalid (sampleRate=\(inputSampleRate), channels=\(inputChannels))"
      return false
    }

    let resolvedRate = sampleRate > 0 ? sampleRate : (lastConfig.sampleRate ?? inputSampleRate)
    let resolvedChannels = channels > 0 ? channels : inputChannels
    let format: AVAudioFormat
    if resolvedRate == inputSampleRate && resolvedChannels == inputChannels {
      format = inputFormat
    } else if let preferredFormat = AVAudioFormat(
      standardFormatWithSampleRate: resolvedRate,
      channels: AVAudioChannelCount(resolvedChannels)
    ) {
      format = preferredFormat
    } else {
      lastRecordingError = "recording format invalid (sampleRate=\(resolvedRate), channels=\(resolvedChannels))"
      return false
    }

    engine.attach(recordingMixer)
    engine.connect(engine.inputNode, to: recordingMixer, format: format)
    engine.connect(recordingMixer, to: engine.mainMixerNode, format: format)
    recordingMixer.volume = 0.0
    if !recordingTapInstalled {
      recordingMixer.installTap(onBus: 0, bufferSize: 1024, format: format) { [weak self] buffer, _ in
        guard let self = self else { return }
        self.queue.async {
          guard let state = self.recordingState else { return }
          self.applyRecordingGain(buffer: buffer)
          self.updateInputLevel(buffer: buffer)
          do {
            try state.file.write(from: buffer)
          } catch {
            return
          }
        }
      }
      recordingTapInstalled = true
    }
    recordingMixerConnected = true
    return true
  }

  private func detachRecordingMixer() {
    if !recordingMixerConnected {
      return
    }
    if recordingTapInstalled {
      recordingMixer.removeTap(onBus: 0)
      recordingTapInstalled = false
    }
    engine.disconnectNodeInput(recordingMixer)
    engine.disconnectNodeOutput(recordingMixer)
    engine.detach(recordingMixer)
    recordingMixerConnected = false
  }

  /// Maps requested output formats to supported formats.
  private func resolveRecordingFormat(requestedFormat: String) -> (format: String, fileExtension: String) {
    if requestedFormat == "wav" {
      return ("wav", "wav")
    }
    if requestedFormat == "m4a" {
      return ("m4a", "m4a")
    }
    if requestedFormat == "mp3" {
      return ("aac", "m4a")
    }
    return ("aac", "m4a")
  }

  /// Resolves output bitrate from config or quality preset.
  private func resolveBitrate(config: [String: Any]?) -> Int {
    if let bitrate = config?["bitrate"] as? Int {
      return bitrate
    }
    if let quality = config?["quality"] as? String {
      switch quality {
      case "low":
        return 64_000
      case "high":
        return 192_000
      default:
        return 128_000
      }
    }
    return 128_000
  }

  /// Builds a file URL for recording/extraction output.
  private func resolveOutputURL(prefix: String, fileExtension: String, outputDir: String?) -> URL {
    let baseURL: URL
    if let outputDir = outputDir {
      baseURL = URL(fileURLWithPath: outputDir, isDirectory: true)
    } else {
      baseURL = FileManager.default.urls(for: .documentDirectory, in: .userDomainMask)[0]
        .appendingPathComponent("sezo-audio-output", isDirectory: true)
    }

    try? FileManager.default.createDirectory(at: baseURL, withIntermediateDirectories: true)
    let filename = "\(prefix)-\(UUID().uuidString).\(fileExtension)"
    return baseURL.appendingPathComponent(filename)
  }

  private func selectPreferredInput(_ session: AVAudioSession) {
    guard let inputs = session.availableInputs, !inputs.isEmpty else { return }
    if let builtIn = inputs.first(where: { $0.portType == .builtInMic }) {
      try? session.setPreferredInput(builtIn)
    }
  }

  private func describeRoute(_ session: AVAudioSession) -> String {
    let inputs = session.currentRoute.inputs.map { "\($0.portType.rawValue):\($0.portName)" }
    let outputs = session.currentRoute.outputs.map { "\($0.portType.rawValue):\($0.portName)" }
    return "inputs[\(inputs.joined(separator: ","))], outputs[\(outputs.joined(separator: ","))]"
  }

  /// Returns file size for output metadata.
  private func resolveFileSize(url: URL) -> Int {
    let path = url.path
    let attributes = try? FileManager.default.attributesOfItem(atPath: path)
    return attributes?[.size] as? Int ?? 0
  }

  /// Applies a simple pre-recording gain to the input buffer.
  private func applyRecordingGain(buffer: AVAudioPCMBuffer) {
    let gain = Float(recordingVolume)
    if gain == 1.0 {
      return
    }
    let frameLength = Int(buffer.frameLength)
    let channelCount = Int(buffer.format.channelCount)

    switch buffer.format.commonFormat {
    case .pcmFormatFloat32:
      guard let channelData = buffer.floatChannelData else { return }
      for channel in 0..<channelCount {
        let samples = channelData[channel]
        for i in 0..<frameLength {
          samples[i] *= gain
        }
      }
    case .pcmFormatInt16:
      guard let channelData = buffer.int16ChannelData else { return }
      for channel in 0..<channelCount {
        let samples = channelData[channel]
        for i in 0..<frameLength {
          let scaled = Float(samples[i]) * gain
          let clamped = max(Float(Int16.min), min(Float(Int16.max), scaled))
          samples[i] = Int16(clamped)
        }
      }
    default:
      break
    }
  }

  /// Updates `inputLevel` using RMS from the first channel.
  private func updateInputLevel(buffer: AVAudioPCMBuffer) {
    guard let channelData = buffer.floatChannelData else { return }
    let frameLength = Int(buffer.frameLength)
    if frameLength == 0 {
      return
    }
    let samples = channelData[0]
    var sum: Float = 0.0
    for i in 0..<frameLength {
      let value = samples[i]
      sum += value * value
    }
    let rms = sqrt(sum / Float(frameLength))
    inputLevel = Double(rms)
  }

  /// Renders one or more tracks offline into a file.
  private func renderOffline(
    tracksToRender: [AudioTrack],
    totalDurationMs: Double,
    config: [String: Any]?,
    trackId: String
  ) throws -> [String: Any] {
    ensureInitializedIfNeeded()
    stopEngineIfRunning()
    stopAllPlayers()

    let renderFormat = engine.mainMixerNode.outputFormat(forBus: 0)
    let formatInfo = resolveRecordingFormat(requestedFormat: config?["format"] as? String ?? "aac")
    let outputURL = resolveOutputURL(
      prefix: "extract-\(trackId)",
      fileExtension: formatInfo.fileExtension,
      outputDir: config?["outputDir"] as? String
    )
    let bitrate = resolveBitrate(config: config)

    let settings: [String: Any]
    if formatInfo.format == "wav" {
      settings = [
        AVFormatIDKey: kAudioFormatLinearPCM,
        AVSampleRateKey: renderFormat.sampleRate,
        AVNumberOfChannelsKey: renderFormat.channelCount,
        AVLinearPCMBitDepthKey: 16,
        AVLinearPCMIsBigEndianKey: false,
        AVLinearPCMIsFloatKey: false
      ]
    } else {
      settings = [
        AVFormatIDKey: kAudioFormatMPEG4AAC,
        AVSampleRateKey: renderFormat.sampleRate,
        AVNumberOfChannelsKey: renderFormat.channelCount,
        AVEncoderBitRateKey: bitrate
      ]
    }

    let includeEffects = config?["includeEffects"] as? Bool ?? true
    let originalState = snapshotTrackState()
    applyExtractionMixOverrides(tracksToRender: tracksToRender, includeEffects: includeEffects)

    let errorContext: [String: Any] = [
      "trackId": trackId,
      "format": formatInfo.format,
      "outputUri": outputURL.absoluteString
    ]

    do {
      try engine.enableManualRenderingMode(
        .offline,
        format: renderFormat,
        maximumFrameCount: 4096
      )

      var scheduledTracks: [AudioTrack] = []
      for track in tracksToRender {
        let scheduled = scheduleTrackForManualRendering(
          track,
          positionMs: 0.0,
          sampleRate: renderFormat.sampleRate
        )
        if scheduled {
          scheduledTracks.append(track)
        }
      }

      try engine.start()
      for track in scheduledTracks {
        track.playerNode.play()
      }

      let outputFile = try AVAudioFile(forWriting: outputURL, settings: settings)
      let totalFrames = AVAudioFramePosition((totalDurationMs / 1000.0) * renderFormat.sampleRate)

      while engine.manualRenderingSampleTime < totalFrames {
        let remaining = totalFrames - engine.manualRenderingSampleTime
        let frameCount = AVAudioFrameCount(min(Int(4096), Int(remaining)))
        guard let buffer = AVAudioPCMBuffer(
          pcmFormat: renderFormat,
          frameCapacity: frameCount
        ) else {
          throw AudioEngineError(
            "EXTRACTION_FAILED",
            "Failed to allocate render buffer.",
            details: errorContext
          )
        }

        let status = try engine.renderOffline(frameCount, to: buffer)
        switch status {
        case .success:
          try outputFile.write(from: buffer)
        case .insufficientDataFromInputNode:
          continue
        case .cannotDoInCurrentContext:
          continue
        case .error:
          throw AudioEngineError(
            "EXTRACTION_FAILED",
            "Offline render failed.",
            details: errorContext
          )
        @unknown default:
          throw AudioEngineError(
            "EXTRACTION_FAILED",
            "Offline render failed with unknown status.",
            details: errorContext
          )
        }
      }

      engine.stop()
      engine.disableManualRenderingMode()
    } catch {
      engine.disableManualRenderingMode()
      restoreTrackState(originalState)
      if let codedError = error as? AudioEngineError {
        throw codedError
      }
      var details = errorContext
      details["nativeError"] = error.localizedDescription
      throw AudioEngineError(
        "EXTRACTION_FAILED",
        "Failed to extract track \(trackId). \(error.localizedDescription)",
        details: details
      )
    }

    restoreTrackState(originalState)
    let fileSize = resolveFileSize(url: outputURL)
    return [
      "trackId": trackId,
      "uri": outputURL.absoluteString,
      "duration": totalDurationMs,
      "format": formatInfo.format,
      "bitrate": bitrate,
      "fileSize": fileSize
    ]
  }

  /// Snapshot of track parameters so offline render can override safely.
  private func snapshotTrackState() -> [String: (Double, Double, Bool, Bool, Double, Double)] {
    var snapshot: [String: (Double, Double, Bool, Bool, Double, Double)] = [:]
    for (trackId, track) in tracks {
      snapshot[trackId] = (track.volume, track.pan, track.muted, track.solo, track.pitch, track.speed)
    }
    return snapshot
  }

  /// Restores track parameters after offline render.
  private func restoreTrackState(_ snapshot: [String: (Double, Double, Bool, Bool, Double, Double)]) {
    for (trackId, values) in snapshot {
      guard let track = tracks[trackId] else { continue }
      track.volume = values.0
      track.pan = values.1
      track.muted = values.2
      track.solo = values.3
      track.pitch = values.4
      track.speed = values.5
    }
    applyMixingForAllTracks()
    applyPitchSpeedForAllTracks()
  }

  /// Applies extraction-specific overrides (mute others, optional effects).
  private func applyExtractionMixOverrides(tracksToRender: [AudioTrack], includeEffects: Bool) {
    let allowedIds = Set(tracksToRender.map { $0.id })
    for track in tracks.values {
      if !allowedIds.contains(track.id) {
        track.volume = 0.0
        track.muted = true
      } else {
        track.muted = false
      }

      if !includeEffects {
        track.pitch = 0.0
        track.speed = 1.0
      }
    }
    applyMixingForAllTracks()
    applyPitchSpeedForAllTracks()
  }

  /// Attaches a track's nodes into the engine graph.
  private func attachTrack(_ track: AudioTrack) {
    if track.isAttached {
      return
    }
    engine.attach(track.playerNode)
    engine.attach(track.timePitch)
    engine.connect(track.playerNode, to: track.timePitch, format: track.file.processingFormat)
    engine.connect(track.timePitch, to: engine.mainMixerNode, format: track.file.processingFormat)
    track.isAttached = true
    applyMixing(for: track, soloActive: isSoloActive())
    applyPitchSpeed(for: track)
  }

  /// Detaches a track's nodes from the engine graph.
  private func detachTrack(_ track: AudioTrack) {
    if !track.isAttached {
      return
    }
    engine.detach(track.playerNode)
    engine.detach(track.timePitch)
    track.isAttached = false
  }

  /// Detaches all tracks from the engine.
  private func detachAllTracks() {
    for track in tracks.values {
      detachTrack(track)
    }
  }

  /// Stops all player nodes.
  private func stopAllPlayers() {
    for track in tracks.values {
      track.playerNode.stop()
    }
  }

  private func emitError(code: String, message: String, details: [String: Any]? = nil) {
    onError?(code, message, details)
  }

  /// Starts the AVAudioEngine if it is not already running.
  @discardableResult
  private func startEngineIfNeeded() -> Bool {
    if !isInitialized {
      lastEngineError = "audio engine not initialized"
      emitError(
        code: "ENGINE_NOT_INITIALIZED",
        message: lastEngineError ?? "Audio engine not initialized"
      )
      return false
    }
    if !engine.isRunning {
      do {
        try engine.start()
        lastEngineError = nil
      } catch {
        lastEngineError = error.localizedDescription
        let session = AVAudioSession.sharedInstance()
        emitError(
          code: "ENGINE_START_FAILED",
          message: "Failed to start audio engine. \(error.localizedDescription)",
          details: [
            "nativeError": error.localizedDescription,
            "category": session.category.rawValue,
            "sampleRate": session.sampleRate,
            "inputAvailable": session.isInputAvailable,
            "inputChannels": session.inputNumberOfChannels,
            "route": describeRoute(session),
            "recordingMixerConnected": recordingMixerConnected
          ]
        )
        return false
      }
    }
    return engine.isRunning
  }

  /// Stops the AVAudioEngine if running.
  private func stopEngineIfRunning() {
    if engine.isRunning {
      engine.stop()
    }
  }

  /// Applies volume/pan/solo rules to every track.
  private func applyMixingForAllTracks() {
    let soloActive = isSoloActive()
    for track in tracks.values {
      applyMixing(for: track, soloActive: soloActive)
    }
  }

  /// Applies volume/pan to a single track.
  private func applyMixing(for track: AudioTrack, soloActive: Bool) {
    let shouldMute = track.muted || (soloActive && !track.solo)
    let volume = shouldMute ? 0.0 : track.volume
    track.playerNode.volume = Float(volume)
    track.playerNode.pan = Float(track.pan)
  }

  /// Applies pitch/speed updates to all tracks.
  private func applyPitchSpeedForAllTracks() {
    for track in tracks.values {
      applyPitchSpeed(for: track)
    }
  }

  /// Combines per-track and global pitch/speed and applies to the unit.
  private func applyPitchSpeed(for track: AudioTrack) {
    let combinedPitch = track.pitch + globalPitch
    let combinedSpeed = track.speed * globalSpeed
    track.timePitch.pitch = Float(combinedPitch * 100.0)
    track.timePitch.rate = Float(combinedSpeed)
  }

  /// Returns true when any track is soloed.
  private func isSoloActive() -> Bool {
    return tracks.values.contains { $0.solo }
  }

  /// Internal play logic (expects to run on the queue).
  private func playInternal() {
    guard !tracks.isEmpty else {
      emitError(
        code: "NO_TRACKS_LOADED",
        message: "Cannot start playback without loaded tracks."
      )
      return
    }
    ensureInitializedIfNeeded()
    if isPlayingFlag {
      return
    }
    guard startEngineIfNeeded() else { return }
    schedulePlayback(at: currentPositionMs)
    isPlayingFlag = true
    updateNowPlayingInfoInternal()
    emitPlaybackState(.playing)
  }

  /// Internal pause logic (expects to run on the queue).
  private func pauseInternal() {
    guard isPlayingFlag else { return }
    currentPositionMs = currentPlaybackPositionMs()
    isPlayingFlag = false
    stopAllPlayers()
    stopEngineIfRunning()
    updateNowPlayingInfoInternal()
    emitPlaybackState(.paused, positionMs: currentPositionMs)
  }

  /// Internal stop logic (expects to run on the queue).
  private func stopInternal() {
    let shouldEmit = isPlayingFlag || currentPositionMs != 0.0
    isPlayingFlag = false
    currentPositionMs = 0.0
    playbackStartHostTime = nil
    playbackStartPositionMs = 0.0
    playbackToken = UUID()
    stopAllPlayers()
    stopEngineIfRunning()
    updateNowPlayingInfoInternal()
    if shouldEmit {
      emitPlaybackState(.stopped, positionMs: currentPositionMs)
    }
  }

  /// Internal seek logic (expects to run on the queue).
  private func seekInternal(positionMs: Double) {
    currentPositionMs = max(0.0, positionMs)
    if isPlayingFlag {
      guard startEngineIfNeeded() else { return }
      schedulePlayback(at: currentPositionMs)
    }
    updateNowPlayingInfoInternal()
  }

  /// Updates Now Playing info based on stored metadata and playback state.
  private func updateNowPlayingInfoInternal() {
    guard backgroundPlaybackEnabled else { return }

    var info: [String: Any] = [:]
    if let title = nowPlayingMetadata["title"] as? String {
      info[MPMediaItemPropertyTitle] = title
    }
    if let artist = nowPlayingMetadata["artist"] as? String {
      info[MPMediaItemPropertyArtist] = artist
    }
    if let album = nowPlayingMetadata["album"] as? String {
      info[MPMediaItemPropertyAlbumTitle] = album
    }
    if let artwork = resolveArtwork(metadata: nowPlayingMetadata) {
      info[MPMediaItemPropertyArtwork] = artwork
    }

    let elapsedSeconds = (isPlayingFlag ? currentPlaybackPositionMs() : currentPositionMs) / 1000.0
    info[MPMediaItemPropertyPlaybackDuration] = durationMs / 1000.0
    info[MPNowPlayingInfoPropertyElapsedPlaybackTime] = elapsedSeconds
    info[MPNowPlayingInfoPropertyPlaybackRate] = isPlayingFlag ? 1.0 : 0.0

    MPNowPlayingInfoCenter.default().nowPlayingInfo = info
    if #available(iOS 13.0, *) {
      if isPlayingFlag {
        MPNowPlayingInfoCenter.default().playbackState = .playing
      } else if currentPositionMs > 0 {
        MPNowPlayingInfoCenter.default().playbackState = .paused
      } else {
        MPNowPlayingInfoCenter.default().playbackState = .stopped
      }
    }
    updateRemoteCommandStates()
  }

  private func emitPlaybackState(_ state: PlaybackState, positionMs: Double? = nil) {
    let resolvedPosition = positionMs ?? (isPlayingFlag ? currentPlaybackPositionMs() : currentPositionMs)
    onPlaybackStateChange?(state.rawValue, resolvedPosition, durationMs)
  }

  /// Registers basic play/pause remote command handlers.
  private func configureRemoteCommandsIfNeeded() {
    guard playCommandTarget == nil else { return }
    let commandCenter = MPRemoteCommandCenter.shared()

    playCommandTarget = commandCenter.playCommand.addTarget { [weak self] _ in
      self?.queue.async { self?.playInternal() }
      return .success
    }
    pauseCommandTarget = commandCenter.pauseCommand.addTarget { [weak self] _ in
      self?.queue.async { self?.pauseInternal() }
      return .success
    }
    toggleCommandTarget = commandCenter.togglePlayPauseCommand.addTarget { [weak self] _ in
      self?.queue.async {
        guard let self = self else { return }
        if self.isPlayingFlag {
          self.pauseInternal()
        } else {
          self.playInternal()
        }
      }
      return .success
    }

    updateRemoteCommandStates()
  }

  /// Removes remote command handlers.
  private func removeRemoteCommands() {
    let commandCenter = MPRemoteCommandCenter.shared()
    if let target = playCommandTarget {
      commandCenter.playCommand.removeTarget(target)
    }
    if let target = pauseCommandTarget {
      commandCenter.pauseCommand.removeTarget(target)
    }
    if let target = toggleCommandTarget {
      commandCenter.togglePlayPauseCommand.removeTarget(target)
    }
    playCommandTarget = nil
    pauseCommandTarget = nil
    toggleCommandTarget = nil
  }

  private func updateRemoteCommandStates() {
    let commandCenter = MPRemoteCommandCenter.shared()
    let hasTracks = !tracks.isEmpty
    commandCenter.playCommand.isEnabled = backgroundPlaybackEnabled && hasTracks && !isPlayingFlag
    commandCenter.pauseCommand.isEnabled = backgroundPlaybackEnabled && hasTracks && isPlayingFlag
    commandCenter.togglePlayPauseCommand.isEnabled = backgroundPlaybackEnabled && hasTracks
  }

  private func setRemoteControlEventsEnabled(_ enabled: Bool) {
    let handler = {
      if enabled {
        UIApplication.shared.beginReceivingRemoteControlEvents()
      } else {
        UIApplication.shared.endReceivingRemoteControlEvents()
      }
    }
    if Thread.isMainThread {
      handler()
    } else {
      DispatchQueue.main.async(execute: handler)
    }
  }

  /// Resolves artwork from a local file path or file URL.
  private func resolveArtwork(metadata: [String: Any]) -> MPMediaItemArtwork? {
    guard let artworkValue = metadata["artwork"] as? String else {
      return nil
    }

    let url: URL?
    if artworkValue.hasPrefix("file://") {
      url = URL(string: artworkValue)
    } else if artworkValue.hasPrefix("/") {
      url = URL(fileURLWithPath: artworkValue)
    } else {
      url = nil
    }

    guard let imageURL = url,
          let data = try? Data(contentsOf: imageURL),
          let image = UIImage(data: data) else {
      return nil
    }

    return MPMediaItemArtwork(boundsSize: image.size) { _ in image }
  }
}
