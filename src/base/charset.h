/**
 * @file charset.h
 * @author lipingan (lipingan.dev@outlook.com)
 * @brief GBK <-> UTF-8 transcoding.
 */
#pragma once

#ifndef CTS_BASE_CHARSET_H_
#define CTS_BASE_CHARSET_H_

#include <string>
#include <string_view>

namespace cts::base {

std::string GbkToUtf8(std::string_view input);
std::string Utf8ToGbk(std::string_view input);

}  // namespace cts::base

#endif  // CTS_BASE_CHARSET_H_
