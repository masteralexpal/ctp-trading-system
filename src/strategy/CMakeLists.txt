add_library(strategy ${CTS_LIBRARY_TYPE})

target_sources(
    strategy
    PRIVATE sma_cross_strategy.cc
    PUBLIC FILE_SET HEADERS FILES sma_cross_strategy.h strategy.h
)

target_link_libraries(
    strategy
    PUBLIC cts::config cts::md cts::td
    PRIVATE third_party::spdlog
)

add_library(cts::strategy ALIAS strategy)

if(CTS_ENABLE_UNIT_TESTS)
    add_executable(strategy_test tests/sma_cross_strategy_test.cc)
    target_link_libraries(
        strategy_test
        PRIVATE cts::strategy third_party::gtest
    )
    include(GoogleTest)
    gtest_discover_tests(strategy_test)
endif()
