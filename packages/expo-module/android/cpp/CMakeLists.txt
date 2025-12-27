cmake_minimum_required(VERSION 3.18)
project(expo_audio_engine_bridge)

add_library(expo_audio_engine_bridge SHARED
  NativeBridge.cpp
)

target_include_directories(expo_audio_engine_bridge PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
)
