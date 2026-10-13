/**
 * @file version.cc
 * @author lipingan (lipingan.dev@outlook.com)
 * @brief Version string.
 */
#include "base/version.h"

namespace cts::base {

std::string_view Version() noexcept {
  return "0.1.0";
}

}  // namespace cts::base
