/**
 * @file string_utils.h
 * @author lipingan (lipingan.dev@outlook.com)
 * @brief Fixed-size char array utilities.
 */
#pragma once

#ifndef CTS_BASE_STRING_UTILS_H_
#define CTS_BASE_STRING_UTILS_H_

#include <cstddef>
#include <cstring>
#include <string>

namespace cts::base {

template <std::size_t N>
void CopyToFixedArray(char (&dst)[N], std::string const& src) {
  std::strncpy(dst, src.c_str(), N - 1);
  dst[N - 1] = '\0';
}

}  // namespace cts::base

#endif  // CTS_BASE_STRING_UTILS_H_
