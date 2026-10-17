/**
 * @file main.cc
 * @author lipingan (lipingan.dev@outlook.com)
 * @brief cts_backtest: replay a raw tick journal through the SMA strategy.
 */
#include <cstdint>
#include <string>

#include "backtest/backtest.h"

#include <CLI/CLI.hpp>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

int main(int argc, char** argv) {
  std::string file;
  cts::strategy::SmaCrossConfig config{};

  CLI::App app{"cts_backtest: replay a CTSRAWT journal through the SMA-cross "
               "strategy with simulated execution"};
  app.add_option("--file", file, "raw tick journal (ticks_YYYYMMDD.raw)")
      ->required();
  app.add_option("--instrument", config.instrument_id, "instrument id")
      ->required();
  app.add_option("--exchange", config.exchange_id, "exchange id")->required();
  app.add_option("--fast", config.fast, "fast SMA window (ticks)");
  app.add_option("--slow", config.slow, "slow SMA window (ticks)");
  app.add_option("--volume", config.volume, "lots per order");

  try {
    app.parse(argc, argv);
  } catch (CLI::ParseError const& e) {
    return app.exit(e);
  }

  spdlog::set_default_logger(spdlog::stdout_color_mt("cts_backtest"));

  auto const result =
      cts::backtest::RunBacktest(file, config);

  SPDLOG_INFO("ticks={} orders={} realized_pnl={}", result.ticks,
              result.orders_submitted, result.realized_pnl);
  for (auto const& p : result.positions) {
    SPDLOG_INFO("position: {} {} today={} yd={} avg={} realized={}",
                p.instrument_id,
                p.side == cts::td::Direction::kBuy ? "long" : "short",
                p.today_volume, p.yd_volume, p.avg_price, p.realized_pnl);
  }
  return 0;
}
