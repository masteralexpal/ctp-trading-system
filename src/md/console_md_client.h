/**
 * @file console_md_client.h
 * @author lipingan (lipingan.dev@outlook.com)
 * @brief Debug client: forwards events to the log.
 */
#pragma once

#ifndef CTS_MD_CONSOLE_MD_CLIENT_H_
#define CTS_MD_CONSOLE_MD_CLIENT_H_

#include "md/md_client.h"

namespace cts::md {

class ConsoleMdClient final : public MdClient {
 public:
  void OnMdConnected() override;
  void OnMdDisconnected(int reason) override;
  void OnMdLogin(bool success, std::string_view msg) override;
  void OnTick(Tick const& tick) override;
  void OnMdError(int error_id, std::string_view msg) override;
};

}  // namespace cts::md

#endif  // CTS_MD_CONSOLE_MD_CLIENT_H_
