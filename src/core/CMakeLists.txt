add_library(clickin_core STATIC
    types/AssetId.cpp
    capability/CapabilityFuture.cpp
    capability/CapabilityRegistry.cpp
    capability/CapabilityBroker.cpp
    db/Database.cpp
    db/Migration.cpp
    db/CoreSchema.cpp
    services/DatabaseService.cpp
    services/AssetService.cpp
    services/MetadataService.cpp
    services/CacheService.cpp
    services/JobService.cpp
    services/SettingsService.cpp
    services/HierarchyService.cpp
    app/CoreContext.cpp
    app/Application.cpp
    app/PluginManager.cpp
)

target_include_directories(clickin_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../    # exposes core/ and sdk/ to dependants
)

target_link_libraries(clickin_core PUBLIC
    clickin_sdk
    sqlite3_lib
    Qt6::Core
)
