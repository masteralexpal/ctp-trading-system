# CTP v6.7.11 prebuilt libraries (Sfit, closed source, vendored in third_party/ctp; not from vcpkg)
#
# Note: the official .so files have no lib prefix and no SONAME, so:
#   1. link with absolute paths (DT_NEEDED records them);
#   2. inject rpath so the artifacts find the libraries from any CWD on this machine.
# For deployment elsewhere, ship the .so files next to the binary and switch to $ORIGIN rpath.

set(CTP_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/ctp)

#------------------------------------------------------------------------------
# Imported libraries
#------------------------------------------------------------------------------
add_library(ctp_md SHARED IMPORTED GLOBAL)
set_target_properties(
    ctp_md
    PROPERTIES IMPORTED_LOCATION ${CTP_ROOT}/lib/thostmduserapi_se.so
)

add_library(ctp_trader SHARED IMPORTED GLOBAL)
set_target_properties(
    ctp_trader
    PROPERTIES IMPORTED_LOCATION ${CTP_ROOT}/lib/thosttraderapi_se.so
)

#------------------------------------------------------------------------------
# Namespace-isolated wrappers
#------------------------------------------------------------------------------
add_library(third_party_ctp_md INTERFACE)
# SYSTEM: keep CTP headers out of -Wall -Wextra diagnostics
target_include_directories(
    third_party_ctp_md
    SYSTEM
    INTERFACE ${CTP_ROOT}/include
)
target_link_libraries(third_party_ctp_md INTERFACE ctp_md)
target_link_options(third_party_ctp_md INTERFACE -Wl,-rpath,${CTP_ROOT}/lib)
add_library(third_party::ctp_md ALIAS third_party_ctp_md)

add_library(third_party_ctp_trader INTERFACE)
target_include_directories(
    third_party_ctp_trader
    SYSTEM
    INTERFACE ${CTP_ROOT}/include
)
target_link_libraries(third_party_ctp_trader INTERFACE ctp_trader)
target_link_options(third_party_ctp_trader INTERFACE -Wl,-rpath,${CTP_ROOT}/lib)
add_library(third_party::ctp_trader ALIAS third_party_ctp_trader)

# Convenience entry for both md and trader
add_library(third_party_ctp INTERFACE)
target_link_libraries(
    third_party_ctp
    INTERFACE third_party_ctp_md third_party_ctp_trader
)
add_library(third_party::ctp ALIAS third_party_ctp)
