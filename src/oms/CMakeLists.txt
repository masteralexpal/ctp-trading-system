add_library(oms ${CTS_LIBRARY_TYPE})

target_sources(
    oms
    PRIVATE checked_order_sink.cc order_manager.cc
    PUBLIC FILE_SET HEADERS FILES checked_order_sink.h order_manager.h
)

target_link_libraries(
    oms
    PUBLIC cts::config cts::base cts::instrument cts::session cts::td
    PRIVATE third_party::spdlog
)

add_library(cts::oms ALIAS oms)

if(CTS_ENABLE_UNIT_TESTS)
    add_executable(
        oms_test
        tests/checked_order_sink_test.cc
        tests/order_manager_test.cc
    )
    target_link_libraries(oms_test PRIVATE cts::oms third_party::gtest)
    include(GoogleTest)
    gtest_discover_tests(oms_test)
endif()
