# Raw tick journal: the on-disk format library (raw_tick_file) + the sink
# implementation (raw_tick_recorder). Kept out of md so that replay/ETL tools
# link the format without pulling in the live gateway.

add_library(recorder ${CTS_LIBRARY_TYPE})

target_sources(
    recorder
    PRIVATE raw_tick_file.cc raw_tick_recorder.cc
    PUBLIC FILE_SET HEADERS FILES raw_tick_file.h raw_tick_recorder.h
)

target_link_libraries(
    recorder
    # Implements md::RawTickSink; the format embeds the CTP wire struct
    PUBLIC cts::config cts::base cts::md
    PRIVATE third_party::spdlog
)

add_library(cts::recorder ALIAS recorder)

add_executable(cts_recorder main.cc)

target_link_libraries(
    cts_recorder
    PRIVATE cts::config cts::base cts::md cts::recorder third_party::cli11
            third_party::spdlog
)

if(CTS_ENABLE_UNIT_TESTS)
    add_executable(
        recorder_test raw_tick_file_test.cc raw_tick_recorder_test.cc
    )
    target_link_libraries(
        recorder_test
        # ctp_md is explicit: tests fabricate raw CTP structs by hand
        PRIVATE cts::recorder third_party::ctp_md third_party::gtest
    )
    include(GoogleTest)
    gtest_discover_tests(recorder_test)
endif()
