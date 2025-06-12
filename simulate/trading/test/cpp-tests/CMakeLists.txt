find_package(GTest CONFIG REQUIRED)

set(test_sources "")

include(CollectTestSources)

add_subdirectory(test-common)
add_subdirectory(accounting)
add_subdirectory(decimal)
add_subdirectory(message)

add_executable(taosim-tests
    ${test_sources}
    LimitedDequeTests.cpp
    SubscriptionRegistryTests.cpp
    UtilTests.cpp
    SelfTradePreventionTests.cpp
    QuoteBasedOrderTests.cpp
)

target_compile_definitions(taosim-tests PRIVATE "${internal_compile_definitions}")

target_include_directories(taosim-tests
    PRIVATE
        "${src_dir}/net"
        "${src_dir}/util"
        test-common
)

target_link_libraries(taosim-tests
    PRIVATE 
        GTest::gtest
        GTest::gtest_main
        GTest::gmock
        GTest::gmock_main
        taosim::test::common
        taosim::simulation
)

add_test(Tests taosim-tests)
