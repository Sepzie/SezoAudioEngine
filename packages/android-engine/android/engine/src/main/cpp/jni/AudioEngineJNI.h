#pragma once

#include <jni.h>
#include <string>

namespace sezo {
namespace jni {

/**
 * JNI utilities for type conversion and error handling.
 */
class JNIHelper {
 public:
  /**
   * Convert JNI string to std::string.
   */
  static std::string JStringToString(JNIEnv* env, jstring jstr);

  /**
   * Convert std::string to JNI string.
   */
  static jstring StringToJString(JNIEnv* env, const std::string& str);

  /**
   * Throw a Java exception.
   */
  static void ThrowException(JNIEnv* env, const char* message);
};

}  // namespace jni
}  // namespace sezo

// JNI method declarations
extern "C" {

JNIEXPORT jlong JNICALL
Java_com_sezo_audioengine_AudioEngine_nativeCreate(JNIEnv* env, jobject thiz);

JNIEXPORT void JNICALL
Java_com_sezo_audioengine_AudioEngine_nativeDestroy(JNIEnv* env, jobject thiz, jlong handle);

JNIEXPORT jboolean JNICALL
Java_com_sezo_audioengine_AudioEngine_nativeInitialize(
    JNIEnv* env, jobject thiz, jlong handle, jint sample_rate, jint max_tracks);

JNIEXPORT void JNICALL
Java_com_sezo_audioengine_AudioEngine_nativeRelease(JNIEnv* env, jobject thiz, jlong handle);

JNIEXPORT jboolean JNICALL
Java_com_sezo_audioengine_AudioEngine_nativeLoadTrack(
    JNIEnv* env, jobject thiz, jlong handle, jstring track_id, jstring file_path,
    jdouble start_time_ms);

JNIEXPORT jboolean JNICALL
Java_com_sezo_audioengine_AudioEngine_nativeUnloadTrack(
    JNIEnv* env, jobject thiz, jlong handle, jstring track_id);

JNIEXPORT void JNICALL
Java_com_sezo_audioengine_AudioEngine_nativeUnloadAllTracks(
    JNIEnv* env, jobject thiz, jlong handle);

JNIEXPORT void JNICALL
Java_com_sezo_audioengine_AudioEngine_nativePlay(JNIEnv* env, jobject thiz, jlong handle);

JNIEXPORT void JNICALL
Java_com_sezo_audioengine_AudioEngine_nativePause(JNIEnv* env, jobject thiz, jlong handle);

JNIEXPORT void JNICALL
Java_com_sezo_audioengine_AudioEngine_nativeStop(JNIEnv* env, jobject thiz, jlong handle);

JNIEXPORT void JNICALL
Java_com_sezo_audioengine_AudioEngine_nativeSeek(
    JNIEnv* env, jobject thiz, jlong handle, jdouble position_ms);

JNIEXPORT jboolean JNICALL
Java_com_sezo_audioengine_AudioEngine_nativeIsPlaying(JNIEnv* env, jobject thiz, jlong handle);

JNIEXPORT jboolean JNICALL
Java_com_sezo_audioengine_AudioEngine_nativeIsStreamHealthy(JNIEnv* env, jobject thiz, jlong handle);

JNIEXPORT jboolean JNICALL
Java_com_sezo_audioengine_AudioEngine_nativeRestartStream(JNIEnv* env, jobject thiz, jlong handle);

JNIEXPORT jdouble JNICALL
Java_com_sezo_audioengine_AudioEngine_nativeGetCurrentPosition(
    JNIEnv* env, jobject thiz, jlong handle);

JNIEXPORT jdouble JNICALL
Java_com_sezo_audioengine_AudioEngine_nativeGetDuration(JNIEnv* env, jobject thiz, jlong handle);

JNIEXPORT void JNICALL
Java_com_sezo_audioengine_AudioEngine_nativeSetPlaybackStateListener(
    JNIEnv* env, jobject thiz, jlong handle, jboolean enabled);

JNIEXPORT void JNICALL
Java_com_sezo_audioengine_AudioEngine_nativeSetTrackVolume(
    JNIEnv* env, jobject thiz, jlong handle, jstring track_id, jfloat volume);

JNIEXPORT void JNICALL
Java_com_sezo_audioengine_AudioEngine_nativeSetTrackMuted(
    JNIEnv* env, jobject thiz, jlong handle, jstring track_id, jboolean muted);

JNIEXPORT void JNICALL
Java_com_sezo_audioengine_AudioEngine_nativeSetTrackSolo(
    JNIEnv* env, jobject thiz, jlong handle, jstring track_id, jboolean solo);

JNIEXPORT void JNICALL
Java_com_sezo_audioengine_AudioEngine_nativeSetTrackPan(
    JNIEnv* env, jobject thiz, jlong handle, jstring track_id, jfloat pan);

JNIEXPORT void JNICALL
Java_com_sezo_audioengine_AudioEngine_nativeSetMasterVolume(
    JNIEnv* env, jobject thiz, jlong handle, jfloat volume);

JNIEXPORT jfloat JNICALL
Java_com_sezo_audioengine_AudioEngine_nativeGetMasterVolume(
    JNIEnv* env, jobject thiz, jlong handle);

// Phase 2: Per-track effects
JNIEXPORT void JNICALL
Java_com_sezo_audioengine_AudioEngine_nativeSetTrackPitch(
    JNIEnv* env, jobject thiz, jlong handle, jstring track_id, jfloat semitones);

JNIEXPORT jfloat JNICALL
Java_com_sezo_audioengine_AudioEngine_nativeGetTrackPitch(
    JNIEnv* env, jobject thiz, jlong handle, jstring track_id);

JNIEXPORT void JNICALL
Java_com_sezo_audioengine_AudioEngine_nativeSetTrackSpeed(
    JNIEnv* env, jobject thiz, jlong handle, jstring track_id, jfloat rate);

JNIEXPORT jfloat JNICALL
Java_com_sezo_audioengine_AudioEngine_nativeGetTrackSpeed(
    JNIEnv* env, jobject thiz, jlong handle, jstring track_id);

// Phase 2: Master effects
JNIEXPORT void JNICALL
Java_com_sezo_audioengine_AudioEngine_nativeSetPitch(
    JNIEnv* env, jobject thiz, jlong handle, jfloat semitones);

JNIEXPORT jfloat JNICALL
Java_com_sezo_audioengine_AudioEngine_nativeGetPitch(JNIEnv* env, jobject thiz, jlong handle);

JNIEXPORT void JNICALL
Java_com_sezo_audioengine_AudioEngine_nativeSetSpeed(
    JNIEnv* env, jobject thiz, jlong handle, jfloat rate);

JNIEXPORT jfloat JNICALL
Java_com_sezo_audioengine_AudioEngine_nativeGetSpeed(JNIEnv* env, jobject thiz, jlong handle);

// Phase 6: Extraction
JNIEXPORT jobject JNICALL
Java_com_sezo_audioengine_AudioEngine_nativeExtractTrack(
    JNIEnv* env, jobject thiz, jlong handle, jstring track_id, jstring output_path,
    jstring format, jint bitrate, jint bits_per_sample, jboolean include_effects);

JNIEXPORT jobject JNICALL
Java_com_sezo_audioengine_AudioEngine_nativeExtractAllTracks(
    JNIEnv* env, jobject thiz, jlong handle, jstring output_path,
    jstring format, jint bitrate, jint bits_per_sample, jboolean include_effects);

JNIEXPORT jlong JNICALL
Java_com_sezo_audioengine_AudioEngine_nativeStartExtractTrack(
    JNIEnv* env, jobject thiz, jlong handle, jstring track_id, jstring output_path,
    jstring format, jint bitrate, jint bits_per_sample, jboolean include_effects);

JNIEXPORT jlong JNICALL
Java_com_sezo_audioengine_AudioEngine_nativeStartExtractAllTracks(
    JNIEnv* env, jobject thiz, jlong handle, jstring output_path,
    jstring format, jint bitrate, jint bits_per_sample, jboolean include_effects);

JNIEXPORT jboolean JNICALL
Java_com_sezo_audioengine_AudioEngine_nativeCancelExtraction(
    JNIEnv* env, jobject thiz, jlong handle, jlong job_id);

}  // extern "C"
