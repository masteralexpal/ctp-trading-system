/**
 * @file iconv_converter.h
 * @author lipingan (lipingan.dev@outlook.com)
 * @brief RAII wrapper of POSIX iconv.
 */
#pragma once

#ifndef CTS_BASE_ICONV_CONVERTER_H_
#define CTS_BASE_ICONV_CONVERTER_H_

#include <iconv.h>

#include <optional>
#include <string>
#include <string_view>

namespace cts::base {

// RAII wrapper of the iconv charset converter (POSIX, built into glibc).
//
// Contract: a Create-produced object is always valid (failure returns nullopt;
// our own code never throws). Non-copyable, movable.
// Note: one instance must not be shared across threads (iconv holds shift
// state) -- use thread_local instances when concurrency is needed.
class IconvConverter {
 public:
  // to/from are iconv charset names ("UTF-8", "GBK", "GB18030", ...)
  static std::optional<IconvConverter> Create(std::string_view to,
                                              std::string_view from) {
    std::string const to_s{to};  // iconv_open wants NUL-terminated names
    std::string const from_s{from};
    iconv_t const cd = iconv_open(to_s.c_str(), from_s.c_str());
    if (cd == kInvalid) {
      return std::nullopt;
    }
    IconvConverter converter;
    converter.cd_ = cd;
    return converter;
  }

  ~IconvConverter() {
    if (cd_ != kInvalid) {
      iconv_close(cd_);
    }
  }

  IconvConverter(IconvConverter const&) = delete;
  IconvConverter& operator=(IconvConverter const&) = delete;

  IconvConverter(IconvConverter&& other) noexcept : cd_(other.cd_) {
    other.cd_ = kInvalid;
  }
  IconvConverter& operator=(IconvConverter&& other) noexcept {
    if (this != &other) {
      if (cd_ != kInvalid) {
        iconv_close(cd_);
      }
      cd_ = other.cd_;
      other.cd_ = kInvalid;
    }
    return *this;
  }

  // On failure (invalid sequence in input), returns the input unchanged
  std::string Convert(std::string_view input) {
    if (cd_ == kInvalid) {
      return std::string(input);  // moved-from
    }
    iconv(cd_, nullptr, nullptr, nullptr, nullptr);  // reset shift state
    std::string out(input.size() * 2 + 1, '\0');
    char* in_ptr = const_cast<char*>(input.data());
    size_t in_left = input.size();
    char* out_ptr = out.data();
    size_t out_left = out.size();
    size_t const rc = iconv(cd_, &in_ptr, &in_left, &out_ptr, &out_left);
    if (rc == static_cast<size_t>(-1)) {
      return std::string(input);
    }
    out.resize(out.size() - out_left);
    return out;
  }

 private:
  IconvConverter() noexcept = default;

  // reinterpret_cast is not a constant expression; use an inline variable
  static inline iconv_t const kInvalid = reinterpret_cast<iconv_t>(-1);
  iconv_t cd_{kInvalid};
};

}  // namespace cts::base

#endif  // CTS_BASE_ICONV_CONVERTER_H_
