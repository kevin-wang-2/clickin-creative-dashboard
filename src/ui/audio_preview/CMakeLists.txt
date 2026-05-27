qt_add_library(clickin_ui_audio_preview STATIC
    AudioPreviewPanel.cpp
    AudioPreviewPanel.h
)
target_link_libraries(clickin_ui_audio_preview PUBLIC clickin_core Qt6::Widgets Qt6::Multimedia)
