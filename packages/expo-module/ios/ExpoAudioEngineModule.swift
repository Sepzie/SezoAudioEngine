import ExpoModulesCore

struct AudioEngineError: CodedError, CustomNSError {
  let code: String
  let description: String
  let details: [String: Any]?

  init(_ code: String, _ description: String, details: [String: Any]? = nil) {
    self.code = code
    self.description = description
    self.details = details
  }

  static var errorDomain: String {
    return "ExpoAudioEngine"
  }

  var errorCode: Int {
    return 0
  }

  var errorUserInfo: [String: Any] {
    var info: [String: Any] = [
      NSLocalizedDescriptionKey: description,
      "code": code
    ]
    if let details = details {
      info["details"] = details
    }
    return info
  }
}

public class ExpoAudioEngineModule: Module {
  private let engine = NativeAudioEngine()

  public func definition() -> ModuleDefinition {
    Name("ExpoAudioEngineModule")
    OnCreate {
      engine.onPlaybackStateChange = { [weak self] state, positionMs, durationMs in
        DispatchQueue.main.async {
          self?.sendEvent(
            "playbackStateChange",
            [
              "state": state,
              "positionMs": positionMs,
              "durationMs": durationMs
            ]
          )
        }
      }
      engine.onPlaybackComplete = { [weak self] positionMs, durationMs in
        DispatchQueue.main.async {
          self?.sendEvent(
            "playbackComplete",
            [
              "positionMs": positionMs,
              "durationMs": durationMs
            ]
          )
        }
      }
      engine.onError = { [weak self] code, message, details in
        DispatchQueue.main.async {
          var payload: [String: Any] = [
            "code": code,
            "message": message
          ]
          if let details = details {
            payload["details"] = details
          }
          self?.sendEvent("error", payload)
        }
      }
    }
    OnDestroy {
      engine.onPlaybackStateChange = nil
      engine.onPlaybackComplete = nil
      engine.onError = nil
    }

    AsyncFunction("initialize") { (config: [String: Any]) in
      engine.initialize(config: config)
    }
    AsyncFunction("release") {
      engine.releaseResources()
    }
    AsyncFunction("loadTracks") { (tracks: [[String: Any]]) throws in
      if let failure = engine.loadTracks(tracks) {
        let failedCount = failure["failedCount"] as? Int ?? 0
        let message = failedCount > 0
          ? "Failed to load \(failedCount) track(s)."
          : "Failed to load tracks."
        throw AudioEngineError("TRACK_LOAD_FAILED", message, details: failure)
      }
    }
    AsyncFunction("unloadTrack") { (trackId: String) in
      engine.unloadTrack(trackId)
    }
    AsyncFunction("unloadAllTracks") {
      engine.unloadAllTracks()
    }
    Function("getLoadedTracks") {
      return engine.getLoadedTracks()
    }

    Function("play") {
      engine.play()
    }
    Function("pause") {
      engine.pause()
    }
    Function("stop") {
      engine.stop()
    }
    Function("seek") { (positionMs: Double) in
      engine.seek(positionMs: positionMs)
    }
    Function("isPlaying") {
      return engine.isPlaying()
    }
    Function("getCurrentPosition") {
      return engine.getCurrentPosition()
    }
    Function("getDuration") {
      return engine.getDuration()
    }

    Function("setTrackVolume") { (trackId: String, volume: Double) in
      engine.setTrackVolume(trackId: trackId, volume: volume)
    }
    Function("setTrackMuted") { (trackId: String, muted: Bool) in
      engine.setTrackMuted(trackId: trackId, muted: muted)
    }
    Function("setTrackSolo") { (trackId: String, solo: Bool) in
      engine.setTrackSolo(trackId: trackId, solo: solo)
    }
    Function("setTrackPan") { (trackId: String, pan: Double) in
      engine.setTrackPan(trackId: trackId, pan: pan)
    }
    Function("setTrackPitch") { (trackId: String, semitones: Double) in
      engine.setTrackPitch(trackId: trackId, semitones: semitones)
    }
    Function("getTrackPitch") { (trackId: String) in
      return engine.getTrackPitch(trackId: trackId)
    }
    Function("setTrackSpeed") { (trackId: String, rate: Double) in
      engine.setTrackSpeed(trackId: trackId, rate: rate)
    }
    Function("getTrackSpeed") { (trackId: String) in
      return engine.getTrackSpeed(trackId: trackId)
    }

    Function("setMasterVolume") { (volume: Double) in
      engine.setMasterVolume(volume)
    }
    Function("getMasterVolume") {
      return engine.getMasterVolume()
    }

    Function("setPitch") { (semitones: Double) in
      engine.setPitch(semitones)
    }
    Function("getPitch") {
      return engine.getPitch()
    }
    Function("setSpeed") { (rate: Double) in
      engine.setSpeed(rate)
    }
    Function("getSpeed") {
      return engine.getSpeed()
    }
    Function("setTempoAndPitch") { (tempo: Double, pitch: Double) in
      engine.setTempoAndPitch(tempo: tempo, pitch: pitch)
    }

    AsyncFunction("startRecording") { (config: [String: Any]?) throws in
      let didStart = engine.startRecording(config: config)
      if !didStart {
        let detail = engine.getLastRecordingError() ?? "audio input format is invalid or the audio session is not ready"
        throw NSError(
          domain: "ExpoAudioEngine",
          code: 1,
          userInfo: [
            NSLocalizedDescriptionKey: "Failed to start recording. \(detail)."
          ]
        )
      }
    }
    AsyncFunction("stopRecording") {
      return engine.stopRecording()
    }
    Function("isRecording") {
      return engine.isRecording()
    }
    Function("setRecordingVolume") { (volume: Double) in
      engine.setRecordingVolume(volume)
    }

    AsyncFunction("extractTrack") { (trackId: String, config: [String: Any]?) throws in
      return try engine.extractTrack(trackId: trackId, config: config)
    }

    AsyncFunction("extractAllTracks") { (config: [String: Any]?) throws in
      return try engine.extractAllTracks(config: config)
    }

    Function("cancelExtraction") { (jobId: Double?) in
      return engine.cancelExtraction(jobId: jobId)
    }

    Function("getInputLevel") {
      return engine.getInputLevel()
    }
    Function("getOutputLevel") {
      return engine.getOutputLevel()
    }
    Function("getTrackLevel") { (trackId: String) in
      return engine.getTrackLevel(trackId: trackId)
    }

    AsyncFunction("enableBackgroundPlayback") { (metadata: [String: Any]) in
      engine.enableBackgroundPlayback(metadata: metadata)
    }
    Function("updateNowPlayingInfo") { (metadata: [String: Any]) in
      engine.updateNowPlayingInfo(metadata: metadata)
    }
    AsyncFunction("disableBackgroundPlayback") {
      engine.disableBackgroundPlayback()
    }

    Events(
      "playbackStateChange",
      "positionUpdate",
      "playbackComplete",
      "trackLoaded",
      "trackUnloaded",
      "recordingStarted",
      "recordingStopped",
      "extractionProgress",
      "extractionComplete",
      "error",
      "debugLog"
    )
  }
}
