qt_add_library(clickin_ui_discover STATIC
    DiscoverManagerDialog.cpp
    DiscoverManagerDialog.h
)
target_link_libraries(clickin_ui_discover PUBLIC
    clickin_core
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
)
