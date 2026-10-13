#------------------------------------------------------------------------------
# CTS build options
#------------------------------------------------------------------------------
option(CTS_COMPONENT_BUILD "Build as dynamic components" OFF)
option(
    CTS_ENABLE_LTO
    "Enable Link-Time Optimization(LTO), Only for release build"
    ON
)
option(CTS_ENABLE_UNIT_TESTS "Enable unit test targets" ON)
option(CTS_ENABLE_COVERAGE "Enable code coverage (LLVM cov) for unit tests" OFF)
