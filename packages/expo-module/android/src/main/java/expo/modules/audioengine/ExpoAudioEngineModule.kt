package expo.modules.audioengine

import android.util.Log
import com.sezo.audioengine.AudioEngine
import expo.modules.kotlin.Promise
import expo.modules.kotlin.modules.Module
import expo.modules.kotlin.modules.ModuleDefinition
import java.util.Collections

class ExpoAudioEngineModule : Module() {
  private var audioEngine: AudioEngine? = null
  private val loadedTrackIds = mutableSetOf<String>()
  private val pendingExtractions = Collections.synchronizedMap(mutableMapOf<Long, PendingExtraction>())
  private val progressLogState = Collections.synchronizedMap(mutableMapOf<Long, Float>())
  private var lastExtractionJobId: Long? = null

  private data class PendingExtraction(
    val promise: Promise,
    val trackId: String?,
    val outputPath: String,
    val format: String,
    val bitrate: Int,
    val operation: String
  )

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
      audioEngine?.let { engine ->
        synchronized(pendingExtractions) {
          pendingExtractions.keys.forEach { engine.cancelExtraction(it) }
          pendingExtractions.clear()
        }
        progressLogState.clear()
        engine.setExtractionProgressListener(null)
        engine.setExtractionCompletionListener(null)
        engine.release()
        engine.destroy()
      }
      audioEngine = null
      loadedTrackIds.clear()
      Log.d(TAG, "Audio engine released")
    }

    AsyncFunction("loadTracks") { tracks: List<Map<String, Any?>> ->
      val engine = audioEngine ?: throw Exception("Engine not initialized")

      tracks.forEach { track ->
        val id = track["id"] as? String ?: throw Exception("Missing track id")
        val uri = track["uri"] as? String ?: throw Exception("Missing track uri")
        val startTimeMs = (track["startTimeMs"] as? Number)?.toDouble() ?: 0.0

        Log.d(TAG, "Loading track: id=$id, uri=$uri")
        val filePath = convertUriToPath(uri)

        if (!engine.loadTrack(id, filePath, startTimeMs)) {
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

    Function("setTrackPitch") { trackId: String, semitones: Double ->
      val engine = audioEngine ?: throw Exception("Engine not initialized")
      engine.setTrackPitch(trackId, semitones.toFloat())
    }

    Function("getTrackPitch") { trackId: String ->
      audioEngine?.getTrackPitch(trackId)?.toDouble() ?: 0.0
    }

    Function("setTrackSpeed") { trackId: String, rate: Double ->
      val engine = audioEngine ?: throw Exception("Engine not initialized")
      engine.setTrackSpeed(trackId, rate.toFloat())
    }

    Function("getTrackSpeed") { trackId: String ->
      audioEngine?.getTrackSpeed(trackId)?.toDouble() ?: 1.0
    }

    AsyncFunction("startRecording") { config: Map<String, Any?>? ->
      val engine = audioEngine ?: throw Exception("Engine not initialized")

      val sampleRate = (config?.get("sampleRate") as? Number)?.toInt() ?: 44100
      val channels = (config?.get("channels") as? Number)?.toInt() ?: 1
      val format = (config?.get("format") as? String) ?: "aac"
      val bitrate = (config?.get("bitrate") as? Number)?.toInt() ?: 128000
      val quality = config?.get("quality") as? String

      val actualBitrate = when (quality) {
        "low" -> 64000
        "medium" -> 128000
        "high" -> 192000
        else -> bitrate
      }

      val outputDir = getCacheDir()
      val fileName = "recording_${System.currentTimeMillis()}.$format"
      val outputPath = "$outputDir/$fileName"

      Log.d(TAG, "Starting recording: $outputPath (format=$format, bitrate=$actualBitrate)")

      val success = engine.startRecording(
        outputPath = outputPath,
        sampleRate = sampleRate,
        channels = channels,
        format = format,
        bitrate = actualBitrate,
        bitsPerSample = 16
      )

      if (!success) {
        throw Exception("Failed to start recording")
      }

      Log.d(TAG, "Recording started successfully")
    }

    AsyncFunction("stopRecording") {
      val engine = audioEngine ?: throw Exception("Engine not initialized")

      Log.d(TAG, "Stopping recording")
      val result = engine.stopRecording()

      if (!result.success) {
        throw Exception("Failed to stop recording: ${result.errorMessage}")
      }

      Log.d(TAG, "Recording stopped: ${result.fileSize} bytes, ${result.durationSamples} samples")

      sendEvent(
        "recordingStopped",
        mapOf(
          "uri" to "file://${result.outputPath}",
          "duration" to (result.durationSamples / 44.1).toInt(),
          "startTimeMs" to result.startTimeMs,
          "startTimeSamples" to result.startTimeSamples,
          "sampleRate" to 44100,
          "channels" to 1,
          "format" to "aac",
          "fileSize" to result.fileSize
        )
      )

      mapOf(
        "uri" to "file://${result.outputPath}",
        "duration" to (result.durationSamples / 44.1).toInt(),
        "startTimeMs" to result.startTimeMs,
        "startTimeSamples" to result.startTimeSamples,
        "sampleRate" to 44100,
        "channels" to 1,
        "format" to "aac",
        "fileSize" to result.fileSize
      )
    }

    Function("isRecording") {
      audioEngine?.isRecording() ?: false
    }

    Function("setRecordingVolume") { volume: Double ->
      val engine = audioEngine ?: throw Exception("Engine not initialized")
      engine.setRecordingVolume(volume.toFloat())
    }

    AsyncFunction("extractTrack") { trackId: String, config: Map<String, Any?>?, promise: Promise ->
      val engine = audioEngine ?: throw Exception("Engine not initialized")

      val format = (config?.get("format") as? String) ?: "wav"
      val bitrate = (config?.get("bitrate") as? Number)?.toInt() ?: 128000
      val bitsPerSample = (config?.get("bitsPerSample") as? Number)?.toInt() ?: 16
      val includeEffects = (config?.get("includeEffects") as? Boolean) ?: true
      val outputDir = (config?.get("outputDir") as? String) ?: getCacheDir()

      val fileName = "track_${trackId}_${System.currentTimeMillis()}.$format"
      val outputPath = "$outputDir/$fileName"

      Log.d(TAG, "Extracting track: $trackId to $outputPath (format=$format, bitrate=$bitrate)")

      val jobId = engine.startExtractTrack(
        trackId = trackId,
        outputPath = outputPath,
        format = format,
        bitrate = bitrate,
        bitsPerSample = bitsPerSample,
        includeEffects = includeEffects
      )

      if (jobId <= 0L) {
        promise.reject("EXTRACTION_FAILED", "Failed to start extraction", null)
        return@AsyncFunction
      }

      pendingExtractions[jobId] = PendingExtraction(
        promise = promise,
        trackId = trackId,
        outputPath = outputPath,
        format = format,
        bitrate = bitrate,
        operation = "track"
      )
      lastExtractionJobId = jobId
      attachExtractionListeners(engine)
    }

    AsyncFunction("extractAllTracks") { config: Map<String, Any?>?, promise: Promise ->
      val engine = audioEngine ?: throw Exception("Engine not initialized")

      val format = (config?.get("format") as? String) ?: "wav"
      val bitrate = (config?.get("bitrate") as? Number)?.toInt() ?: 128000
      val bitsPerSample = (config?.get("bitsPerSample") as? Number)?.toInt() ?: 16
      val includeEffects = (config?.get("includeEffects") as? Boolean) ?: true
      val outputDir = (config?.get("outputDir") as? String) ?: getCacheDir()

      val fileName = "mixed_tracks_${System.currentTimeMillis()}.$format"
      val outputPath = "$outputDir/$fileName"

      Log.d(TAG, "Extracting all tracks mixed to $outputPath (format=$format, bitrate=$bitrate)")

      val jobId = engine.startExtractAllTracks(
        outputPath = outputPath,
        format = format,
        bitrate = bitrate,
        bitsPerSample = bitsPerSample,
        includeEffects = includeEffects
      )

      if (jobId <= 0L) {
        promise.reject("EXTRACTION_FAILED", "Failed to start extraction", null)
        return@AsyncFunction
      }

      pendingExtractions[jobId] = PendingExtraction(
        promise = promise,
        trackId = null,
        outputPath = outputPath,
        format = format,
        bitrate = bitrate,
        operation = "mix"
      )
      lastExtractionJobId = jobId
      attachExtractionListeners(engine)
    }

    Function("cancelExtraction") { jobId: Double? ->
      val engine = audioEngine ?: throw Exception("Engine not initialized")
      val resolvedJobId = jobId?.toLong() ?: lastExtractionJobId
      if (resolvedJobId == null) {
        false
      } else {
        engine.cancelExtraction(resolvedJobId)
      }
    }

    Function("getInputLevel") {
      audioEngine?.getInputLevel()?.toDouble() ?: 0.0
    }

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

  private fun attachExtractionListeners(engine: AudioEngine) {
    if (pendingExtractions.isEmpty()) {
      return
    }

    engine.setExtractionProgressListener { jobId, progress ->
      val pending = pendingExtractions[jobId] ?: return@setExtractionProgressListener
      val clamped = progress.coerceIn(0.0f, 1.0f)
      val lastLogged = progressLogState[jobId] ?: -1.0f
      if (clamped >= 1.0f || clamped - lastLogged >= 0.05f) {
        progressLogState[jobId] = clamped
        Log.d(TAG, "Extraction progress job=$jobId op=${pending.operation} progress=$clamped")
      }
      sendEvent(
        "extractionProgress",
        mapOf(
          "jobId" to jobId,
          "progress" to clamped.toDouble(),
          "trackId" to pending.trackId,
          "outputPath" to pending.outputPath,
          "format" to pending.format,
          "operation" to pending.operation
        )
      )
    }

    engine.setExtractionCompletionListener { jobId, result ->
      val pending = pendingExtractions.remove(jobId)
      progressLogState.remove(jobId)
      if (pending == null) {
        return@setExtractionCompletionListener
      }

      if (!result.success) {
        Log.e(TAG, "Extraction failed for job=$jobId: ${result.errorMessage}")
        sendEvent(
          "extractionComplete",
          mapOf(
            "success" to false,
            "jobId" to jobId,
            "trackId" to pending.trackId,
            "outputPath" to pending.outputPath,
            "format" to pending.format,
            "bitrate" to pending.bitrate,
            "errorMessage" to (result.errorMessage ?: "Unknown error"),
            "operation" to pending.operation
          )
        )
        val code = if (result.errorMessage == "Extraction cancelled") {
          "EXTRACTION_CANCELLED"
        } else {
          "EXTRACTION_FAILED"
        }
        pending.promise.reject(code, result.errorMessage, null)
      } else {
        Log.d(TAG, "Extraction successful: ${result.fileSize} bytes, ${result.durationSamples} samples")
        Log.d(TAG, "Extraction complete event (${pending.operation})")

        sendEvent(
          "extractionComplete",
          mapOf(
            "success" to true,
            "jobId" to jobId,
            "trackId" to pending.trackId,
            "uri" to "file://${result.outputPath}",
            "outputPath" to result.outputPath,
            "duration" to (result.durationSamples / 44.1).toInt(),
            "format" to pending.format,
            "fileSize" to result.fileSize,
            "bitrate" to pending.bitrate,
            "operation" to pending.operation
          )
        )

        val response = if (pending.operation == "mix") {
          listOf(
            mapOf(
              "uri" to "file://${result.outputPath}",
              "duration" to (result.durationSamples / 44.1).toInt(),
              "format" to pending.format,
              "fileSize" to result.fileSize,
              "bitrate" to pending.bitrate
            )
          )
        } else {
          mapOf(
            "trackId" to pending.trackId,
            "uri" to "file://${result.outputPath}",
            "duration" to (result.durationSamples / 44.1).toInt(),
            "format" to pending.format,
            "fileSize" to result.fileSize,
            "bitrate" to pending.bitrate
          )
        }

        pending.promise.resolve(response)
      }

      if (pendingExtractions.isEmpty()) {
        engine.setExtractionProgressListener(null)
        engine.setExtractionCompletionListener(null)
      }
    }
  }

  private fun getCacheDir(): String {
    return appContext.reactContext?.cacheDir?.absolutePath ?: "/tmp"
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
