add_executable(test_providers
    test_plugin_manifests.cpp
)

target_link_libraries(test_providers PRIVATE
    clickin_providers
    GTest::gtest_main
)

gtest_discover_tests(test_providers)
