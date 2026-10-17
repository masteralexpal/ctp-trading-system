/**
 * @file cli.cc
 * @author lipingan (lipingan.dev@outlook.com)
 * @brief Command-line parsing entry.
 */
#include "app/cli.h"

#include <cstdlib>
#include <string>

#include "base/version.h"

#include <CLI/CLI.hpp>

namespace cts::app {
namespace {

// CTP front address must be in tcp://host:port form
CLI::Validator const kTcpFront{
    [](std::string& input) -> std::string {
      if (!input.starts_with("tcp://")) {
        return "front address must start with tcp://";
      }
      return {};
    },
    "TCP", "tcp://host:port"};

std::unique_ptr<CLI::App> BuildApp(AppConfig& cfg) {
  auto app = std::make_unique<CLI::App>(
      "ctp-trading-system: CTP futures trading system");

  char const* home = std::getenv("HOME");
  std::string const default_config =
      home != nullptr ? std::string(home) + "/.cts/config.ini" : "";
  auto* config_opt =
      app->set_config("--config", default_config,
                      "INI config path (may be given twice; the latter wins)");
  config_opt->expected(1, 2);
  app->set_version_flag("--version", std::string(base::Version()));

  // Default flow-file root; CLI11 leaves the bound variable untouched when
  // unset
  if (cfg.flow_dir.empty()) {
    cfg.flow_dir =
        home != nullptr ? std::string(home) + "/.cts/flow" : ".cts/flow";
  }
  app->add_option("--flow-dir", cfg.flow_dir, "root dir of CTP flow files");
  app->add_option("--strategy-instrument", cfg.strategy_instrument,
                  "run the SMA-cross demo strategy on this instrument "
                  "(empty disables)");
  app->add_option("--strategy-fast", cfg.strategy_fast,
                  "strategy fast SMA window (ticks)");
  app->add_option("--strategy-slow", cfg.strategy_slow,
                  "strategy slow SMA window (ticks)");
  app->add_option("--strategy-volume", cfg.strategy_volume,
                  "strategy lots per order");
  app->add_option("--log-level", cfg.log_level, "log level")
      ->check(CLI::IsMember({"trace", "debug", "info", "warn", "error"}))
      ->capture_default_str();
  if (cfg.log_dir.empty()) {
    cfg.log_dir =
        home != nullptr ? std::string(home) + "/.cts/logs" : ".cts/logs";
  }
  app->add_option("--log-dir", cfg.log_dir,
                  "log dir; one daily-rolled file per calendar day");
  app->add_option("--md-front", cfg.md_front, "market data front address")
      ->check(kTcpFront);
  app->add_option("--trader-front", cfg.trader_front, "trader front address")
      ->check(kTcpFront);
  app->add_option("--username", cfg.username, "CTP investor id");
  // password/auth-code belong in INI only: CLI args are visible in ps
  app->add_option("--password", cfg.password, "CTP password");
  app->add_option("--broker-id", cfg.broker_id, "broker id (SimNow: 9999)");
  app->add_option("--app-id", cfg.app_id, "CTP auth app id");
  app->add_option("--auth-code", cfg.auth_code, "CTP auth code");
  app->add_flag("--dry-run", cfg.dry_run,
                "parse and print config only; do not connect");
  app->add_option("instruments", cfg.instruments,
                  "instruments to subscribe (multi)");

  return app;
}

}  // namespace

CliResult ParseArgs(int argc, char** argv) {
  AppConfig config;
  auto app = BuildApp(config);

  try {
    app->parse(argc, argv);
  } catch (CLI::ParseError const& e) {
    // help/version/parse error: exit() prints and yields a semantic exit code
    return {std::nullopt, app->exit(e)};
  }
  return {.config = std::move(config)};
}

}  // namespace cts::app
