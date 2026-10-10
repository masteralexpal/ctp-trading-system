# CTP v6.7.11 预编译库（上期技术，闭源，vendored 于 third_party/ctp，非 vcpkg 提供）
#
# 注意：官方 .so 没有 lib 前缀、也没有 SONAME，因此：
#   1. 链接时必须给绝对路径（DT_NEEDED 会记录该路径）；
#   2. 额外注入 rpath，保证构建产物在构建机上任意 CWD 下都能找到库。
# 部署到其它机器时，应把 .so 随可执行文件一起分发并改用 $ORIGIN rpath。

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
# SYSTEM: CTP 头文件不参与 -Wall -Wextra 警告检查
target_include_directories(third_party_ctp_md SYSTEM INTERFACE ${CTP_ROOT}/include)
target_link_libraries(third_party_ctp_md INTERFACE ctp_md)
target_link_options(third_party_ctp_md INTERFACE -Wl,-rpath,${CTP_ROOT}/lib)
add_library(third_party::ctp_md ALIAS third_party_ctp_md)

add_library(third_party_ctp_trader INTERFACE)
target_include_directories(third_party_ctp_trader SYSTEM INTERFACE ${CTP_ROOT}/include)
target_link_libraries(third_party_ctp_trader INTERFACE ctp_trader)
target_link_options(third_party_ctp_trader INTERFACE -Wl,-rpath,${CTP_ROOT}/lib)
add_library(third_party::ctp_trader ALIAS third_party_ctp_trader)

# 行情 + 交易都要时的便捷入口
add_library(third_party_ctp INTERFACE)
target_link_libraries(
    third_party_ctp
    INTERFACE third_party_ctp_md third_party_ctp_trader
)
add_library(third_party::ctp ALIAS third_party_ctp)
