#include "AudioEngineJNI.h"
#include "AudioEngine.h"

#include <android/log.h>
#include <atomic>
#include <memory>
#include <string>

#define LOG_TAG "AudioEngineJNI"
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

namespace {

JavaVM* g_java_vm = nullptr;
using sezo::AudioEngine;
using sezo::jni::JNIHelper;

struct JniExtractionCallbackContext {
  JavaVM* jvm = nullptr;
  jobject engine_object = nullptr;
  jmethodID progress_method = nullptr;
  jmethodID completion_method = nullptr;
};

JNIEnv* GetEnvForCallback(JavaVM* jvm, bool* did_attach) {
  if (did_attach) {
    *did_attach = false;
  }
  if (!jvm) {
    return nullptr;
  }

  JNIEnv* env = nullptr;
  if (jvm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) == JNI_OK) {
    return env;
  }

  if (jvm->AttachCurrentThread(&env, nullptr) == JNI_OK) {
    if (did_attach) {
      *did_attach = true;
    }
    return env;
  }
  return nullptr;
}

void CallProgressCallback(
    const std::shared_ptr<JniExtractionCallbackContext>& context,
    int64_t job_id,
    float progress) {
  if (!context || !context->jvm || !context->engine_object || !context->progress_method) {
    return;
  }

  bool did_attach = false;
  JNIEnv* env = GetEnvForCallback(context->jvm, &did_attach);
  if (!env) {
    return;
  }

  env->CallVoidMethod(context->engine_object, context->progress_method,
                      static_cast<jlong>(job_id), progress);
  if (env->ExceptionCheck()) {
    env->ExceptionClear();
  }

  if (did_attach) {
    context->jvm->DetachCurrentThread();
  }
}

jobject CreateExtractionResultMap(JNIEnv* env, const AudioEngine::ExtractionResult& result) {
  jclass hashMapClass = env->FindClass("java/util/HashMap");
  jmethodID hashMapInit = env->GetMethodID(hashMapClass, "<init>", "()V");
  jmethodID hashMapPut = env->GetMethodID(hashMapClass, "put",
      "(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;");

  jobject resultMap = env->NewObject(hashMapClass, hashMapInit);

  jclass booleanClass = env->FindClass("java/lang/Boolean");
  jmethodID booleanInit = env->GetMethodID(booleanClass, "<init>", "(Z)V");
  jobject successObj = env->NewObject(booleanClass, booleanInit,
                                      result.success ? JNI_TRUE : JNI_FALSE);
  env->CallObjectMethod(resultMap, hashMapPut, env->NewStringUTF("success"), successObj);

  env->CallObjectMethod(resultMap, hashMapPut, env->NewStringUTF("trackId"),
                        JNIHelper::StringToJString(env, result.track_id));

  env->CallObjectMethod(resultMap, hashMapPut, env->NewStringUTF("outputPath"),
                        JNIHelper::StringToJString(env, result.output_path));

  jclass longClass = env->FindClass("java/lang/Long");
  jmethodID longInit = env->GetMethodID(longClass, "<init>", "(J)V");
  jobject durationObj = env->NewObject(longClass, longInit, result.duration_samples);
  env->CallObjectMethod(resultMap, hashMapPut,
                        env->NewStringUTF("durationSamples"), durationObj);

  jobject fileSizeObj = env->NewObject(longClass, longInit, result.file_size);
  env->CallObjectMethod(resultMap, hashMapPut, env->NewStringUTF("fileSize"), fileSizeObj);

  env->CallObjectMethod(resultMap, hashMapPut, env->NewStringUTF("errorMessage"),
                        JNIHelper::StringToJString(env, result.error_message));

  return resultMap;
}

