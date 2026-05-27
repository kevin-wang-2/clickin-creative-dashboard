add_library(provider_local_audio STATIC
    LocalAudioPlugin.cpp
)
target_link_libraries(provider_local_audio PUBLIC clickin_core Qt6::Core Qt6::Multimedia)
