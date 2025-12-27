import ExpoModulesCore

public class ExpoAudioEngineModule: Module {
  public func definition() -> ModuleDefinition {
    Name("ExpoAudioEngineModule")

    AsyncFunction("initialize") { (_: [String: Any]) in }
    AsyncFunction("release") { }
    AsyncFunction("loadTracks") { (_: [[String: Any]]) in }
    AsyncFunction("unloadTrack") { (_: String) in }
    AsyncFunction("unloadAllTracks") { }
    Function("getLoadedTracks") { return [[String: Any]]() }

    Function("play") { }
    Function("pause") { }
    Function("stop") { }
    Function("seek") { (_: Double) in }
    Function("isPlaying") { return false }
    Function("getCurrentPosition") { return 0.0 }
    Function("getDuration") { return 0.0 }

    Function("setTrackVolume") { (_: String, _: Double) in }
    Function("setTrackMuted") { (_: String, _: Bool) in }
    Function("setTrackSolo") { (_: String, _: Bool) in }
    Function("setTrackPan") { (_: String, _: Double) in }

    Function("setMasterVolume") { (_: Double) in }
    Function("getMasterVolume") { return 1.0 }

    Function("setPitch") { (_: Double) in }
    Function("getPitch") { return 0.0 }
    Function("setSpeed") { (_: Double) in }
    Function("getSpeed") { return 1.0 }
    Function("setTempoAndPitch") { (_: Double, _: Double) in }

    AsyncFunction("startRecording") { (_: [String: Any]?) in }
    AsyncFunction("stopRecording") {
      return [
        "uri": "",
        "duration": 0,
        "sampleRate": 44100,
        "channels": 1,
        "format": "aac",
        "fileSize": 0
      ]
    }
    Function("isRecording") { return false }
    Function("setRecordingVolume") { (_: Double) in }

    AsyncFunction("extractTrack") { (_: String, _: [String: Any]?) in
      return [
        "trackId": "",
        "uri": "",
        "duration": 0,
        "format": "aac",
        "fileSize": 0
      ]
    }

    AsyncFunction("extractAllTracks") { (_: [String: Any]?) in
      return [[String: Any]]()
    }

    Function("getInputLevel") { return 0.0 }
    Function("getOutputLevel") { return 0.0 }
    Function("getTrackLevel") { (_: String) in return 0.0 }

    AsyncFunction("enableBackgroundPlayback") { (_: [String: Any]) in }
    Function("updateNowPlayingInfo") { (_: [String: Any]) in }
    AsyncFunction("disableBackgroundPlayback") { }

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
      "error"
    )
  }
}
