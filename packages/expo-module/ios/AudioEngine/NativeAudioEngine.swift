import AVFoundation

final class NativeAudioEngine {
  private let queue = DispatchQueue(label: "sezo.audioengine.state")
  private let engine = AVAudioEngine()
  private let sessionManager = AudioSessionManager()
  private var tracks: [String: AudioTrack] = [:]
  private var masterVolume: Double = 1.0
  private var globalPitch: Double = 0.0
  private var globalSpeed: Double = 1.0
  private var isPlayingFlag = false
  private var isRecordingFlag = false
  private var currentPositionMs: Double = 0.0
  private var durationMs: Double = 0.0
  private var recordingVolume: Double = 1.0
  private var isInitialized = false
  private var playbackStartHostTime: UInt64?
  private var playbackStartPositionMs: Double = 0.0
  private var activePlaybackCount = 0
  private var playbackToken = UUID()

  func initialize(config: [String: Any]) {
    let parsedConfig = AudioEngineConfig(dictionary: config)
    queue.sync {
      sessionManager.configure(with: parsedConfig)
      engine.mainMixerNode.outputVolume = Float(masterVolume)
      engine.prepare()
      isInitialized = true
    }
  }

  func releaseResources() {
    queue.sync {
      stopEngineIfRunning()
      stopAllPlayers()
      detachAllTracks()
      tracks.removeAll()
      isPlayingFlag = false
      isRecordingFlag = false
      currentPositionMs = 0.0
      durationMs = 0.0
      playbackStartHostTime = nil
      playbackStartPositionMs = 0.0
      activePlaybackCount = 0
      playbackToken = UUID()
      isInitialized = false
      sessionManager.deactivate()
    }
  }

