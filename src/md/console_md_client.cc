/**
 * @file console_md_client.cc
 * @author lipingan (lipingan.dev@outlook.com)
 * @brief Debug client: forwards events to the log.
 */
#include "md/console_md_client.h"

#include <spdlog/spdlog.h>

#include "base/time_utils.h"

namespace cts::md {

void ConsoleMdClient::OnMdConnected() {
  SPDLOG_INFO("md: connected");
}

void ConsoleMdClient::OnMdDisconnected(int reason) {
  SPDLOG_WARN("md: disconnected reason={}", reason);
}

void ConsoleMdClient::OnMdLogin(bool success, std::string_view msg) {
  if (success) {
    SPDLOG_INFO("md: logged in");
  } else {
    SPDLOG_ERROR("md: login failed: {}", msg);
  }
}

void ConsoleMdClient::OnTick(Tick const& tick) {
  SPDLOG_INFO("tick: {} {} vol={} {}", tick.instrument_id, tick.last_price,
              tick.volume, base::FormatLocalTimeOfDay(tick.epoch_ms));
}

void ConsoleMdClient::OnMdError(int error_id, std::string_view msg) {
  SPDLOG_ERROR("md: error {} {}", error_id, msg);
}

}  // namespace cts::md
