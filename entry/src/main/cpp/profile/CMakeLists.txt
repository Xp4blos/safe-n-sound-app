# Dependency-free FFT frame analysis and sound description (SoundProfile). No OS dependencies.
file(GLOB SNS_PROFILE_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)
add_library(sns_profile STATIC ${SNS_PROFILE_SOURCES})
target_include_directories(sns_profile PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
set_target_properties(sns_profile PROPERTIES POSITION_INDEPENDENT_CODE ON)
