/**
 * @file md_api_ops.cc
 * @author lipingan (lipingan.dev@outlook.com)
 * @brief Narrow seam over CThostFtdcMdApi (concrete, non-virtual, unfakeable).
 */
#include "md/detail/md_api_ops.h"

#include <filesystem>

namespace cts::md::detail {

CtpMdApiOps::CtpMdApiOps(std::string flow_dir) {
  if (flow_dir.empty() || flow_dir.back() != '/') {
    flow_dir += '/';
  }
  std::filesystem::create_directories(flow_dir);
  api_ = CThostFtdcMdApi::CreateFtdcMdApi(flow_dir.c_str());
}

void CtpMdApiOps::Release() {
  api_->Release();
  api_ = nullptr;
}

}  // namespace cts::md::detail
