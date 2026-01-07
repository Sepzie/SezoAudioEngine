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
      detachAllTracks()
      tracks.removeAll()
      isPlayingFlag = false
      isRecordingFlag = false
      currentPositionMs = 0.0
      durationMs = 0.0
      isInitialized = false
      sessionManager.deactivate()
    }
  }

  func loadTracks(_ trackInputs: [[String: Any]]) {
    queue.sync {
      stopEngineIfRunning()
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
      startEngineIfNeeded()
      isPlayingFlag = true
    }
  }

  func pause() {
    queue.sync {
      isPlayingFlag = false
      stopEngineIfRunning()
    }
  }

  func stop() {
    queue.sync {
      isPlayingFlag = false
      currentPositionMs = 0.0
      stopAllPlayers()
      stopEngineIfRunning()
    }
  }

  func seek(positionMs: Double) {
    queue.sync {
      currentPositionMs = max(0.0, positionMs)
    }
  }

  func isPlaying() -> Bool {
    return queue.sync { isPlayingFlag }
  }

  func getCurrentPosition() -> Double {
    return queue.sync { currentPositionMs }
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

  private func startEngineIfNeeded() {
    if !isInitialized {
      return
    }
    if !engine.isRunning {
      try? engine.start()
    }
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
