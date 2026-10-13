add_library(instrument ${CTS_LIBRARY_TYPE})

target_sources(
    instrument
    PRIVATE instrument_master.cc
    PUBLIC
        FILE_SET HEADERS FILES instrument.h instrument_ctp.h instrument_master.h
)

target_link_libraries(
    instrument
    # ctp_md must be PUBLIC: instrument_ctp.h exposes the CTP POD header
    PUBLIC cts::config third_party::ctp_md
)

if(CTS_ENABLE_UNIT_TESTS)
    add_executable(instrument_test tests/instrument_master_test.cc)
    target_link_libraries(
        instrument_test
        PRIVATE cts::instrument third_party::gtest
    )
    include(GoogleTest)
    gtest_discover_tests(instrument_test)
endif()

add_library(cts::instrument ALIAS instrument)