void CallCompletionCallback(
    const std::shared_ptr<JniExtractionCallbackContext>& context,
    int64_t job_id,
    const AudioEngine::ExtractionResult& result) {
  if (!context || !context->jvm || !context->engine_object || !context->completion_method) {
    return;
  }

  bool did_attach = false;
  JNIEnv* env = GetEnvForCallback(context->jvm, &did_attach);
  if (!env) {
    return;
  }

  jobject resultMap = CreateExtractionResultMap(env, result);
  env->CallVoidMethod(context->engine_object, context->completion_method,
                      static_cast<jlong>(job_id), resultMap);
  if (env->ExceptionCheck()) {
    env->ExceptionClear();
  }

  env->DeleteLocalRef(resultMap);
  env->DeleteGlobalRef(context->engine_object);

  if (did_attach) {
    context->jvm->DetachCurrentThread();
  }
}

}  // namespace

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

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* reserved [[maybe_unused]]) {
  g_java_vm = vm;
  return JNI_VERSION_1_6;
}

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

// Phase 2: Per-track effects
JNIEXPORT void JNICALL
Java_com_sezo_audioengine_AudioEngine_nativeSetTrackPitch(
    JNIEnv* env, jobject thiz [[maybe_unused]], jlong handle, jstring track_id, jfloat semitones) {
  auto* engine = reinterpret_cast<AudioEngine*>(handle);
  if (engine && track_id) {
    const char* id_chars = env->GetStringUTFChars(track_id, nullptr);
    std::string id_str(id_chars);
    env->ReleaseStringUTFChars(track_id, id_chars);
    engine->SetTrackPitch(id_str, semitones);
  }
}

JNIEXPORT jfloat JNICALL
Java_com_sezo_audioengine_AudioEngine_nativeGetTrackPitch(
    JNIEnv* env, jobject thiz [[maybe_unused]], jlong handle, jstring track_id) {
  auto* engine = reinterpret_cast<AudioEngine*>(handle);
  if (engine && track_id) {
    const char* id_chars = env->GetStringUTFChars(track_id, nullptr);
    std::string id_str(id_chars);
    env->ReleaseStringUTFChars(track_id, id_chars);
    return engine->GetTrackPitch(id_str);
  }
  return 0.0f;
}

JNIEXPORT void JNICALL
Java_com_sezo_audioengine_AudioEngine_nativeSetTrackSpeed(
    JNIEnv* env, jobject thiz [[maybe_unused]], jlong handle, jstring track_id, jfloat rate) {
  auto* engine = reinterpret_cast<AudioEngine*>(handle);
  if (engine && track_id) {
    const char* id_chars = env->GetStringUTFChars(track_id, nullptr);
    std::string id_str(id_chars);
    env->ReleaseStringUTFChars(track_id, id_chars);
    engine->SetTrackSpeed(id_str, rate);
  }
}

JNIEXPORT jfloat JNICALL
Java_com_sezo_audioengine_AudioEngine_nativeGetTrackSpeed(
    JNIEnv* env, jobject thiz [[maybe_unused]], jlong handle, jstring track_id) {
  auto* engine = reinterpret_cast<AudioEngine*>(handle);
  if (engine && track_id) {
    const char* id_chars = env->GetStringUTFChars(track_id, nullptr);
    std::string id_str(id_chars);
    env->ReleaseStringUTFChars(track_id, id_chars);
    return engine->GetTrackSpeed(id_str);
  }
  return 1.0f;
}

