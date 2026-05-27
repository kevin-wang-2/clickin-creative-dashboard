add_library(provider_local_audio STATIC
    LocalAudioPlugin.cpp
    AudioPreviewWidget.cpp
    AudioPreviewWidget.h
)
set_target_properties(provider_local_audio PROPERTIES AUTOMOC ON)
target_link_libraries(provider_local_audio PUBLIC
    clickin_core
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Multimedia
)