  func loadTracks(_ trackInputs: [[String: Any]]) {
    queue.sync {
      ensureInitializedIfNeeded()
      stopEngineIfRunning()
      stopAllPlayers()
      for input in trackInputs {
        guard let track = AudioTrack(input: input) else {
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
    }
  }

  func unloadTrack(_ trackId: String) {
    queue.sync {
      if let track = tracks.removeValue(forKey: trackId) {
        detachTrack(track)
      }
      applyMixingForAllTracks()
      recalculateDuration()
    }
  }

  func unloadAllTracks() {
    queue.sync {
      stopAllPlayers()
      detachAllTracks()
      tracks.removeAll()
      recalculateDuration()
    }
  }

  func getLoadedTracks() -> [[String: Any]] {
    return queue.sync {
      return tracks.values.map { $0.asDictionary() }
    }
  }

  func play() {
    queue.sync {
      guard !tracks.isEmpty else { return }
      ensureInitializedIfNeeded()
      if isPlayingFlag {
        return
      }
      guard startEngineIfNeeded() else { return }
      schedulePlayback(at: currentPositionMs)
      isPlayingFlag = true
    }
  }

  func pause() {
    queue.sync {
      guard isPlayingFlag else { return }
      currentPositionMs = currentPlaybackPositionMs()
      isPlayingFlag = false
      stopAllPlayers()
      stopEngineIfRunning()
    }
  }

  func stop() {
    queue.sync {
      isPlayingFlag = false
      currentPositionMs = 0.0
      playbackStartHostTime = nil
      playbackStartPositionMs = 0.0
      playbackToken = UUID()
      stopAllPlayers()
      stopEngineIfRunning()
    }
  }

  func seek(positionMs: Double) {
    queue.sync {
      currentPositionMs = max(0.0, positionMs)
      if isPlayingFlag {
        guard startEngineIfNeeded() else { return }
        schedulePlayback(at: currentPositionMs)
      }
    }
  }

  func isPlaying() -> Bool {
    return queue.sync { isPlayingFlag }
  }

  func getCurrentPosition() -> Double {
    return queue.sync {
      if isPlayingFlag {
        return currentPlaybackPositionMs()
      }
      return currentPositionMs
    }
  }

  func getDuration() -> Double {
    return queue.sync { durationMs }
  }

  func setTrackVolume(trackId: String, volume: Double) {
    queue.sync {
      guard let track = tracks[trackId] else { return }
      track.volume = volume
      applyMixingForAllTracks()
    }
  }

  func setTrackMuted(trackId: String, muted: Bool) {
    queue.sync {
      guard let track = tracks[trackId] else { return }
      track.muted = muted
      applyMixingForAllTracks()
    }
  }

  func setTrackSolo(trackId: String, solo: Bool) {
    queue.sync {
      guard let track = tracks[trackId] else { return }
      track.solo = solo
      applyMixingForAllTracks()
    }
  }

  func setTrackPan(trackId: String, pan: Double) {
    queue.sync {
      guard let track = tracks[trackId] else { return }
      track.pan = pan
      applyMixingForAllTracks()
    }
  }

  func setTrackPitch(trackId: String, semitones: Double) {
    queue.sync {
      guard let track = tracks[trackId] else { return }
      track.pitch = semitones
      applyPitchSpeed(for: track)
    }
  }

  func getTrackPitch(trackId: String) -> Double {
    return queue.sync {
      return tracks[trackId]?.pitch ?? 0.0
    }
  }

  func setTrackSpeed(trackId: String, rate: Double) {
    queue.sync {
      guard let track = tracks[trackId] else { return }
      track.speed = rate
      applyPitchSpeed(for: track)
    }
  }

  func getTrackSpeed(trackId: String) -> Double {
    return queue.sync {
      return tracks[trackId]?.speed ?? 1.0
    }
  }

  func setMasterVolume(_ volume: Double) {
    queue.sync {
      masterVolume = volume
      engine.mainMixerNode.outputVolume = Float(volume)
    }
  }

  func getMasterVolume() -> Double {
    return queue.sync { masterVolume }
  }

  func setPitch(_ semitones: Double) {
    queue.sync {
      globalPitch = semitones
      applyPitchSpeedForAllTracks()
    }
  }

  func getPitch() -> Double {
    return queue.sync { globalPitch }
  }

  func setSpeed(_ rate: Double) {
    queue.sync {
      globalSpeed = rate
      applyPitchSpeedForAllTracks()
    }
  }

  func getSpeed() -> Double {
    return queue.sync { globalSpeed }
  }

  func setTempoAndPitch(tempo: Double, pitch: Double) {
    queue.sync {
      globalSpeed = tempo
      globalPitch = pitch
      applyPitchSpeedForAllTracks()
    }
  }

  func startRecording(config: [String: Any]?) {
    queue.sync {
      _ = config
      isRecordingFlag = true
    }
  }

  func stopRecording() -> [String: Any] {
    return queue.sync {
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
  }

  func isRecording() -> Bool {
    return queue.sync { isRecordingFlag }
  }

  func setRecordingVolume(_ volume: Double) {
    queue.sync {
      recordingVolume = volume
    }
  }

  func extractTrack(trackId: String, config: [String: Any]?) -> [String: Any] {
    return queue.sync {
      _ = config
      return [
        "trackId": trackId,
        "uri": "",
        "duration": 0,
        "format": "aac",
        "fileSize": 0
      ]
    }
  }

  func extractAllTracks(config: [String: Any]?) -> [[String: Any]] {
    return queue.sync {
      _ = config
      return []
    }
  }

  func cancelExtraction(jobId: Double?) -> Bool {
    _ = jobId
    return false
  }

  func getInputLevel() -> Double {
    return 0.0
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
      _ = metadata
    }
  }

  func updateNowPlayingInfo(metadata: [String: Any]) {
    queue.sync {
      _ = metadata
    }
  }

  func disableBackgroundPlayback() {
    queue.sync { }
  }

  private func recalculateDuration() {
    durationMs = tracks.values.reduce(0.0) { current, track in
      let endMs = track.startTimeMs + track.durationMs
      return max(current, endMs)
    }
  }

  private func ensureInitializedIfNeeded() {
    if isInitialized {
      return
    }
    let config = AudioEngineConfig(dictionary: [:])
    sessionManager.configure(with: config)
    engine.mainMixerNode.outputVolume = Float(masterVolume)
    engine.prepare()
    isInitialized = true
  }

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
  }

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

  private func detachTrack(_ track: AudioTrack) {
    if !track.isAttached {
      return
    }
    engine.detach(track.playerNode)
    engine.detach(track.timePitch)
    track.isAttached = false
  }

  private func detachAllTracks() {
    for track in tracks.values {
      detachTrack(track)
    }
  }

  private func stopAllPlayers() {
    for track in tracks.values {
      track.playerNode.stop()
    }
  }

  @discardableResult
  private func startEngineIfNeeded() -> Bool {
    if !isInitialized {
      return false
    }
    if !engine.isRunning {
      do {
        try engine.start()
      } catch {
        return false
      }
    }
    return engine.isRunning
  }

  private func stopEngineIfRunning() {
    if engine.isRunning {
      engine.stop()
    }
  }

  private func applyMixingForAllTracks() {
    let soloActive = isSoloActive()
    for track in tracks.values {
      applyMixing(for: track, soloActive: soloActive)
    }
  }

  private func applyMixing(for track: AudioTrack, soloActive: Bool) {
    let shouldMute = track.muted || (soloActive && !track.solo)
    let volume = shouldMute ? 0.0 : track.volume
    track.playerNode.volume = Float(volume)
    track.playerNode.pan = Float(track.pan)
  }

  private func applyPitchSpeedForAllTracks() {
    for track in tracks.values {
      applyPitchSpeed(for: track)
    }
  }

  private func applyPitchSpeed(for track: AudioTrack) {
    let combinedPitch = track.pitch + globalPitch
    let combinedSpeed = track.speed * globalSpeed
    track.timePitch.pitch = Float(combinedPitch * 100.0)
    track.timePitch.rate = Float(combinedSpeed)
  }

  private func isSoloActive() -> Bool {
    return tracks.values.contains { $0.solo }
  }
}
