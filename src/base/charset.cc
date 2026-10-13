/**
 * @file charset.cc
 * @author lipingan (lipingan.dev@outlook.com)
 * @brief GBK <-> UTF-8 transcoding.
 */
#include "base/charset.h"

#include "base/iconv_converter.h"

namespace cts::base {

std::string GbkToUtf8(std::string_view input) {
  thread_local auto converter = IconvConverter::Create("UTF-8", "GBK");
  if (!converter) {
    return std::string(input);
  }
  return converter->Convert(input);
}

std::string Utf8ToGbk(std::string_view input) {
  thread_local auto converter = IconvConverter::Create("GBK", "UTF-8");
  if (!converter) {
    return std::string(input);
  }
  return converter->Convert(input);
}

}  // namespace cts::base
