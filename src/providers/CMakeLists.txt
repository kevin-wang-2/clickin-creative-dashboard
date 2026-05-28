add_subdirectory(audio)
add_subdirectory(builtin_search)
add_subdirectory(core_asset)
add_subdirectory(local_file)
add_subdirectory(local_audio)

# Aggregate all providers into one convenient target
add_library(clickin_providers INTERFACE)
target_link_libraries(clickin_providers INTERFACE
    provider_builtin_search
    provider_core_asset
    provider_local_file
    provider_local_audio
)
