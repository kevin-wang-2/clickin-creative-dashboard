add_executable(test_core
    capability/test_capability_broker.cpp
    db/test_database_layer.cpp
)

target_link_libraries(test_core PRIVATE
    clickin_core
    GTest::gtest_main
)

gtest_discover_tests(test_core)
