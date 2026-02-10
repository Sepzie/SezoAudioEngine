package expo.modules.audioengine

import android.media.AudioManager
import android.os.Bundle
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
  private var activeRecordingFormat: String = "aac"
  private var wasPlayingBeforePause: Boolean = false
  private var backgroundPlaybackEnabled: Boolean = false
  private var shouldResumeAfterTransientFocusLoss: Boolean = false
  private var volumeBeforeDuck: Float? = null
  private var audioFocusManager: AudioFocusManager? = null
  private val nowPlayingMetadata = mutableMapOf<String, Any?>()

  private val backgroundController = object : BackgroundPlaybackBridge.Controller {
    override fun play(): Boolean {
      val engine = audioEngine ?: return false
      return startPlaybackInternal(engine, fromSystem = true)
    }

    override fun pause() {
      audioEngine?.let { pausePlaybackInternal(it, fromSystem = true) }
    }

    override fun stop() {
      audioEngine?.let { stopPlaybackInternal(it, fromSystem = true) }
    }

    override fun seekTo(positionMs: Double) {
      audioEngine?.seek(positionMs)
      if (backgroundPlaybackEnabled) {
        syncBackgroundService(isPlaying = audioEngine?.isPlaying() ?: false)
      }
    }

    override fun isPlaying(): Boolean = audioEngine?.isPlaying() ?: false

    override fun getCurrentPositionMs(): Double = audioEngine?.getCurrentPosition() ?: 0.0

    override fun getDurationMs(): Double = audioEngine?.getDuration() ?: 0.0
  }

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

    OnActivityEntersBackground {
      Log.d(TAG, "Activity entering background")
      val engine = audioEngine ?: return@OnActivityEntersBackground

      if (backgroundPlaybackEnabled) {
        Log.d(TAG, "Background playback enabled, keeping engine running")
        return@OnActivityEntersBackground
      }

      wasPlayingBeforePause = engine.isPlaying()
      if (wasPlayingBeforePause) {
        Log.d(TAG, "Pausing engine before background (was playing)")
        pausePlaybackInternal(engine, fromSystem = true, keepAudioFocus = false)
        sendEvent("engineStateChanged", mapOf(
          "reason" to "backgrounded",
          "wasPlaying" to true
        ))
      }
    }

    OnActivityEntersForeground {
      Log.d(TAG, "Activity entering foreground")
      val engine = audioEngine ?: return@OnActivityEntersForeground

      // Check stream health on resume
      if (!engine.isStreamHealthy()) {
        Log.w(TAG, "Stream unhealthy on resume, attempting restart")
        val restarted = engine.restartStream()
        sendEvent("engineStateChanged", mapOf(
          "reason" to "streamRestarted",
          "success" to restarted
        ))
        if (!restarted) {
          Log.e(TAG, "Failed to restart stream on resume")
          sendEvent("error", mapOf(
            "code" to "STREAM_DISCONNECTED",
            "message" to "Audio stream could not be recovered after returning to foreground"
          ))
          return@OnActivityEntersForeground
        }
      }

      // Resume playback if it was playing before backgrounding
      if (wasPlayingBeforePause && !backgroundPlaybackEnabled) {
        Log.d(TAG, "Resuming playback after returning to foreground")
        val resumed = startPlaybackInternal(engine, fromSystem = true)
        wasPlayingBeforePause = false
        sendEvent(
          "engineStateChanged",
          mapOf(
            "reason" to "resumed",
            "wasPlaying" to true,
            "success" to resumed
          )
        )
      }
    }

    OnDestroy {
      Log.d(TAG, "Module being destroyed, releasing engine")
      teardownBackgroundPlayback(clearMetadata = true, stopService = true)
      audioEngine?.let { engine ->
        synchronized(pendingExtractions) {
          pendingExtractions.keys.forEach { engine.cancelExtraction(it) }
          pendingExtractions.clear()
        }
        progressLogState.clear()
        engine.setExtractionProgressListener(null)
        engine.setExtractionCompletionListener(null)
        engine.setPlaybackStateListener(null)
        engine.release()
        engine.destroy()
      }
      BackgroundPlaybackBridge.setController(null)
      audioEngine = null
      loadedTrackIds.clear()
    }

    AsyncFunction("initialize") { config: Map<String, Any?> ->
      Log.d(TAG, "Initialize called with config: $config")

      val sampleRate = (config["sampleRate"] as? Number)?.toInt() ?: 44100
      val maxTracks = (config["maxTracks"] as? Number)?.toInt() ?: 8

      audioEngine = AudioEngine()
      val success = audioEngine?.initialize(sampleRate, maxTracks) ?: false

      if (!success) {
        throw Exception("Failed to initialize audio engine")
      }

      ensureAudioFocusManager()
      BackgroundPlaybackBridge.setController(backgroundController)

      audioEngine?.setPlaybackStateListener { state, positionMs, durationMs ->
        sendEvent(
          "playbackStateChange",
          mapOf(
            "state" to state,
            "positionMs" to positionMs,
            "durationMs" to durationMs
          )
        )
        handlePlaybackStateChange(state)
      }

      Log.d(TAG, "Audio engine initialized successfully")
    }

    AsyncFunction("release") {
      Log.d(TAG, "Release called")
      teardownBackgroundPlayback(clearMetadata = false, stopService = true)
      audioEngine?.let { engine ->
        synchronized(pendingExtractions) {
          pendingExtractions.keys.forEach { engine.cancelExtraction(it) }
          pendingExtractions.clear()
        }
        progressLogState.clear()
        engine.setExtractionProgressListener(null)
        engine.setExtractionCompletionListener(null)
        engine.setPlaybackStateListener(null)
        engine.release()
        engine.destroy()
      }
      BackgroundPlaybackBridge.setController(null)
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

    AsyncFunction("play") {
      val engine = audioEngine ?: throw Exception("Engine not initialized")
      if (!startPlaybackInternal(engine, fromSystem = false)) {
        throw Exception("Failed to start playback")
      }
      Log.d(TAG, "Playback started")
    }

    Function("pause") {
      val engine = audioEngine ?: throw Exception("Engine not initialized")
      pausePlaybackInternal(engine, fromSystem = false)
      Log.d(TAG, "Playback paused")
    }

    Function("stop") {
      val engine = audioEngine ?: throw Exception("Engine not initialized")
      stopPlaybackInternal(engine, fromSystem = false)
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

      activeRecordingFormat = format
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
          "format" to activeRecordingFormat,
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
        "format" to activeRecordingFormat,
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

    AsyncFunction("enableBackgroundPlayback") { metadata: Map<String, Any?> ->
      val engine = audioEngine ?: throw Exception("Engine not initialized")
      backgroundPlaybackEnabled = true
      mergeNowPlayingMetadata(metadata)
      ensureAudioFocusManager()
      BackgroundPlaybackBridge.setController(backgroundController)
      if (engine.isPlaying()) {
        requestAudioFocus()
      }
      syncBackgroundService(engine.isPlaying())
      sendEvent("engineStateChanged", mapOf("reason" to "backgroundPlaybackEnabled"))
    }
    Function("updateNowPlayingInfo") { metadata: Map<String, Any?> ->
      mergeNowPlayingMetadata(metadata)
      if (backgroundPlaybackEnabled) {
        syncBackgroundService(audioEngine?.isPlaying() ?: false)
      }
    }
    AsyncFunction("disableBackgroundPlayback") {
      backgroundPlaybackEnabled = false
      shouldResumeAfterTransientFocusLoss = false
      volumeBeforeDuck = null
      stopBackgroundService()
      if (!(audioEngine?.isPlaying() ?: false)) {
        abandonAudioFocus()
      }
      nowPlayingMetadata.clear()
      sendEvent("engineStateChanged", mapOf("reason" to "backgroundPlaybackDisabled"))
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
      "engineStateChanged",
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

  private fun startPlaybackInternal(engine: AudioEngine, fromSystem: Boolean): Boolean {
    if (!engine.isStreamHealthy()) {
      Log.w(TAG, "Stream unhealthy before play, attempting restart")
      if (!engine.restartStream()) {
        if (!fromSystem) {
          sendEvent(
            "error",
            mapOf(
              "code" to "STREAM_DISCONNECTED",
              "message" to "Audio stream is disconnected and could not be recovered"
            )
          )
        }
        return false
      }
    }

    if (!requestAudioFocus()) {
      Log.w(TAG, "Audio focus request denied")
      if (!fromSystem) {
        sendEvent(
          "error",
          mapOf(
            "code" to "AUDIO_FOCUS_DENIED",
            "message" to "Could not gain audio focus for playback"
          )
        )
      }
      return false
    }

    shouldResumeAfterTransientFocusLoss = false
    restoreDuckedVolumeIfNeeded(engine)
    engine.play()

    if (backgroundPlaybackEnabled) {
      syncBackgroundService(true)
    }
    return true
  }

  private fun pausePlaybackInternal(
    engine: AudioEngine,
    fromSystem: Boolean,
    keepAudioFocus: Boolean = false
  ) {
    engine.pause()
    shouldResumeAfterTransientFocusLoss = false
    restoreDuckedVolumeIfNeeded(engine)

    if (!keepAudioFocus) {
      abandonAudioFocus()
    }

    if (backgroundPlaybackEnabled) {
      syncBackgroundService(false)
    }

    if (fromSystem) {
      sendEvent("engineStateChanged", mapOf("reason" to "pausedFromSystem"))
    }
  }

  private fun stopPlaybackInternal(engine: AudioEngine, fromSystem: Boolean) {
    engine.stop()
    shouldResumeAfterTransientFocusLoss = false
    restoreDuckedVolumeIfNeeded(engine)
    abandonAudioFocus()

    if (backgroundPlaybackEnabled) {
      stopBackgroundService()
    }

    if (fromSystem) {
      sendEvent("engineStateChanged", mapOf("reason" to "stoppedFromSystem"))
    }
  }

  private fun handlePlaybackStateChange(state: String) {
    if (!backgroundPlaybackEnabled) {
      return
    }

    when (state) {
      "playing" -> {
        requestAudioFocus()
        syncBackgroundService(true)
      }
      "paused" -> syncBackgroundService(false)
      "stopped" -> stopBackgroundService()
    }
  }

  private fun ensureAudioFocusManager(): AudioFocusManager? {
    if (audioFocusManager != null) {
      return audioFocusManager
    }

    val context = appContext.reactContext?.applicationContext ?: run {
      Log.w(TAG, "React context unavailable; audio focus manager not initialized")
      return null
    }

    audioFocusManager = AudioFocusManager(context) { focusChange ->
      handleAudioFocusChange(focusChange)
    }
    return audioFocusManager
  }

  private fun requestAudioFocus(): Boolean {
    return ensureAudioFocusManager()?.requestFocus() ?: false
  }

  private fun abandonAudioFocus() {
    audioFocusManager?.abandonFocus()
  }

  private fun handleAudioFocusChange(focusChange: Int) {
    val engine = audioEngine ?: return
    when (focusChange) {
      AudioManager.AUDIOFOCUS_GAIN -> {
        restoreDuckedVolumeIfNeeded(engine)
        if (shouldResumeAfterTransientFocusLoss) {
          shouldResumeAfterTransientFocusLoss = false
          startPlaybackInternal(engine, fromSystem = true)
        }
      }
      AudioManager.AUDIOFOCUS_LOSS_TRANSIENT -> {
        if (engine.isPlaying()) {
          shouldResumeAfterTransientFocusLoss = true
          pausePlaybackInternal(engine, fromSystem = true, keepAudioFocus = true)
          sendEvent("engineStateChanged", mapOf("reason" to "audioFocusLossTransient"))
        }
      }
      AudioManager.AUDIOFOCUS_LOSS_TRANSIENT_CAN_DUCK -> {
        if (volumeBeforeDuck == null) {
          volumeBeforeDuck = engine.getMasterVolume()
          val duckedVolume = (volumeBeforeDuck!! * 0.3f).coerceAtLeast(0.05f)
          engine.setMasterVolume(duckedVolume)
          sendEvent(
            "engineStateChanged",
            mapOf(
              "reason" to "audioFocusDuck",
              "volume" to duckedVolume.toDouble()
            )
          )
        }
      }
      AudioManager.AUDIOFOCUS_LOSS -> {
        shouldResumeAfterTransientFocusLoss = false
        if (engine.isPlaying()) {
          pausePlaybackInternal(engine, fromSystem = true)
          sendEvent("engineStateChanged", mapOf("reason" to "audioFocusLoss"))
        } else {
          abandonAudioFocus()
        }
        restoreDuckedVolumeIfNeeded(engine)
      }
    }
  }

  private fun restoreDuckedVolumeIfNeeded(engine: AudioEngine) {
    val previousVolume = volumeBeforeDuck ?: return
    engine.setMasterVolume(previousVolume)
    volumeBeforeDuck = null
  }

  private fun mergeNowPlayingMetadata(metadata: Map<String, Any?>) {
    metadata.forEach { (key, value) ->
      if (value == null) {
        nowPlayingMetadata.remove(key)
      } else {
        nowPlayingMetadata[key] = value
      }
    }
  }

  private fun syncBackgroundService(isPlaying: Boolean) {
    if (!backgroundPlaybackEnabled) {
      return
    }

    val context = appContext.reactContext?.applicationContext ?: return
    val metadataBundle = mapToBundle(nowPlayingMetadata)
    MediaPlaybackService.sync(context, metadataBundle, isPlaying)
  }

  private fun stopBackgroundService() {
    val context = appContext.reactContext?.applicationContext ?: return
    MediaPlaybackService.stop(context)
  }

  private fun teardownBackgroundPlayback(clearMetadata: Boolean, stopService: Boolean) {
    backgroundPlaybackEnabled = false
    shouldResumeAfterTransientFocusLoss = false
    volumeBeforeDuck = null
    if (stopService) {
      stopBackgroundService()
    }
    if (clearMetadata) {
      nowPlayingMetadata.clear()
    }
    abandonAudioFocus()
  }

  private fun mapToBundle(map: Map<String, Any?>): Bundle {
    val bundle = Bundle()
    map.forEach { (key, value) ->
      when (value) {
        null -> Unit
        is String -> bundle.putString(key, value)
        is Boolean -> bundle.putBoolean(key, value)
        is Int -> bundle.putInt(key, value)
        is Long -> bundle.putLong(key, value)
        is Float -> bundle.putFloat(key, value)
        is Double -> bundle.putDouble(key, value)
        is Number -> bundle.putDouble(key, value.toDouble())
        is Map<*, *> -> {
          val child = mutableMapOf<String, Any?>()
          value.forEach { (nestedKey, nestedValue) ->
            if (nestedKey is String) {
              child[nestedKey] = nestedValue
            }
          }
          bundle.putBundle(key, mapToBundle(child))
        }
        else -> bundle.putString(key, value.toString())
      }
    }
    return bundle
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
