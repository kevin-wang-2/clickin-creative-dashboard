qt_add_library(clickin_ui_preview_host STATIC
    PreviewHost.cpp
    PreviewHost.h
)
target_link_libraries(clickin_ui_preview_host PUBLIC clickin_core Qt6::Widgets Qt6::Multimedia)
