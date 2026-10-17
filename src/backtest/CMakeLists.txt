add_library(backtest ${CTS_LIBRARY_TYPE})

target_sources(
    backtest
    PRIVATE backtest.cc sim_order_sink.cc
    PUBLIC FILE_SET HEADERS FILES backtest.h sim_order_sink.h
)

target_link_libraries(
    backtest
    PUBLIC cts::config cts::md cts::oms cts::recorder cts::strategy cts::td
)

add_library(cts::backtest ALIAS backtest)

add_executable(cts_backtest main.cc)

target_link_libraries(
    cts_backtest
    PRIVATE cts::backtest cts::config third_party::cli11 third_party::spdlog
)

if(CTS_ENABLE_UNIT_TESTS)
    add_executable(
        backtest_test tests/backtest_test.cc tests/sim_order_sink_test.cc
    )
    target_link_libraries(
        backtest_test
        PRIVATE cts::backtest third_party::ctp_md third_party::gtest
    )
    include(GoogleTest)
    gtest_discover_tests(backtest_test)
endif()
