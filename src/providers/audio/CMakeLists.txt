add_library(audio_domain INTERFACE)
target_include_directories(audio_domain INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../..>
)
