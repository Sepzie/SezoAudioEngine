#pragma once

#include <jni.h>

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
    JNIEnv* env, jobject thiz, jlong handle, jstring track_id, jstring file_path);

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

JNIEXPORT jdouble JNICALL
Java_com_sezo_audioengine_AudioEngine_nativeGetCurrentPosition(
    JNIEnv* env, jobject thiz, jlong handle);

JNIEXPORT jdouble JNICALL
Java_com_sezo_audioengine_AudioEngine_nativeGetDuration(JNIEnv* env, jobject thiz, jlong handle);

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

}  // extern "C"
