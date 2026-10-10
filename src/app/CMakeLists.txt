add_executable(cts_app main.cc)

target_link_libraries(
    cts_app
    PRIVATE
        cts::config
        cts::base
        third_party::ctp
        third_party::fmt
)
