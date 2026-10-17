add_executable(cts_app app_config.cc cli.cc console.cc main.cc)

target_link_libraries(
    cts_app
    PRIVATE
        cts::config
        cts::base
        cts::md
        cts::oms
        cts::strategy
        cts::td
        third_party::cli11
        third_party::spdlog
)
