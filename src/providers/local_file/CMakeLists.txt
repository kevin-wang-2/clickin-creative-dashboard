add_library(provider_local_file STATIC
    LocalFilePlugin.cpp
)
target_link_libraries(provider_local_file PUBLIC clickin_core Qt6::Core)
