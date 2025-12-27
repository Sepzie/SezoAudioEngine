cmake_minimum_required(VERSION 3.18)
project(sezo_audio_engine)

add_library(sezo_audio_engine SHARED
  AudioEngine.cpp
)

target_include_directories(sezo_audio_engine PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
)
