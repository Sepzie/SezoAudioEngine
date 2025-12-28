#include "AudioEngineJNI.h"
#include "AudioEngine.h"

#include <android/log.h>
#include <string>

#define LOG_TAG "AudioEngineJNI"
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

namespace sezo {
namespace jni {

std::string JNIHelper::JStringToString(JNIEnv* env, jstring jstr) {
  if (!jstr) {
    return "";
  }

  const char* chars = env->GetStringUTFChars(jstr, nullptr);
  std::string result(chars);
  env->ReleaseStringUTFChars(jstr, chars);
  return result;
}

jstring JNIHelper::StringToJString(JNIEnv* env, const std::string& str) {
  return env->NewStringUTF(str.c_str());
}

void JNIHelper::ThrowException(JNIEnv* env, const char* message) {
  jclass exception_class = env->FindClass("java/lang/RuntimeException");
  env->ThrowNew(exception_class, message);
}

}  // namespace jni
}  // namespace sezo

using namespace sezo;
using namespace sezo::jni;

extern "C" {

JNIEXPORT jlong JNICALL
Java_com_sezo_audioengine_AudioEngine_nativeCreate(JNIEnv* env [[maybe_unused]], jobject thiz [[maybe_unused]]) {
  (void)env;
  (void)thiz;
  auto* engine = new AudioEngine();
  return reinterpret_cast<jlong>(engine);
}

JNIEXPORT void JNICALL
Java_com_sezo_audioengine_AudioEngine_nativeDestroy(JNIEnv* env [[maybe_unused]], jobject thiz [[maybe_unused]], jlong handle) {
  (void)env;
  (void)thiz;
  auto* engine = reinterpret_cast<AudioEngine*>(handle);
  delete engine;
}

JNIEXPORT jboolean JNICALL
Java_com_sezo_audioengine_AudioEngine_nativeInitialize(
    JNIEnv* env [[maybe_unused]], jobject thiz [[maybe_unused]], jlong handle, jint sample_rate, jint max_tracks) {
  (void)thiz;
  auto* engine = reinterpret_cast<AudioEngine*>(handle);
  if (!engine) {
    JNIHelper::ThrowException(env, "Engine not initialized");
    return JNI_FALSE;
  }
  return engine->Initialize(sample_rate, max_tracks) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL
Java_com_sezo_audioengine_AudioEngine_nativeRelease(JNIEnv* env [[maybe_unused]], jobject thiz [[maybe_unused]], jlong handle) {
  (void)env;
  (void)thiz;
  auto* engine = reinterpret_cast<AudioEngine*>(handle);
  if (engine) {
  (void)env;
  (void)thiz;
    engine->Release();
  }
}

JNIEXPORT jboolean JNICALL
Java_com_sezo_audioengine_AudioEngine_nativeLoadTrack(
    JNIEnv* env [[maybe_unused]], jobject thiz [[maybe_unused]], jlong handle, jstring track_id, jstring file_path) {
  (void)thiz;
  auto* engine = reinterpret_cast<AudioEngine*>(handle);
  if (!engine) {
    return JNI_FALSE;
  }

  std::string id = JNIHelper::JStringToString(env, track_id);
  std::string path = JNIHelper::JStringToString(env, file_path);

  return engine->LoadTrack(id, path) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL
Java_com_sezo_audioengine_AudioEngine_nativeUnloadTrack(
    JNIEnv* env [[maybe_unused]], jobject thiz [[maybe_unused]], jlong handle, jstring track_id) {
  (void)thiz;
  auto* engine = reinterpret_cast<AudioEngine*>(handle);
  if (!engine) {
    return JNI_FALSE;
  }

  std::string id = JNIHelper::JStringToString(env, track_id);
  return engine->UnloadTrack(id) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL
Java_com_sezo_audioengine_AudioEngine_nativeUnloadAllTracks(
    JNIEnv* env [[maybe_unused]], jobject thiz [[maybe_unused]], jlong handle) {
  (void)env;
  (void)thiz;
  auto* engine = reinterpret_cast<AudioEngine*>(handle);
  if (engine) {
    engine->UnloadAllTracks();
  }
}

JNIEXPORT void JNICALL
Java_com_sezo_audioengine_AudioEngine_nativePlay(JNIEnv* env [[maybe_unused]], jobject thiz [[maybe_unused]], jlong handle) {
  (void)env;
  (void)thiz;
  auto* engine = reinterpret_cast<AudioEngine*>(handle);
  if (engine) {
  (void)env;
  (void)thiz;
    engine->Play();
  }
}

JNIEXPORT void JNICALL
Java_com_sezo_audioengine_AudioEngine_nativePause(JNIEnv* env [[maybe_unused]], jobject thiz [[maybe_unused]], jlong handle) {
  auto* engine = reinterpret_cast<AudioEngine*>(handle);
  if (engine) {
  (void)env;
  (void)thiz;
    engine->Pause();
  }
}

JNIEXPORT void JNICALL
Java_com_sezo_audioengine_AudioEngine_nativeStop(JNIEnv* env [[maybe_unused]], jobject thiz [[maybe_unused]], jlong handle) {
  auto* engine = reinterpret_cast<AudioEngine*>(handle);
  if (engine) {
  (void)env;
  (void)thiz;
    engine->Stop();
  }
}

JNIEXPORT void JNICALL
Java_com_sezo_audioengine_AudioEngine_nativeSeek(
    JNIEnv* env [[maybe_unused]], jobject thiz [[maybe_unused]], jlong handle, jdouble position_ms) {
  auto* engine = reinterpret_cast<AudioEngine*>(handle);
  if (engine) {
    engine->Seek(position_ms);
  }
}

JNIEXPORT jboolean JNICALL
Java_com_sezo_audioengine_AudioEngine_nativeIsPlaying(JNIEnv* env [[maybe_unused]], jobject thiz [[maybe_unused]], jlong handle) {
  auto* engine = reinterpret_cast<AudioEngine*>(handle);
  if (!engine) {
  (void)env;
  (void)thiz;
    return JNI_FALSE;
  }
  return engine->IsPlaying() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jdouble JNICALL
Java_com_sezo_audioengine_AudioEngine_nativeGetCurrentPosition(
    JNIEnv* env [[maybe_unused]], jobject thiz [[maybe_unused]], jlong handle) {
  auto* engine = reinterpret_cast<AudioEngine*>(handle);
  if (!engine) {
    return 0.0;
  }
  return engine->GetCurrentPosition();
}

JNIEXPORT jdouble JNICALL
Java_com_sezo_audioengine_AudioEngine_nativeGetDuration(JNIEnv* env [[maybe_unused]], jobject thiz [[maybe_unused]], jlong handle) {
  auto* engine = reinterpret_cast<AudioEngine*>(handle);
  if (!engine) {
  (void)env;
  (void)thiz;
    return 0.0;
  }
  return engine->GetDuration();
}

JNIEXPORT void JNICALL
Java_com_sezo_audioengine_AudioEngine_nativeSetTrackVolume(
    JNIEnv* env [[maybe_unused]], jobject thiz [[maybe_unused]], jlong handle, jstring track_id, jfloat volume) {
  auto* engine = reinterpret_cast<AudioEngine*>(handle);
  if (engine) {
    std::string id = JNIHelper::JStringToString(env, track_id);
    engine->SetTrackVolume(id, volume);
  }
}

JNIEXPORT void JNICALL
Java_com_sezo_audioengine_AudioEngine_nativeSetTrackMuted(
    JNIEnv* env [[maybe_unused]], jobject thiz [[maybe_unused]], jlong handle, jstring track_id, jboolean muted) {
  auto* engine = reinterpret_cast<AudioEngine*>(handle);
  if (engine) {
    std::string id = JNIHelper::JStringToString(env, track_id);
    engine->SetTrackMuted(id, muted == JNI_TRUE);
  }
}

JNIEXPORT void JNICALL
Java_com_sezo_audioengine_AudioEngine_nativeSetTrackSolo(
    JNIEnv* env [[maybe_unused]], jobject thiz [[maybe_unused]], jlong handle, jstring track_id, jboolean solo) {
  auto* engine = reinterpret_cast<AudioEngine*>(handle);
  if (engine) {
    std::string id = JNIHelper::JStringToString(env, track_id);
    engine->SetTrackSolo(id, solo == JNI_TRUE);
  }
}

JNIEXPORT void JNICALL
Java_com_sezo_audioengine_AudioEngine_nativeSetTrackPan(
    JNIEnv* env [[maybe_unused]], jobject thiz [[maybe_unused]], jlong handle, jstring track_id, jfloat pan) {
  auto* engine = reinterpret_cast<AudioEngine*>(handle);
  if (engine) {
    std::string id = JNIHelper::JStringToString(env, track_id);
    engine->SetTrackPan(id, pan);
  }
}

JNIEXPORT void JNICALL
Java_com_sezo_audioengine_AudioEngine_nativeSetMasterVolume(
    JNIEnv* env [[maybe_unused]], jobject thiz [[maybe_unused]], jlong handle, jfloat volume) {
  auto* engine = reinterpret_cast<AudioEngine*>(handle);
  if (engine) {
    engine->SetMasterVolume(volume);
  }
}

JNIEXPORT jfloat JNICALL
Java_com_sezo_audioengine_AudioEngine_nativeGetMasterVolume(
    JNIEnv* env [[maybe_unused]], jobject thiz [[maybe_unused]], jlong handle) {
  auto* engine = reinterpret_cast<AudioEngine*>(handle);
  if (!engine) {
    return 1.0f;
  }
  return engine->GetMasterVolume();
}

JNIEXPORT void JNICALL
Java_com_sezo_audioengine_AudioEngine_nativeSetPitch(
    JNIEnv* env [[maybe_unused]], jobject thiz [[maybe_unused]], jlong handle, jfloat semitones) {
  auto* engine = reinterpret_cast<AudioEngine*>(handle);
  if (engine) {
    engine->SetPitch(semitones);
  }
}

JNIEXPORT jfloat JNICALL
Java_com_sezo_audioengine_AudioEngine_nativeGetPitch(JNIEnv* env [[maybe_unused]], jobject thiz [[maybe_unused]], jlong handle) {
  auto* engine = reinterpret_cast<AudioEngine*>(handle);
  if (!engine) {
  (void)env;
  (void)thiz;
    return 0.0f;
  }
  return engine->GetPitch();
}

JNIEXPORT void JNICALL
Java_com_sezo_audioengine_AudioEngine_nativeSetSpeed(
    JNIEnv* env [[maybe_unused]], jobject thiz [[maybe_unused]], jlong handle, jfloat rate) {
  auto* engine = reinterpret_cast<AudioEngine*>(handle);
  if (engine) {
    engine->SetSpeed(rate);
  }
}

JNIEXPORT jfloat JNICALL
Java_com_sezo_audioengine_AudioEngine_nativeGetSpeed(JNIEnv* env [[maybe_unused]], jobject thiz [[maybe_unused]], jlong handle) {
  auto* engine = reinterpret_cast<AudioEngine*>(handle);
  if (!engine) {
  (void)env;
  (void)thiz;
    return 1.0f;
  }
  return engine->GetSpeed();
}

}  // extern "C"
