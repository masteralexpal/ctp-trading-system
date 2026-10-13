cmake_minimum_required(VERSION 3.20)

project(ctp_trading_system LANGUAGES C CXX)

#------------------------------------------------------------------------------
# Options
#------------------------------------------------------------------------------
include(cmake/cts_options.cmake)

#------------------------------------------------------------------------------
# Language
#------------------------------------------------------------------------------
set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

#------------------------------------------------------------------------------
# Platform
#------------------------------------------------------------------------------
message(STATUS "[cts] SystemName: ${CMAKE_SYSTEM_NAME}")
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(
        FATAL_ERROR
        "[cts] Unsupported platform: ${CMAKE_SYSTEM_NAME}. Only Linux is supported."
    )
endif()

#------------------------------------------------------------------------------
# Compiler
#------------------------------------------------------------------------------
set(CTS_IS_GNU FALSE)
set(CTS_IS_CLANG FALSE)

message(STATUS "[cts] CompilerID: ${CMAKE_CXX_COMPILER_ID}")
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    set(CTS_IS_GNU TRUE)
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    set(CTS_IS_CLANG TRUE)
else()
    message(
        FATAL_ERROR
        "[cts] Unknown compiler ID: ${CMAKE_CXX_COMPILER_ID}. Only GNU and Clang are supported."
    )
endif()

#------------------------------------------------------------------------------
# Compiler driver style
#------------------------------------------------------------------------------
message(STATUS "[cts] CompilerFrontend: ${CMAKE_CXX_COMPILER_FRONTEND_VARIANT}")
if(NOT CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "GNU")
    message(
        FATAL_ERROR
        "[cts] Unsupported compiler driver: ${CMAKE_CXX_COMPILER_FRONTEND_VARIANT}. "
        "Only GNU driver is supported."
    )
endif()

# LTO
if(CTS_ENABLE_LTO)
    include(CheckIPOSupported)

    check_ipo_supported(RESULT ipo_supported)

    message(STATUS "[cts] LTO supported: ${ipo_supported}")
    if(ipo_supported)
        set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
        set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELWITHDEBINFO TRUE)
    endif() # ipo_supported
endif() # CTS_ENABLE_LTO

# Component build
if(CTS_COMPONENT_BUILD)
    set(CTS_LIBRARY_TYPE SHARED)
else()
    set(CTS_LIBRARY_TYPE STATIC)
endif() # CTS_COMPONENT_BUILD

#------------------------------------------------------------------------------
# CTS global config interface
#------------------------------------------------------------------------------
set(CTS_ROOT ${CMAKE_SOURCE_DIR})
add_library(cts_config INTERFACE)
target_include_directories(cts_config INTERFACE ${CTS_ROOT}/src)

target_compile_options(
    cts_config
    INTERFACE -Wall -Wextra -Wpedantic -Wshadow -fvisibility=hidden
)
target_link_options(cts_config INTERFACE -fuse-ld=lld)

if(CTS_COMPONENT_BUILD)
    target_compile_definitions(cts_config INTERFACE CTS_COMPONENT_BUILD)
endif()

# Unit test specific: suppress self-move and self-assign warnings.
if(CTS_ENABLE_UNIT_TESTS)
    target_compile_options(
        cts_config
        INTERFACE -Wno-self-move -Wno-self-assign-overloaded
    )
endif()

# Code coverage (LLVM source-based coverage)
if(CTS_ENABLE_COVERAGE AND CTS_IS_CLANG)
    message(STATUS "[cts] Enabling LLVM source-based code coverage")
    target_compile_options(
        cts_config
        INTERFACE -fprofile-instr-generate -fcoverage-mapping
    )
    target_link_options(
        cts_config
        INTERFACE -fprofile-instr-generate -fcoverage-mapping
    )
endif()

add_library(cts::config ALIAS cts_config)

enable_testing()

#------------------------------------------------------------------------------
# Subdirectories
#------------------------------------------------------------------------------
add_subdirectory(third_party)
add_subdirectory(src)
