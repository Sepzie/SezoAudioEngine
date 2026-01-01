cmake_minimum_required(VERSION 3.18)
project(sezo_audio_engine_tests)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(SEZO_ENGINE_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../main/cpp")
set(SEZO_GTEST_ROOT "${SEZO_ENGINE_ROOT}/third_party/googletest")

if (NOT EXISTS "${SEZO_GTEST_ROOT}/CMakeLists.txt")
  message(FATAL_ERROR "GoogleTest not found at ${SEZO_GTEST_ROOT}. Did you vendor it?")
endif()

add_subdirectory("${SEZO_GTEST_ROOT}" "${CMAKE_BINARY_DIR}/googletest")

file(GLOB SEZO_TEST_SOURCES CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_LIST_DIR}/tests/*.cpp"
)

set(SEZO_ENGINE_SOURCES
  "${SEZO_ENGINE_ROOT}/core/CircularBuffer.cpp"
  "${SEZO_ENGINE_ROOT}/core/MasterClock.cpp"
  "${SEZO_ENGINE_ROOT}/core/TransportController.cpp"
  "${SEZO_ENGINE_ROOT}/core/TimingManager.cpp"
  "${SEZO_ENGINE_ROOT}/audio/AudioDecoder.cpp"
  "${SEZO_ENGINE_ROOT}/audio/MP3Decoder.cpp"
  "${SEZO_ENGINE_ROOT}/audio/WAVDecoder.cpp"
  "${SEZO_ENGINE_ROOT}/playback/TimeStretch.cpp"
  "${SEZO_ENGINE_ROOT}/playback/Track.cpp"
  "${SEZO_ENGINE_ROOT}/playback/MultiTrackMixer.cpp"
)

add_executable(sezo_engine_tests
  ${SEZO_TEST_SOURCES}
  ${SEZO_ENGINE_SOURCES}
)

if (NOT ANDROID)
  target_include_directories(sezo_engine_tests PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/stubs"
  )
endif()

target_include_directories(sezo_engine_tests PRIVATE
  "${SEZO_ENGINE_ROOT}"
  "${SEZO_ENGINE_ROOT}/third_party/dr_libs"
  "${SEZO_ENGINE_ROOT}/third_party/signalsmith-stretch"
  "${SEZO_ENGINE_ROOT}/third_party/signalsmith-linear/include"
)

find_package(Threads REQUIRED)

target_link_libraries(sezo_engine_tests
  gtest
  gtest_main
  Threads::Threads
)

if (ANDROID)
  target_link_libraries(sezo_engine_tests log)
endif()

enable_testing()
add_test(NAME sezo_engine_tests COMMAND sezo_engine_tests)
