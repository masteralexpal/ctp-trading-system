add_library(md ${CTS_LIBRARY_TYPE})

target_sources(
    md
    PRIVATE
        console_md_client.cc
        md_dispatcher.cc
        md_gateway.cc
        # detail/: boundary code, internal to md (exposes CTP types)
        detail/md_api_ops.cc
        detail/md_api_ops.h
        detail/md_spi_adapter.cc
        detail/md_spi_adapter.h
        detail/tick_ctp.cc
        detail/tick_ctp.h
    PUBLIC
        FILE_SET HEADERS
            FILES
                console_md_client.h
                md_delegate.h
                md_dispatcher.h
                md_gateway.h
                md_client.h
                # The raw tap interface stays here: MdSpiAdapter fires it.
                # The journal format + recorder live in src/recorder.
                raw_tick_sink.h
                tick.h
                # detail/md_event.h rides along: md_dispatcher.h's queue member
                # needs the complete type. Still internal -- do not include
                # directly.
                detail/md_event.h
)

target_link_libraries(
    md
    # raw_tick_sink.h exposes the verbatim CTP wire struct, so ctp_md is
    # PUBLIC; normalized consumers still never see CTP types
    PUBLIC cts::config cts::base third_party::ctp_md
    PRIVATE third_party::spdlog
)

add_library(cts::md ALIAS md)

if(CTS_ENABLE_UNIT_TESTS)
    add_executable(
        md_test
        tests/md_dispatcher_test.cc
        tests/md_gateway_test.cc
        tests/tick_ctp_test.cc
    )
    target_link_libraries(
        md_test
        # ctp_md is explicit here: tests feed raw CTP structs / fake the API
        PRIVATE cts::md third_party::ctp_md third_party::gtest
    )
    include(GoogleTest)
    gtest_discover_tests(md_test)
endif()
