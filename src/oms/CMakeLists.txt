add_library(oms ${CTS_LIBRARY_TYPE})

target_sources(
    oms
    PRIVATE checked_order_sink.cc order_manager.cc
    PUBLIC FILE_SET HEADERS FILES checked_order_sink.h oms_listener.h
                                  order_manager.h
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
        tests/order_manager_property_test.cc
        tests/order_manager_test.cc
    )
    target_link_libraries(oms_test PRIVATE cts::oms third_party::gtest)
    include(GoogleTest)
    gtest_discover_tests(oms_test)
endif()

if(CTS_ENABLE_FUZZERS)
    if(NOT CTS_IS_CLANG)
        message(FATAL_ERROR "[cts] fuzzers require Clang (-fsanitize=fuzzer)")
    endif()
    # order_manager.cc is compiled INTO the harness so coverage guidance and
    # sanitizers reach the fold logic itself; component libs resolve the rest
    add_executable(oms_fuzz tests/order_manager_fuzz.cc order_manager.cc)
    target_link_libraries(
        oms_fuzz
        PRIVATE cts::config cts::base cts::instrument cts::session cts::td
                third_party::spdlog
    )
    target_compile_options(
        oms_fuzz PRIVATE -fsanitize=fuzzer,address,undefined -g
    )
    target_link_options(
        oms_fuzz PRIVATE -fsanitize=fuzzer,address,undefined
    )
endif()
