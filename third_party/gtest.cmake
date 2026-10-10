find_package(GTest CONFIG REQUIRED)
add_library(third_party_gtest INTERFACE)
target_link_libraries(
    third_party_gtest
    INTERFACE GTest::gtest GTest::gtest_main
)
add_library(third_party::gtest ALIAS third_party_gtest)
