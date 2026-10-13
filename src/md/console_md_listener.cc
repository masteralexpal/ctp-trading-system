/**
 * @file console_md_listener.cc
 * @author lipingan (lipingan.dev@outlook.com)
 * @brief Debug listener: forwards events to the log.
 */
#include "md/console_md_listener.h"

#include <spdlog/spdlog.h>

#include "base/time_utils.h"

namespace cts::md {

void ConsoleMdListener::OnTick(Tick const& tick) {
  SPDLOG_INFO("tick: {} {} vol={} {}", tick.instrument_id, tick.last_price,
              tick.volume, base::FormatLocalTimeOfDay(tick.epoch_ms));
}

void ConsoleMdListener::OnMdState(bool connected, bool login) {
  SPDLOG_INFO("md: connected={} logged_in={}", connected, login);
  logged_in = connected && login;
}

void ConsoleMdListener::OnMdError(int error_id, std::string_view msg) {
  SPDLOG_ERROR("md: error {} {}", error_id, msg);
}

}  // namespace cts::md
