/**
 * @file td_api_ops.cc
 * @author lipingan (lipingan.dev@outlook.com)
 * @brief Narrow seam over CThostFtdcTraderApi (concrete, non-virtual,
 *        unfakeable).
 */
#include "td/detail/td_api_ops.h"

#include <filesystem>

namespace cts::td::detail {

CtpTdApiOps::CtpTdApiOps(std::string flow_dir) {
  if (flow_dir.empty() || flow_dir.back() != '/') {
    flow_dir += '/';
  }
  std::filesystem::create_directories(flow_dir);
  api_ = CThostFtdcTraderApi::CreateFtdcTraderApi(flow_dir.c_str());
}

void CtpTdApiOps::Release() {
  api_->Release();
  api_ = nullptr;
}

}  // namespace cts::td::detail
