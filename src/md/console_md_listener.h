/**
 * @file console_md_listener.h
 * @author lipingan (lipingan.dev@outlook.com)
 * @brief Debug listener: forwards events to the log.
 */
#pragma once

#ifndef CTS_MD_CONSOLE_MD_LISTENER_H_
#define CTS_MD_CONSOLE_MD_LISTENER_H_

#include <atomic>

#include "md/md_listener.h"

namespace cts::md {

class ConsoleMdListener final : public MdListener {
 public:
  std::atomic<bool> logged_in{false};

  void OnTick(Tick const& tick) override;
  void OnMdState(bool connected, bool logged_in) override;
  void OnMdError(int error_id, std::string_view msg) override;
};

}  // namespace cts::md

#endif  // CTS_MD_CONSOLE_MD_LISTENER_H_
