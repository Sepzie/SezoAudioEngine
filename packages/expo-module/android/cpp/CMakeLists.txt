cmake_minimum_required(VERSION 3.18)
project(expo_audio_engine_bridge)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# JNI bridge library
add_library(expo_audio_engine_bridge SHARED
  NativeBridge.cpp
)

# Include directories
target_include_directories(expo_audio_engine_bridge PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
)

# Link to android-engine and Android libs
target_link_libraries(expo_audio_engine_bridge
  sezo_audio_engine
  android
  log
)
