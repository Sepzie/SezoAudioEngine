package com.sezo.audioengine

import androidx.annotation.Keep

class AudioEngine {
  private var nativeHandle: Long = 0
  private var extractionProgressListener: ((Long, Float) -> Unit)? = null
  private var extractionCompletionListener: ((Long, ExtractionResult) -> Unit)? = null
  private var playbackStateListener: ((String, Double, Double) -> Unit)? = null

  init {
    System.loadLibrary("sezo_audio_engine")
    nativeHandle = nativeCreate()
  }

  fun initialize(sampleRate: Int = 44100, maxTracks: Int = 8): Boolean {
    return nativeInitialize(nativeHandle, sampleRate, maxTracks)
  }

  fun release() {
    setPlaybackStateListener(null)
    nativeRelease(nativeHandle)
  }

  fun destroy() {
    if (nativeHandle != 0L) {
      setPlaybackStateListener(null)
      nativeDestroy(nativeHandle)
      nativeHandle = 0
    }
  }

  // Track management
  @JvmOverloads
  fun loadTrack(trackId: String, filePath: String, startTimeMs: Double = 0.0): Boolean {
    return nativeLoadTrack(nativeHandle, trackId, filePath, startTimeMs)
  }

  fun unloadTrack(trackId: String): Boolean {
    return nativeUnloadTrack(nativeHandle, trackId)
  }

  fun unloadAllTracks() {
    nativeUnloadAllTracks(nativeHandle)
  }

  // Playback control
  fun play() {
    nativePlay(nativeHandle)
  }

  fun pause() {
    nativePause(nativeHandle)
  }

  fun stop() {
    nativeStop(nativeHandle)
  }

  fun seek(positionMs: Double) {
    nativeSeek(nativeHandle, positionMs)
  }

  fun isPlaying(): Boolean {
    return nativeIsPlaying(nativeHandle)
  }

  fun getCurrentPosition(): Double {
    return nativeGetCurrentPosition(nativeHandle)
  }

  fun getDuration(): Double {
    return nativeGetDuration(nativeHandle)
  }

  fun setPlaybackStateListener(listener: ((String, Double, Double) -> Unit)?) {
    playbackStateListener = listener
    if (nativeHandle != 0L) {
      nativeSetPlaybackStateListener(nativeHandle, listener != null)
    }
  }

  // Track controls
  fun setTrackVolume(trackId: String, volume: Float) {
    nativeSetTrackVolume(nativeHandle, trackId, volume)
  }

  fun setTrackMuted(trackId: String, muted: Boolean) {
    nativeSetTrackMuted(nativeHandle, trackId, muted)
  }

  fun setTrackSolo(trackId: String, solo: Boolean) {
    nativeSetTrackSolo(nativeHandle, trackId, solo)
  }

  fun setTrackPan(trackId: String, pan: Float) {
    nativeSetTrackPan(nativeHandle, trackId, pan)
  }

  // Master controls
  fun setMasterVolume(volume: Float) {
    nativeSetMasterVolume(nativeHandle, volume)
  }

  fun getMasterVolume(): Float {
    return nativeGetMasterVolume(nativeHandle)
  }

  // Effects (Phase 2) - Master controls
  fun setPitch(semitones: Float) {
    nativeSetPitch(nativeHandle, semitones)
  }

  fun getPitch(): Float {
    return nativeGetPitch(nativeHandle)
  }

  fun setSpeed(rate: Float) {
    nativeSetSpeed(nativeHandle, rate)
  }

  fun getSpeed(): Float {
    return nativeGetSpeed(nativeHandle)
  }

  // Effects (Phase 2) - Per-track controls
  fun setTrackPitch(trackId: String, semitones: Float) {
    nativeSetTrackPitch(nativeHandle, trackId, semitones)
  }

  fun getTrackPitch(trackId: String): Float {
    return nativeGetTrackPitch(nativeHandle, trackId)
  }

  fun setTrackSpeed(trackId: String, rate: Float) {
    nativeSetTrackSpeed(nativeHandle, trackId, rate)
  }

  fun getTrackSpeed(trackId: String): Float {
    return nativeGetTrackSpeed(nativeHandle, trackId)
  }

  // Recording (Phase 3)
  data class RecordingConfig(
    val sampleRate: Int = 44100,
    val channels: Int = 1,
    val format: String = "aac",
    val bitrate: Int = 128000,
    val bitsPerSample: Int = 16,
    val enableNoiseGate: Boolean = false,
    val enableNormalization: Boolean = false
  )

  data class RecordingResult(
    val success: Boolean,
    val outputPath: String,
    val durationSamples: Long,
    val startTimeSamples: Long,
    val startTimeMs: Double,
    val fileSize: Long,
    val errorMessage: String?
  )