// Phase 3: Recording
JNIEXPORT jboolean JNICALL
Java_com_sezo_audioengine_AudioEngine_nativeStartRecording(
    JNIEnv* env, jobject thiz [[maybe_unused]], jlong handle, jstring output_path,
    jint sample_rate, jint channels, jstring format, jint bitrate, jint bits_per_sample) {

  auto* engine = reinterpret_cast<AudioEngine*>(handle);
  if (!engine) {
    return JNI_FALSE;
  }

  std::string output_path_str = JNIHelper::JStringToString(env, output_path);
  std::string format_str = JNIHelper::JStringToString(env, format);

  recording::RecordingConfig config;
  config.sample_rate = static_cast<int32_t>(sample_rate);
  config.channels = static_cast<int32_t>(channels);
  config.format = format_str;
  config.bitrate = static_cast<int32_t>(bitrate);
  config.bits_per_sample = static_cast<int32_t>(bits_per_sample);

  bool success = engine->StartRecording(output_path_str, config, nullptr);
  return success ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jobject JNICALL
Java_com_sezo_audioengine_AudioEngine_nativeStopRecording(
    JNIEnv* env, jobject thiz [[maybe_unused]], jlong handle) {

  auto* engine = reinterpret_cast<AudioEngine*>(handle);
  if (!engine) {
    return nullptr;
  }

  auto result = engine->StopRecording();

  // Create HashMap for result
  jclass hashMapClass = env->FindClass("java/util/HashMap");
  jmethodID hashMapInit = env->GetMethodID(hashMapClass, "<init>", "()V");
  jmethodID hashMapPut = env->GetMethodID(hashMapClass, "put",
      "(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;");

  jobject resultMap = env->NewObject(hashMapClass, hashMapInit);

  // Add success
  jclass booleanClass = env->FindClass("java/lang/Boolean");
  jmethodID booleanInit = env->GetMethodID(booleanClass, "<init>", "(Z)V");
  jobject successObj = env->NewObject(booleanClass, booleanInit,
                                      result.success ? JNI_TRUE : JNI_FALSE);
  env->CallObjectMethod(resultMap, hashMapPut, env->NewStringUTF("success"), successObj);

  // Add outputPath
  env->CallObjectMethod(resultMap, hashMapPut, env->NewStringUTF("outputPath"),
                        JNIHelper::StringToJString(env, result.output_path));

  // Add durationSamples
  jclass longClass = env->FindClass("java/lang/Long");
  jmethodID longInit = env->GetMethodID(longClass, "<init>", "(J)V");
  jobject durationObj = env->NewObject(longClass, longInit, result.duration_samples);
  env->CallObjectMethod(resultMap, hashMapPut,
                        env->NewStringUTF("durationSamples"), durationObj);

  // Add fileSize
  jobject fileSizeObj = env->NewObject(longClass, longInit, result.file_size);
  env->CallObjectMethod(resultMap, hashMapPut, env->NewStringUTF("fileSize"), fileSizeObj);

  // Add errorMessage if present
  if (!result.error_message.empty()) {
    env->CallObjectMethod(resultMap, hashMapPut, env->NewStringUTF("errorMessage"),
                          JNIHelper::StringToJString(env, result.error_message));
  }

  return resultMap;
}

JNIEXPORT jboolean JNICALL
Java_com_sezo_audioengine_AudioEngine_nativeIsRecording(
    JNIEnv* env [[maybe_unused]], jobject thiz [[maybe_unused]], jlong handle) {
  auto* engine = reinterpret_cast<AudioEngine*>(handle);
  if (engine) {
    return engine->IsRecording() ? JNI_TRUE : JNI_FALSE;
  }
  return JNI_FALSE;
}

JNIEXPORT jfloat JNICALL
Java_com_sezo_audioengine_AudioEngine_nativeGetInputLevel(
    JNIEnv* env [[maybe_unused]], jobject thiz [[maybe_unused]], jlong handle) {
  auto* engine = reinterpret_cast<AudioEngine*>(handle);
  if (engine) {
    return engine->GetInputLevel();
  }
  return 0.0f;
}

JNIEXPORT void JNICALL
Java_com_sezo_audioengine_AudioEngine_nativeSetRecordingVolume(
    JNIEnv* env [[maybe_unused]], jobject thiz [[maybe_unused]], jlong handle, jfloat volume) {
  auto* engine = reinterpret_cast<AudioEngine*>(handle);
  if (engine) {
    engine->SetRecordingVolume(volume);
  }
}

// Phase 6: Extraction
JNIEXPORT jobject JNICALL
Java_com_sezo_audioengine_AudioEngine_nativeExtractTrack(
    JNIEnv* env, jobject thiz [[maybe_unused]], jlong handle, jstring track_id, jstring output_path,
    jstring format, jint bitrate, jint bits_per_sample, jboolean include_effects) {

  auto* engine = reinterpret_cast<AudioEngine*>(handle);
  if (!engine) {
    return nullptr;
  }

  // Convert Java strings to C++ strings
  std::string track_id_str = JNIHelper::JStringToString(env, track_id);
  std::string output_path_str = JNIHelper::JStringToString(env, output_path);
  std::string format_str = JNIHelper::JStringToString(env, format);

  // Setup extraction options
  AudioEngine::ExtractionOptions options;
  options.format = format_str;
  options.bitrate = static_cast<int32_t>(bitrate);
  options.bits_per_sample = static_cast<int32_t>(bits_per_sample);
  options.include_effects = (include_effects == JNI_TRUE);

  // Setup progress callback
  jclass engine_class = env->GetObjectClass(thiz);
  jmethodID progress_method = env->GetMethodID(
      engine_class, "onNativeExtractionProgress", "(JF)V");

  AudioEngine::ExtractionProgressCallback progress_callback = nullptr;
  if (progress_method) {
    progress_callback = [env, thiz, progress_method](float progress) {
      env->CallVoidMethod(thiz, progress_method, static_cast<jlong>(0), progress);
    };
  } else {
    if (env->ExceptionCheck()) {
      env->ExceptionClear();
    }
    LOGE("Failed to find onNativeExtractionProgress");
  }

  // Perform extraction
  auto result = engine->ExtractTrack(
      track_id_str, output_path_str, options, progress_callback);

  return CreateExtractionResultMap(env, result);
}

JNIEXPORT jobject JNICALL
Java_com_sezo_audioengine_AudioEngine_nativeExtractAllTracks(
    JNIEnv* env, jobject thiz [[maybe_unused]], jlong handle, jstring output_path,
    jstring format, jint bitrate, jint bits_per_sample, jboolean include_effects) {

  auto* engine = reinterpret_cast<AudioEngine*>(handle);
  if (!engine) {
    return nullptr;
  }

  // Convert Java strings to C++ strings
  std::string output_path_str = JNIHelper::JStringToString(env, output_path);
  std::string format_str = JNIHelper::JStringToString(env, format);

  // Setup extraction options
  AudioEngine::ExtractionOptions options;
  options.format = format_str;
  options.bitrate = static_cast<int32_t>(bitrate);
  options.bits_per_sample = static_cast<int32_t>(bits_per_sample);
  options.include_effects = (include_effects == JNI_TRUE);

  // Setup progress callback
  jclass engine_class = env->GetObjectClass(thiz);
  jmethodID progress_method = env->GetMethodID(
      engine_class, "onNativeExtractionProgress", "(JF)V");

  AudioEngine::ExtractionProgressCallback progress_callback = nullptr;
  if (progress_method) {
    progress_callback = [env, thiz, progress_method](float progress) {
      env->CallVoidMethod(thiz, progress_method, static_cast<jlong>(0), progress);
    };
  } else {
    if (env->ExceptionCheck()) {
      env->ExceptionClear();
    }
    LOGE("Failed to find onNativeExtractionProgress");
  }

  // Perform extraction
  auto result = engine->ExtractAllTracks(
      output_path_str, options, progress_callback);

  return CreateExtractionResultMap(env, result);
}

JNIEXPORT jlong JNICALL
Java_com_sezo_audioengine_AudioEngine_nativeStartExtractTrack(
    JNIEnv* env, jobject thiz [[maybe_unused]], jlong handle, jstring track_id,
    jstring output_path, jstring format, jint bitrate, jint bits_per_sample,
    jboolean include_effects) {

  auto* engine = reinterpret_cast<AudioEngine*>(handle);
  if (!engine || !g_java_vm) {
    return 0;
  }

  std::string track_id_str = JNIHelper::JStringToString(env, track_id);
  std::string output_path_str = JNIHelper::JStringToString(env, output_path);
  std::string format_str = JNIHelper::JStringToString(env, format);

  AudioEngine::ExtractionOptions options;
  options.format = format_str;
  options.bitrate = static_cast<int32_t>(bitrate);
  options.bits_per_sample = static_cast<int32_t>(bits_per_sample);
  options.include_effects = (include_effects == JNI_TRUE);

  jclass engine_class = env->GetObjectClass(thiz);
  jmethodID progress_method = env->GetMethodID(
      engine_class, "onNativeExtractionProgress", "(JF)V");
  jmethodID completion_method = env->GetMethodID(
      engine_class, "onNativeExtractionComplete", "(JLjava/util/Map;)V");

  if (!progress_method && env->ExceptionCheck()) {
    env->ExceptionClear();
  }

  if (!completion_method) {
    if (env->ExceptionCheck()) {
      env->ExceptionClear();
    }
    LOGE("Failed to find onNativeExtractionComplete");
    return 0;
  }

  auto context = std::make_shared<JniExtractionCallbackContext>();
  context->jvm = g_java_vm;
  context->engine_object = env->NewGlobalRef(thiz);
  context->progress_method = progress_method;
  context->completion_method = completion_method;

  auto job_id_holder = std::make_shared<std::atomic<int64_t>>(0);

  auto progress_callback = [context, job_id_holder](float progress) {
    CallProgressCallback(context, job_id_holder->load(std::memory_order_acquire), progress);
  };

  auto completion_callback =
      [context, job_id_holder](int64_t job_id, const AudioEngine::ExtractionResult& result) {
        job_id_holder->store(job_id, std::memory_order_release);
        CallCompletionCallback(context, job_id, result);
      };

  int64_t job_id = engine->StartExtractTrack(
      track_id_str, output_path_str, options, progress_callback, completion_callback);
  job_id_holder->store(job_id, std::memory_order_release);

  if (job_id == 0) {
    env->DeleteGlobalRef(context->engine_object);
  }

  return job_id;
}

JNIEXPORT jlong JNICALL
Java_com_sezo_audioengine_AudioEngine_nativeStartExtractAllTracks(
    JNIEnv* env, jobject thiz [[maybe_unused]], jlong handle, jstring output_path,
    jstring format, jint bitrate, jint bits_per_sample, jboolean include_effects) {

  auto* engine = reinterpret_cast<AudioEngine*>(handle);
  if (!engine || !g_java_vm) {
    return 0;
  }

  std::string output_path_str = JNIHelper::JStringToString(env, output_path);
  std::string format_str = JNIHelper::JStringToString(env, format);

  AudioEngine::ExtractionOptions options;
  options.format = format_str;
  options.bitrate = static_cast<int32_t>(bitrate);
  options.bits_per_sample = static_cast<int32_t>(bits_per_sample);
  options.include_effects = (include_effects == JNI_TRUE);

  jclass engine_class = env->GetObjectClass(thiz);
  jmethodID progress_method = env->GetMethodID(
      engine_class, "onNativeExtractionProgress", "(JF)V");
  jmethodID completion_method = env->GetMethodID(
      engine_class, "onNativeExtractionComplete", "(JLjava/util/Map;)V");

  if (!progress_method && env->ExceptionCheck()) {
    env->ExceptionClear();
  }

  if (!completion_method) {
    if (env->ExceptionCheck()) {
      env->ExceptionClear();
    }
    LOGE("Failed to find onNativeExtractionComplete");
    return 0;
  }

  auto context = std::make_shared<JniExtractionCallbackContext>();
  context->jvm = g_java_vm;
  context->engine_object = env->NewGlobalRef(thiz);
  context->progress_method = progress_method;
  context->completion_method = completion_method;

  auto job_id_holder = std::make_shared<std::atomic<int64_t>>(0);

  auto progress_callback = [context, job_id_holder](float progress) {
    CallProgressCallback(context, job_id_holder->load(std::memory_order_acquire), progress);
  };

  auto completion_callback =
      [context, job_id_holder](int64_t job_id, const AudioEngine::ExtractionResult& result) {
        job_id_holder->store(job_id, std::memory_order_release);
        CallCompletionCallback(context, job_id, result);
      };

  int64_t job_id = engine->StartExtractAllTracks(
      output_path_str, options, progress_callback, completion_callback);
  job_id_holder->store(job_id, std::memory_order_release);

  if (job_id == 0) {
    env->DeleteGlobalRef(context->engine_object);
  }

  return job_id;
}

JNIEXPORT jboolean JNICALL
Java_com_sezo_audioengine_AudioEngine_nativeCancelExtraction(
    JNIEnv* env [[maybe_unused]], jobject thiz [[maybe_unused]],
    jlong handle, jlong job_id) {
  auto* engine = reinterpret_cast<AudioEngine*>(handle);
  if (!engine) {
    return JNI_FALSE;
  }
  return engine->CancelExtraction(job_id) ? JNI_TRUE : JNI_FALSE;
}

}  // extern "C"
