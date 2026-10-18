add_library(session ${CTS_LIBRARY_TYPE})

target_sources(
    session
    PRIVATE session.cc
    PUBLIC
        FILE_SET HEADERS FILES session.h
)

target_link_libraries(
    session
    PUBLIC cts::base
)

add_library(cts::session ALIAS session)

if(CTS_ENABLE_UNIT_TESTS)
    add_executable(session_test tests/session_test.cc)
    target_link_libraries(
        session_test
        PRIVATE cts::session third_party::gtest
    )
    include(GoogleTest)
    gtest_discover_tests(session_test)
endif()
