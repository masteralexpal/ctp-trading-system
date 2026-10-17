add_library(td ${CTS_LIBRARY_TYPE})

target_sources(
    td
    PRIVATE
        console_td_client.cc
        td_dispatcher.cc
        td_gateway.cc
        # detail/: boundary code, internal to td (exposes CTP types)
        detail/order_ctp.cc
        detail/order_ctp.h
        detail/td_api_ops.cc
        detail/td_api_ops.h
        detail/td_spi_adapter.cc
        detail/td_spi_adapter.h
    PUBLIC
        FILE_SET HEADERS
            FILES
                console_td_client.h
                order.h
                order_sink.h
                rate_table.h
                td_client.h
                td_delegate.h
                td_dispatcher.h
                td_gateway.h
                # detail/td_event.h rides along: td_dispatcher.h's queue member
                # needs the complete type. Still internal -- do not include
                # directly.
                detail/td_event.h
)

target_link_libraries(
    td
    # CTP is confined to td's .cc files; public headers are vendor-free
    PUBLIC cts::config cts::base cts::instrument
    PRIVATE third_party::ctp_trader third_party::spdlog
)

add_library(cts::td ALIAS td)

if(CTS_ENABLE_UNIT_TESTS)
    add_executable(td_test tests/order_ctp_test.cc tests/td_gateway_test.cc)
    target_link_libraries(
        td_test
        # ctp_trader is explicit here: tests feed raw CTP structs / fake the API
        PRIVATE cts::td third_party::ctp_trader third_party::gtest
    )
    include(GoogleTest)
    gtest_discover_tests(td_test)
endif()
