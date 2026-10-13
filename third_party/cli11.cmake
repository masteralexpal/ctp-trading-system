find_package(CLI11 CONFIG REQUIRED)
add_library(third_party_cli11 INTERFACE)
target_link_libraries(third_party_cli11 INTERFACE CLI11::CLI11)
add_library(third_party::cli11 ALIAS third_party_cli11)
