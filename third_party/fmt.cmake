find_package(fmt CONFIG REQUIRED)
add_library(third_party_fmt INTERFACE)
target_link_libraries(third_party_fmt INTERFACE fmt::fmt)
add_library(third_party::fmt ALIAS third_party_fmt)
