# Thin application layer over the ambient engine: ring buffer, live spectrum, learn/train, SoundProfile.
file(GLOB SNS_WRAPPER_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)
add_library(sns_wrapper STATIC ${SNS_WRAPPER_SOURCES})
target_include_directories(sns_wrapper PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(sns_wrapper PUBLIC ambient_core sns_profile)
set_target_properties(sns_wrapper PROPERTIES POSITION_INDEPENDENT_CODE ON)
