#include "base/version.h"

#include <fmt/format.h>

#include "ThostFtdcMdApi.h"
#include "ThostFtdcTraderApi.h"

int main() {
    fmt::print("ctp-trading-system v{}\n", cts::base::Version());

    // 仅取函数地址以强制链接器保留对 CTP 库的符号引用，验证链接通路；
    // 不在此处调用，避免在 smoke 阶段执行柜台交互。
    auto md_version_fn = &CThostFtdcMdApi::GetApiVersion;
    auto trader_version_fn = &CThostFtdcTraderApi::GetApiVersion;
    fmt::print("CTP MdApi::GetApiVersion     @ {}\n", fmt::ptr(md_version_fn));
    fmt::print("CTP TraderApi::GetApiVersion @ {}\n", fmt::ptr(trader_version_fn));

    return 0;
}
