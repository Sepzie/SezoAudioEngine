package com.sezo.audioengine

class AudioEngine {
  private var nativeHandle: Long = 0

  init {
    System.loadLibrary("sezo_audio_engine")
    nativeHandle = nativeCreate()
  }

  fun initialize(sampleRate: Int = 44100, maxTracks: Int = 8): Boolean {
    return nativeInitialize(nativeHandle, sampleRate, maxTracks)
  }

  fun release() {
    nativeRelease(nativeHandle)
  }

  fun destroy() {
    if (nativeHandle != 0L) {
      nativeDestroy(nativeHandle)
      nativeHandle = 0
    }
  }

  // Track management
  fun loadTrack(trackId: String, filePath: String): Boolean {
    return nativeLoadTrack(nativeHandle, trackId, filePath)
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

  // Native method declarations
  private external fun nativeCreate(): Long
  private external fun nativeDestroy(handle: Long)
  private external fun nativeInitialize(handle: Long, sampleRate: Int, maxTracks: Int): Boolean
  private external fun nativeRelease(handle: Long)

  private external fun nativeLoadTrack(handle: Long, trackId: String, filePath: String): Boolean
  private external fun nativeUnloadTrack(handle: Long, trackId: String): Boolean
  private external fun nativeUnloadAllTracks(handle: Long)

  private external fun nativePlay(handle: Long)
  private external fun nativePause(handle: Long)
  private external fun nativeStop(handle: Long)
  private external fun nativeSeek(handle: Long, positionMs: Double)
  private external fun nativeIsPlaying(handle: Long): Boolean
  private external fun nativeGetCurrentPosition(handle: Long): Double
  private external fun nativeGetDuration(handle: Long): Double

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
}
