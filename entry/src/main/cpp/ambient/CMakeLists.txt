# Teammate's ambient sound engine (see README.md in this folder). Library only; its tests and wav_cli are
# built by tests/CMakeLists.txt on the PC.
find_package(Threads)
add_library(ambient_core STATIC src/fft.cpp src/detector.cpp src/custom.cpp src/trainer.cpp src/speech.cpp)
target_include_directories(ambient_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(ambient_core PUBLIC cxx_std_17)
set_target_properties(ambient_core PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(Threads_FOUND)
  target_link_libraries(ambient_core PUBLIC Threads::Threads)
endif()
