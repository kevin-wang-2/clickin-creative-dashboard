add_library(provider_builtin_search STATIC
    BuiltinSearchPlugin.cpp
)
target_link_libraries(provider_builtin_search PUBLIC clickin_core Qt6::Core)
