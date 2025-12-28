package expo.modules.audioengine

import android.util.Log
import com.sezo.audioengine.AudioEngine
import expo.modules.kotlin.modules.Module
import expo.modules.kotlin.modules.ModuleDefinition

class ExpoAudioEngineModule : Module() {
  private var audioEngine: AudioEngine? = null
  private val loadedTrackIds = mutableSetOf<String>()

  override fun definition() = ModuleDefinition {
    Name("ExpoAudioEngineModule")

    AsyncFunction("initialize") { config: Map<String, Any?> ->
      Log.d(TAG, "Initialize called with config: $config")

      val sampleRate = (config["sampleRate"] as? Number)?.toInt() ?: 44100
      val maxTracks = (config["maxTracks"] as? Number)?.toInt() ?: 8

      audioEngine = AudioEngine()
      val success = audioEngine?.initialize(sampleRate, maxTracks) ?: false

      if (!success) {
        throw Exception("Failed to initialize audio engine")
      }

      Log.d(TAG, "Audio engine initialized successfully")
    }

    AsyncFunction("release") {
      Log.d(TAG, "Release called")
      audioEngine?.release()
      audioEngine?.destroy()
      audioEngine = null
      loadedTrackIds.clear()
      Log.d(TAG, "Audio engine released")
    }

    AsyncFunction("loadTracks") { tracks: List<Map<String, Any?>> ->
      val engine = audioEngine ?: throw Exception("Engine not initialized")

      tracks.forEach { track ->
        val id = track["id"] as? String ?: throw Exception("Missing track id")
        val uri = track["uri"] as? String ?: throw Exception("Missing track uri")

        Log.d(TAG, "Loading track: id=$id, uri=$uri")
        val filePath = convertUriToPath(uri)

        if (!engine.loadTrack(id, filePath)) {
          throw Exception("Failed to load track: $id from $filePath")
        }

        loadedTrackIds.add(id)
        Log.d(TAG, "Track loaded successfully: $id")
      }
    }

    AsyncFunction("unloadTrack") { trackId: String ->
      val engine = audioEngine ?: throw Exception("Engine not initialized")

      if (!engine.unloadTrack(trackId)) {
        throw Exception("Failed to unload track: $trackId")
      }

      loadedTrackIds.remove(trackId)
      Log.d(TAG, "Track unloaded: $trackId")
    }

    AsyncFunction("unloadAllTracks") {
      val engine = audioEngine ?: throw Exception("Engine not initialized")
      engine.unloadAllTracks()
      loadedTrackIds.clear()
      Log.d(TAG, "All tracks unloaded")
    }

    Function("getLoadedTracks") {
      loadedTrackIds.map { mapOf("id" to it) }
    }

    Function("play") {
      val engine = audioEngine ?: throw Exception("Engine not initialized")
      engine.play()
      Log.d(TAG, "Playback started")
    }

    Function("pause") {
      val engine = audioEngine ?: throw Exception("Engine not initialized")
      engine.pause()
      Log.d(TAG, "Playback paused")
    }

    Function("stop") {
      val engine = audioEngine ?: throw Exception("Engine not initialized")
      engine.stop()
      Log.d(TAG, "Playback stopped")
    }

    Function("seek") { positionMs: Double ->
      val engine = audioEngine ?: throw Exception("Engine not initialized")
      engine.seek(positionMs)
      Log.d(TAG, "Seeked to position: $positionMs ms")
    }

    Function("isPlaying") {
      audioEngine?.isPlaying() ?: false
    }

    Function("getCurrentPosition") {
      audioEngine?.getCurrentPosition() ?: 0.0
    }

    Function("getDuration") {
      audioEngine?.getDuration() ?: 0.0
    }

    Function("setTrackVolume") { trackId: String, volume: Double ->
      val engine = audioEngine ?: throw Exception("Engine not initialized")
      engine.setTrackVolume(trackId, volume.toFloat())
    }

    Function("setTrackMuted") { trackId: String, muted: Boolean ->
      val engine = audioEngine ?: throw Exception("Engine not initialized")
      engine.setTrackMuted(trackId, muted)
    }

    Function("setTrackSolo") { trackId: String, solo: Boolean ->
      val engine = audioEngine ?: throw Exception("Engine not initialized")
      engine.setTrackSolo(trackId, solo)
    }

    Function("setTrackPan") { trackId: String, pan: Double ->
      val engine = audioEngine ?: throw Exception("Engine not initialized")
      engine.setTrackPan(trackId, pan.toFloat())
    }

    Function("setMasterVolume") { volume: Double ->
      val engine = audioEngine ?: throw Exception("Engine not initialized")
      engine.setMasterVolume(volume.toFloat())
    }

    Function("getMasterVolume") {
      audioEngine?.getMasterVolume()?.toDouble() ?: 1.0
    }

    Function("setPitch") { semitones: Double ->
      val engine = audioEngine ?: throw Exception("Engine not initialized")
      engine.setPitch(semitones.toFloat())
    }

    Function("getPitch") {
      audioEngine?.getPitch()?.toDouble() ?: 0.0
    }

    Function("setSpeed") { rate: Double ->
      val engine = audioEngine ?: throw Exception("Engine not initialized")
      engine.setSpeed(rate.toFloat())
    }

    Function("getSpeed") {
      audioEngine?.getSpeed()?.toDouble() ?: 1.0
    }

    Function("setTempoAndPitch") { tempo: Double, pitch: Double ->
      val engine = audioEngine ?: throw Exception("Engine not initialized")
      engine.setSpeed(tempo.toFloat())
      engine.setPitch(pitch.toFloat())
    }

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

  private fun convertUriToPath(uri: String): String {
    return when {
      uri.startsWith("file://") -> {
        // Remove file:// prefix
        uri.substring(7)
      }
      uri.startsWith("/") -> {
        // Already an absolute path
        uri
      }
      uri.startsWith("content://") -> {
        // TODO: Handle content:// URIs with ContentResolver
        // For now, throw an error - this will be implemented when needed
        throw Exception("content:// URIs not yet supported. Use file:// or absolute paths.")
      }
      uri.startsWith("asset://") -> {
        // TODO: Handle asset:// URIs by copying to temp file
        // For now, throw an error - this will be implemented when needed
        throw Exception("asset:// URIs not yet supported. Use file:// or absolute paths.")
      }
      else -> {
        throw Exception("Unsupported URI scheme: $uri. Use file:// or absolute paths.")
      }
    }
  }

  companion object {
    private const val TAG = "ExpoAudioEngine"
  }
}
