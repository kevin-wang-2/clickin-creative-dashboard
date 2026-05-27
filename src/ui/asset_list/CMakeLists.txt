qt_add_library(clickin_ui_asset_list STATIC
    AssetListView.cpp
    AssetListView.h
)
target_link_libraries(clickin_ui_asset_list PUBLIC clickin_core Qt6::Widgets)
