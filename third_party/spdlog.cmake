find_package(spdlog CONFIG REQUIRED)
add_library(third_party_spdlog INTERFACE)
target_link_libraries(third_party_spdlog INTERFACE spdlog::spdlog)
add_library(third_party::spdlog ALIAS third_party_spdlog)
