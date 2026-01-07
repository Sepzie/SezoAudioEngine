import AVFoundation

final class NativeAudioEngine {
  private struct TrackState {
    let id: String
    let uri: String
    var volume: Double
    var pan: Double
    var muted: Bool
    var solo: Bool
    var pitch: Double
    var speed: Double
    var startTimeMs: Double
    var durationMs: Double

    func asDictionary() -> [String: Any] {
      return [
        "id": id,
        "uri": uri,
        "volume": volume,
        "pan": pan,
        "muted": muted,
        "startTimeMs": startTimeMs
      ]
    }
  }

  private let queue = DispatchQueue(label: "sezo.audioengine.state")
  private var tracks: [String: TrackState] = [:]
  private var masterVolume: Double = 1.0
  private var pitch: Double = 0.0
  private var speed: Double = 1.0
  private var isPlayingFlag = false
  private var isRecordingFlag = false
  private var currentPositionMs: Double = 0.0
  private var durationMs: Double = 0.0
  private var recordingVolume: Double = 1.0

  func initialize(config: [String: Any]) {
    queue.sync {
      _ = config
    }
  }

  func releaseResources() {
    queue.sync {
      tracks.removeAll()
      isPlayingFlag = false
      isRecordingFlag = false
      currentPositionMs = 0.0
      durationMs = 0.0
    }
  }

  func loadTracks(_ trackInputs: [[String: Any]]) {
    queue.sync {
      for input in trackInputs {
        guard let id = input["id"] as? String,
              let uri = input["uri"] as? String else {
          continue
        }
        let volume = input["volume"] as? Double ?? 1.0
        let pan = input["pan"] as? Double ?? 0.0
        let muted = input["muted"] as? Bool ?? false
        let solo = input["solo"] as? Bool ?? false
        let startTimeMs = input["startTimeMs"] as? Double ?? 0.0
        let durationMs = resolveDurationMs(uri: uri)
        let track = TrackState(
          id: id,
          uri: uri,
          volume: volume,
          pan: pan,
          muted: muted,
          solo: solo,
          pitch: 0.0,
          speed: 1.0,
          startTimeMs: startTimeMs,
          durationMs: durationMs
        )
        tracks[id] = track
      }
      recalculateDuration()
    }
  }

  func unloadTrack(_ trackId: String) {
    queue.sync {
      tracks.removeValue(forKey: trackId)
      recalculateDuration()
    }
  }

  func unloadAllTracks() {
    queue.sync {
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
      isPlayingFlag = true
    }
  }

  func pause() {
    queue.sync {
      isPlayingFlag = false
    }
  }

  func stop() {
    queue.sync {
      isPlayingFlag = false
      currentPositionMs = 0.0
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
      guard var track = tracks[trackId] else { return }
      track.volume = volume
      tracks[trackId] = track
    }
  }

  func setTrackMuted(trackId: String, muted: Bool) {
    queue.sync {
      guard var track = tracks[trackId] else { return }
      track.muted = muted
      tracks[trackId] = track
    }
  }

  func setTrackSolo(trackId: String, solo: Bool) {
    queue.sync {
      guard var track = tracks[trackId] else { return }
      track.solo = solo
      tracks[trackId] = track
    }
  }

  func setTrackPan(trackId: String, pan: Double) {
    queue.sync {
      guard var track = tracks[trackId] else { return }
      track.pan = pan
      tracks[trackId] = track
    }
  }

  func setTrackPitch(trackId: String, semitones: Double) {
    queue.sync {
      guard var track = tracks[trackId] else { return }
      track.pitch = semitones
      tracks[trackId] = track
    }
  }

  func getTrackPitch(trackId: String) -> Double {
    return queue.sync {
      return tracks[trackId]?.pitch ?? 0.0
    }
  }

  func setTrackSpeed(trackId: String, rate: Double) {
    queue.sync {
      guard var track = tracks[trackId] else { return }
      track.speed = rate
      tracks[trackId] = track
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
    }
  }

  func getMasterVolume() -> Double {
    return queue.sync { masterVolume }
  }

  func setPitch(_ semitones: Double) {
    queue.sync {
      pitch = semitones
    }
  }

  func getPitch() -> Double {
    return queue.sync { pitch }
  }

  func setSpeed(_ rate: Double) {
    queue.sync {
      speed = rate
    }
  }

  func getSpeed() -> Double {
    return queue.sync { speed }
  }

  func setTempoAndPitch(tempo: Double, pitch: Double) {
    queue.sync {
      self.speed = tempo
      self.pitch = pitch
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

  private func resolveDurationMs(uri: String) -> Double {
    guard let url = resolveURL(uri: uri), url.isFileURL else {
      return 0.0
    }
    do {
      let file = try AVAudioFile(forReading: url)
      let sampleRate = file.processingFormat.sampleRate
      if sampleRate <= 0 {
        return 0.0
      }
      return (Double(file.length) / sampleRate) * 1000.0
    } catch {
      return 0.0
    }
  }

  private func resolveURL(uri: String) -> URL? {
    if uri.hasPrefix("file://") {
      return URL(string: uri)
    }
    if uri.hasPrefix("/") {
      return URL(fileURLWithPath: uri)
    }
    return URL(string: uri)
  }
}
