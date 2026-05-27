qt_add_library(clickin_ui_plugin_mgmt STATIC
    PluginManagementView.cpp
    PluginManagementView.h
)
target_link_libraries(clickin_ui_plugin_mgmt PUBLIC clickin_core Qt6::Widgets)
