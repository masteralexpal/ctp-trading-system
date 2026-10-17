/**
 * @file string_utils.h
 * @author lipingan (lipingan.dev@outlook.com)
 * @brief Fixed-size char array utilities.
 */
#pragma once

#ifndef CTS_BASE_STRING_UTILS_H_
#define CTS_BASE_STRING_UTILS_H_

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <string_view>

namespace cts::base {

template <std::size_t N>
void CopyToFixedArray(char (&dst)[N], std::string_view src) {
  auto const n = std::min(src.size(), N - 1);
  std::memcpy(dst, src.data(), n);
  std::memset(dst + n, 0, N - n);  // strncpy semantics: NUL-pad the tail
}

}  // namespace cts::base

#endif  // CTS_BASE_STRING_UTILS_H_
