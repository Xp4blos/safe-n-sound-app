cmake_minimum_required(VERSION 3.5.0)
project(safensound)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_subdirectory(ambient)   # teammate's engine (static lib ambient_core)
add_subdirectory(profile)   # FFT frame analysis and SoundProfile
add_subdirectory(wrapper)   # SoundEngine

add_library(safensound SHARED napi/napi_init.cpp)
target_link_libraries(safensound PUBLIC sns_wrapper libace_napi.z.so libhilog_ndk.z.so)
