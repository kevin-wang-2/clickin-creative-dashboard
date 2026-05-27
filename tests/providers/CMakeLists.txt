add_executable(test_providers
    test_plugin_manifests.cpp
    test_local_file_plugin.cpp
)

target_link_libraries(test_providers PRIVATE
    clickin_core
    clickin_providers
    GTest::gtest_main
)

gtest_discover_tests(test_providers)
