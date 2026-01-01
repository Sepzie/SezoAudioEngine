cmake_minimum_required(VERSION 3.18)
project(sezo_audio_engine)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Add Oboe library
add_subdirectory(third_party/oboe)

# Main audio engine library
add_library(sezo_audio_engine SHARED
  AudioEngine.cpp
  # Core components
  core/CircularBuffer.cpp
  core/MasterClock.cpp
  core/TransportController.cpp
  core/TimingManager.cpp
  # Audio decoding
  audio/AudioDecoder.cpp
  audio/MP3Decoder.cpp
  audio/WAVDecoder.cpp
  # Audio encoding
  audio/AACEncoder.cpp
  audio/MP3Encoder.cpp
  audio/WAVEncoder.cpp
  # Playback
  playback/Track.cpp
  playback/MultiTrackMixer.cpp
  playback/OboePlayer.cpp
  # Phase 2: Real-time effects
  playback/TimeStretch.cpp
  # Phase 3: Recording
  recording/MicrophoneCapture.cpp
  recording/RecordingPipeline.cpp
  # Phase 6: Extraction
  extraction/ExtractionPipeline.cpp
  # JNI bridge
  jni/AudioEngineJNI.cpp
)

# Include directories
target_include_directories(sezo_audio_engine PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party/dr_libs
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party/signalsmith-stretch
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party/signalsmith-linear/include
)

# Optional LAME MP3 encoder
find_path(LAME_INCLUDE_DIR lame/lame.h
  PATHS ${CMAKE_CURRENT_SOURCE_DIR}/third_party/lame/include)
find_library(LAME_LIBRARY NAMES mp3lame lame
  PATHS ${CMAKE_CURRENT_SOURCE_DIR}/third_party/lame/lib)

if (LAME_INCLUDE_DIR AND LAME_LIBRARY)
  target_include_directories(sezo_audio_engine PRIVATE ${LAME_INCLUDE_DIR})
  target_link_libraries(sezo_audio_engine ${LAME_LIBRARY})
  target_compile_definitions(sezo_audio_engine PRIVATE SEZO_ENABLE_LAME)
endif()

# Link libraries
target_link_libraries(sezo_audio_engine
  oboe
  android
  mediandk
  log
)

# Compiler flags for real-time audio
target_compile_options(sezo_audio_engine PRIVATE
  -Wall
  -Wextra
  -Werror
  -ffast-math
  -O3
)

option(SEZO_ENABLE_TESTS "Build Sezo Audio Engine tests" OFF)
if (SEZO_ENABLE_TESTS)
  add_subdirectory(
    ${CMAKE_CURRENT_SOURCE_DIR}/../../test/cpp
    ${CMAKE_CURRENT_BINARY_DIR}/sezo_tests
  )
endif()
