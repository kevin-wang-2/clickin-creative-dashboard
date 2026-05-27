# SDK is header-only — consumed by core and all providers
add_library(clickin_sdk INTERFACE)

target_include_directories(clickin_sdk INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/../    # allows #include "sdk/IPlugin.h"
)
