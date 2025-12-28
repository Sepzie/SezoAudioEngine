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
  audio/WAVEncoder.cpp
  # Playback
  playback/Track.cpp
  playback/MultiTrackMixer.cpp
  playback/OboePlayer.cpp
  # Phase 2: Real-time effects
  playback/TimeStretch.cpp
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

# Link libraries
target_link_libraries(sezo_audio_engine
  oboe
  android
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
