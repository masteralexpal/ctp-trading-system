add_library(metrics ${CTS_LIBRARY_TYPE})

target_sources(
    metrics
    PRIVATE latency_histogram.cc
    PUBLIC FILE_SET HEADERS FILES latency_histogram.h scoped_latency.h
)

target_link_libraries(metrics PUBLIC cts::config)

add_library(cts::metrics ALIAS metrics)

if(CTS_ENABLE_UNIT_TESTS)
    add_executable(
        metrics_test
        tests/latency_histogram_test.cc
        tests/scoped_latency_test.cc
    )
    target_link_libraries(metrics_test PRIVATE cts::metrics third_party::gtest)
    include(GoogleTest)
    gtest_discover_tests(metrics_test)
endif()
