add_library(provider_core_asset STATIC
    CoreAssetPlugin.cpp
)
target_link_libraries(provider_core_asset PUBLIC clickin_core)
