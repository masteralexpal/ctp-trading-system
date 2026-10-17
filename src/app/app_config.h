/**
 * @file app_config.h
 * @author lipingan (lipingan.dev@outlook.com)
 * @brief App config + Inject/Get global access.
 */
#pragma once

#ifndef CTS_APP_APP_CONFIG_H_
#define CTS_APP_APP_CONFIG_H_

#include <string>
#include <vector>

namespace cts::app {

struct AppConfig {
  // CTP configs.
  std::string broker_id;     // broker id
  std::string username;      // investor id
  std::string password;      // password
  std::string app_id;        //
  std::string auth_code;     //
  std::string md_front;      // market data front
  std::string trader_front;  // trader front
  // Root dir of CTP flow files (.con resume bookmarks);
  // filled by the CLI layer ($HOME/.cts/flow); gateways add per-role subdirs
  std::string flow_dir;
  // Tick recording dir (one binary journal per trading day); empty = disabled
  std::string record_dir;

  // SMA-cross demo strategy; empty instrument = no strategy
  std::string strategy_instrument;
  int strategy_fast{5};    // fast SMA window (ticks)
  int strategy_slow{20};   // slow SMA window (ticks)
  int strategy_volume{1};  // lots per order
  // logging.
  std::string log_level{"info"};  // log level

  // instruments
  std::vector<std::string> instruments;
  bool dry_run{false};
};

void InstallAppConfig(AppConfig config);
AppConfig const& GetAppConfig();

}  // namespace cts::app

#endif  // CTS_APP_APP_CONFIG_H_
