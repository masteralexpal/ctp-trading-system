add_library(md ${CTS_LIBRARY_TYPE})

target_sources(
    md
    PRIVATE console_md_listener.cc md_dispatcher.cc md_gateway.cc
            # detail/: boundary code, internal to md (exposes CTP types)
            detail/md_spi_adapter.cc detail/md_spi_adapter.h
            detail/tick_ctp.cc detail/tick_ctp.h
    PUBLIC
        FILE_SET HEADERS
            FILES
                console_md_listener.h
                md_delegate.h
                md_dispatcher.h
                md_gateway.h
                md_listener.h
                tick.h
                # detail/md_event.h rides along: md_dispatcher.h's queue member
                # needs the complete type. Still internal -- do not include
                # directly.
                detail/md_event.h
)

target_link_libraries(
    md
    # CTP is confined to md's .cc files; public headers are vendor-free
    PUBLIC cts::config cts::base
    PRIVATE third_party::ctp_md third_party::spdlog
)

add_library(cts::md ALIAS md)

if(CTS_ENABLE_UNIT_TESTS)
    add_executable(md_test tests/md_dispatcher_test.cc tests/tick_ctp_test.cc)
    target_link_libraries(
        md_test
        # ctp_md is explicit here: tick_ctp_test feeds raw CTP structs
        PRIVATE cts::md third_party::ctp_md third_party::gtest
    )
    include(GoogleTest)
    gtest_discover_tests(md_test)
endif()
