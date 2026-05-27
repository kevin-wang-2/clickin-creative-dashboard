qt_add_library(clickin_ui_shell STATIC
    MainWindow.cpp
    MainWindow.h
)
target_link_libraries(clickin_ui_shell PUBLIC
    clickin_core
    clickin_providers
    clickin_ui_asset_list
    clickin_ui_plugin_mgmt
    clickin_ui_inspector
    clickin_ui_job_status
    clickin_ui_preview_host
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
)
