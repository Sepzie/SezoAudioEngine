package expo.modules.audioengine

import expo.modules.kotlin.modules.Module
import expo.modules.kotlin.modules.ModuleDefinition

class ExpoAudioEngineModule : Module() {
  override fun definition() = ModuleDefinition {
    Name("ExpoAudioEngineModule")

    AsyncFunction("initialize") { _config: Map<String, Any?> ->
      // TODO: Bridge to native engine.
    }

    AsyncFunction("release") {
      // TODO: Release native resources.
    }

    AsyncFunction("loadTracks") { _tracks: List<Map<String, Any?>> ->
      // TODO: Load tracks.
    }

    AsyncFunction("unloadTrack") { _trackId: String ->
      // TODO: Unload track.
    }

    AsyncFunction("unloadAllTracks") {
      // TODO: Unload all.
    }

    Function("getLoadedTracks") {
      emptyList<Map<String, Any?>>()
    }

    Function("play") { }
    Function("pause") { }
    Function("stop") { }
    Function("seek") { _positionMs: Double -> }
    Function("isPlaying") { false }
    Function("getCurrentPosition") { 0.0 }
    Function("getDuration") { 0.0 }

    Function("setTrackVolume") { _trackId: String, _volume: Double -> }
    Function("setTrackMuted") { _trackId: String, _muted: Boolean -> }
    Function("setTrackSolo") { _trackId: String, _solo: Boolean -> }
    Function("setTrackPan") { _trackId: String, _pan: Double -> }

    Function("setMasterVolume") { _volume: Double -> }
    Function("getMasterVolume") { 1.0 }

    Function("setPitch") { _semitones: Double -> }
    Function("getPitch") { 0.0 }
    Function("setSpeed") { _rate: Double -> }
    Function("getSpeed") { 1.0 }
    Function("setTempoAndPitch") { _tempo: Double, _pitch: Double -> }

    AsyncFunction("startRecording") { _config: Map<String, Any?>? -> }
    AsyncFunction("stopRecording") {
      mapOf(
        "uri" to "",
        "duration" to 0,
        "sampleRate" to 44100,
        "channels" to 1,
        "format" to "aac",
        "fileSize" to 0
      )
    }
    Function("isRecording") { false }
    Function("setRecordingVolume") { _volume: Double -> }

    AsyncFunction("extractTrack") { _trackId: String, _config: Map<String, Any?>? ->
      mapOf(
        "trackId" to _trackId,
        "uri" to "",
        "duration" to 0,
        "format" to "aac",
        "fileSize" to 0
      )
    }

    AsyncFunction("extractAllTracks") { _config: Map<String, Any?>? ->
      emptyList<Map<String, Any?>>()
    }

    Function("getInputLevel") { 0.0 }
    Function("getOutputLevel") { 0.0 }
    Function("getTrackLevel") { _trackId: String -> 0.0 }

    AsyncFunction("enableBackgroundPlayback") { _metadata: Map<String, Any?> -> }
    Function("updateNowPlayingInfo") { _metadata: Map<String, Any?> -> }
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
