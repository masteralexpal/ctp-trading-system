/**
 * @file version.h
 * @author lipingan (lipingan.dev@outlook.com)
 * @brief Version string.
 */
#pragma once

#ifndef CTS_BASE_VERSION_H_
#define CTS_BASE_VERSION_H_

#include <string_view>

#include "base/base_export.h"

namespace cts::base {

CTS_BASE_EXPORT std::string_view Version() noexcept;

}  // namespace cts::base

#endif  // CTS_BASE_VERSION_H_
