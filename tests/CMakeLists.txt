include(GoogleTest)

add_executable(test_config test_config.cpp)
target_link_libraries(test_config PRIVATE obscuragateway GTest::gtest GTest::gtest_main)
gtest_discover_tests(test_config)

add_executable(test_router test_router.cpp)
target_link_libraries(test_router PRIVATE obscuragateway GTest::gtest GTest::gtest_main)
gtest_discover_tests(test_router)

add_executable(test_internal_protocol test_internal_protocol.cpp)
target_link_libraries(test_internal_protocol PRIVATE obscuragateway GTest::gtest GTest::gtest_main)
gtest_discover_tests(test_internal_protocol)
