add_library(base ${CTS_LIBRARY_TYPE})

target_sources(
    base
    PRIVATE charset.cc shutdown.cc task/task_loop.cc time_utils.cc version.cc
    PUBLIC
        FILE_SET HEADERS
            FILES
                base_export.h
                charset.h
                iconv_converter.h
                latest_spsc_queue.h
                string_utils.h
                no_destructor.h
                shutdown.h
                spsc_queue.h
                task/task.h
                task/task_loop.h
                thread.h
                time_utils.h
                version.h
)

if(CTS_COMPONENT_BUILD)
    target_compile_definitions(base PRIVATE BASE_IMPLEMENTATION)
endif()

target_link_libraries(base PUBLIC cts::config)

if(CTS_ENABLE_UNIT_TESTS)
    add_executable(
        base_test
        tests/charset_test.cc
        tests/latest_spsc_queue_test.cc
        tests/no_destructor_test.cc
        tests/shutdown_test.cc
        tests/spsc_queue_test.cc
        tests/string_utils_test.cc
        tests/task_loop_test.cc
        tests/thread_test.cc
        tests/time_utils_test.cc
    )
    target_link_libraries(base_test PRIVATE cts::base third_party::gtest)
    include(GoogleTest)
    gtest_discover_tests(base_test)
endif()

add_library(cts::base ALIAS base)
