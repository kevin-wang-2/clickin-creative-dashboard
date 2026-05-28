qt_add_library(clickin_ui_inspector STATIC
    InspectorPanel.cpp
    InspectorPanel.h
)
target_link_libraries(clickin_ui_inspector PUBLIC clickin_core audio_domain Qt6::Widgets)