  fun startRecording(
    outputPath: String,
    sampleRate: Int = 44100,
    channels: Int = 1,
    format: String = "aac",
    bitrate: Int = 128000,
    bitsPerSample: Int = 16
  ): Boolean {
    return nativeStartRecording(
      nativeHandle,
      outputPath,
      sampleRate,
      channels,
      format,
      bitrate,
      bitsPerSample
    )
  }

  fun stopRecording(): RecordingResult {
    val resultMap = nativeStopRecording(nativeHandle)
    return RecordingResult(
      success = resultMap["success"] as? Boolean ?: false,
      outputPath = resultMap["outputPath"] as? String ?: "",
      durationSamples = resultMap["durationSamples"] as? Long ?: 0L,
      startTimeSamples = (resultMap["startTimeSamples"] as? Number)?.toLong() ?: 0L,
      startTimeMs = (resultMap["startTimeMs"] as? Number)?.toDouble() ?: 0.0,
      fileSize = resultMap["fileSize"] as? Long ?: 0L,
      errorMessage = resultMap["errorMessage"] as? String
    )
  }

  fun isRecording(): Boolean {
    return nativeIsRecording(nativeHandle)
  }

  fun getInputLevel(): Float {
    return nativeGetInputLevel(nativeHandle)
  }

  fun setRecordingVolume(volume: Float) {
    nativeSetRecordingVolume(nativeHandle, volume)
  }

  // Extraction (Phase 6)
  data class ExtractionResult(
    val success: Boolean,
    val trackId: String?,
    val outputPath: String,
    val durationSamples: Long,
    val fileSize: Long,
    val errorMessage: String?
  )

  fun extractTrack(
    trackId: String,
    outputPath: String,
    format: String = "wav",
    bitrate: Int = 128000,
    bitsPerSample: Int = 16,
    includeEffects: Boolean = true
  ): ExtractionResult {
    val resultMap = nativeExtractTrack(
      nativeHandle, trackId, outputPath, format, bitrate, bitsPerSample, includeEffects
    ) as? Map<*, *> ?: return ExtractionResult(
      success = false,
      trackId = trackId,
      outputPath = outputPath,
      durationSamples = 0,
      fileSize = 0,
      errorMessage = "Native method returned null"
    )

    return ExtractionResult(
      success = resultMap["success"] as? Boolean ?: false,
      trackId = resultMap["trackId"] as? String,
      outputPath = resultMap["outputPath"] as? String ?: outputPath,
      durationSamples = resultMap["durationSamples"] as? Long ?: 0L,
      fileSize = resultMap["fileSize"] as? Long ?: 0L,
      errorMessage = resultMap["errorMessage"] as? String
    )
  }

  fun startExtractTrack(
    trackId: String,
    outputPath: String,
    format: String = "wav",
    bitrate: Int = 128000,
    bitsPerSample: Int = 16,
    includeEffects: Boolean = true
  ): Long {
    return nativeStartExtractTrack(
      nativeHandle, trackId, outputPath, format, bitrate, bitsPerSample, includeEffects
    )
  }

  fun extractAllTracks(
    outputPath: String,
    format: String = "wav",
    bitrate: Int = 128000,
    bitsPerSample: Int = 16,
    includeEffects: Boolean = true
  ): ExtractionResult {
    val resultMap = nativeExtractAllTracks(
      nativeHandle, outputPath, format, bitrate, bitsPerSample, includeEffects
    ) as? Map<*, *> ?: return ExtractionResult(
      success = false,
      trackId = null,
      outputPath = outputPath,
      durationSamples = 0,
      fileSize = 0,
      errorMessage = "Native method returned null"
    )

    return ExtractionResult(
      success = resultMap["success"] as? Boolean ?: false,
      trackId = null,
      outputPath = resultMap["outputPath"] as? String ?: outputPath,
      durationSamples = resultMap["durationSamples"] as? Long ?: 0L,
      fileSize = resultMap["fileSize"] as? Long ?: 0L,
      errorMessage = resultMap["errorMessage"] as? String
    )
  }

  fun startExtractAllTracks(
    outputPath: String,
    format: String = "wav",
    bitrate: Int = 128000,
    bitsPerSample: Int = 16,
    includeEffects: Boolean = true
  ): Long {
    return nativeStartExtractAllTracks(
      nativeHandle, outputPath, format, bitrate, bitsPerSample, includeEffects
    )
  }

  fun cancelExtraction(jobId: Long): Boolean {
    return nativeCancelExtraction(nativeHandle, jobId)
  }

  fun setExtractionProgressListener(listener: ((Long, Float) -> Unit)?) {
    extractionProgressListener = listener
  }

  fun setExtractionCompletionListener(listener: ((Long, ExtractionResult) -> Unit)?) {
    extractionCompletionListener = listener
  }

