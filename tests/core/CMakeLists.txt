add_executable(test_core
    capability/test_capability_broker.cpp
)

target_link_libraries(test_core PRIVATE
    clickin_core
    GTest::gtest_main
)

gtest_discover_tests(test_core)