  @Keep
  private fun onNativeExtractionProgress(jobId: Long, progress: Float) {
    extractionProgressListener?.invoke(jobId, progress)
  }

  @Keep
  private fun onNativeExtractionComplete(jobId: Long, result: Map<String, Any?>) {
    val success = result["success"] as? Boolean ?: false
    val trackId = result["trackId"] as? String
    val outputPath = result["outputPath"] as? String ?: ""
    val durationSamples = when (val value = result["durationSamples"]) {
      is Number -> value.toLong()
      else -> 0L
    }
    val fileSize = when (val value = result["fileSize"]) {
      is Number -> value.toLong()
      else -> 0L
    }
    val errorMessage = result["errorMessage"] as? String

    extractionCompletionListener?.invoke(
      jobId,
      ExtractionResult(
        success = success,
        trackId = trackId,
        outputPath = outputPath,
        durationSamples = durationSamples,
        fileSize = fileSize,
        errorMessage = errorMessage
      )
    )
  }

  @Keep
  private fun onNativePlaybackStateChanged(state: Int, positionMs: Double, durationMs: Double) {
    val mappedState = when (state) {
      0 -> "stopped"
      1 -> "playing"
      2 -> "paused"
      3 -> "recording"
      else -> "stopped"
    }
    playbackStateListener?.invoke(mappedState, positionMs, durationMs)
  }

  // Native method declarations
  private external fun nativeCreate(): Long
  private external fun nativeDestroy(handle: Long)
  private external fun nativeInitialize(handle: Long, sampleRate: Int, maxTracks: Int): Boolean
  private external fun nativeRelease(handle: Long)

  private external fun nativeLoadTrack(
    handle: Long, trackId: String, filePath: String, startTimeMs: Double
  ): Boolean
  private external fun nativeUnloadTrack(handle: Long, trackId: String): Boolean
  private external fun nativeUnloadAllTracks(handle: Long)

  private external fun nativePlay(handle: Long)
  private external fun nativePause(handle: Long)
  private external fun nativeStop(handle: Long)
  private external fun nativeSeek(handle: Long, positionMs: Double)
  private external fun nativeIsPlaying(handle: Long): Boolean
  private external fun nativeGetCurrentPosition(handle: Long): Double
  private external fun nativeGetDuration(handle: Long): Double
  private external fun nativeSetPlaybackStateListener(handle: Long, enabled: Boolean)

  private external fun nativeSetTrackVolume(handle: Long, trackId: String, volume: Float)
  private external fun nativeSetTrackMuted(handle: Long, trackId: String, muted: Boolean)
  private external fun nativeSetTrackSolo(handle: Long, trackId: String, solo: Boolean)
  private external fun nativeSetTrackPan(handle: Long, trackId: String, pan: Float)

  private external fun nativeSetMasterVolume(handle: Long, volume: Float)
  private external fun nativeGetMasterVolume(handle: Long): Float

  private external fun nativeSetPitch(handle: Long, semitones: Float)
  private external fun nativeGetPitch(handle: Long): Float
  private external fun nativeSetSpeed(handle: Long, rate: Float)
  private external fun nativeGetSpeed(handle: Long): Float

  private external fun nativeSetTrackPitch(handle: Long, trackId: String, semitones: Float)
  private external fun nativeGetTrackPitch(handle: Long, trackId: String): Float
  private external fun nativeSetTrackSpeed(handle: Long, trackId: String, rate: Float)
  private external fun nativeGetTrackSpeed(handle: Long, trackId: String): Float

  // Recording (Phase 3)
  private external fun nativeStartRecording(
    handle: Long, outputPath: String, sampleRate: Int, channels: Int,
    format: String, bitrate: Int, bitsPerSample: Int
  ): Boolean
  private external fun nativeStopRecording(handle: Long): Map<String, Any?>
  private external fun nativeIsRecording(handle: Long): Boolean
  private external fun nativeGetInputLevel(handle: Long): Float
  private external fun nativeSetRecordingVolume(handle: Long, volume: Float)

  private external fun nativeExtractTrack(
    handle: Long, trackId: String, outputPath: String,
    format: String, bitrate: Int, bitsPerSample: Int, includeEffects: Boolean
  ): Any?

  private external fun nativeExtractAllTracks(
    handle: Long, outputPath: String,
    format: String, bitrate: Int, bitsPerSample: Int, includeEffects: Boolean
  ): Any?

  private external fun nativeStartExtractTrack(
    handle: Long, trackId: String, outputPath: String,
    format: String, bitrate: Int, bitsPerSample: Int, includeEffects: Boolean
  ): Long

  private external fun nativeStartExtractAllTracks(
    handle: Long, outputPath: String,
    format: String, bitrate: Int, bitsPerSample: Int, includeEffects: Boolean
  ): Long

  private external fun nativeCancelExtraction(handle: Long, jobId: Long): Boolean
}
