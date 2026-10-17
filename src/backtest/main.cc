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
  cts::td::CommissionRate fee{};
  int multiple = 1;

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
  // Commission schedule (by-money ratios on the notional, e.g. 0.0001 =
  // 万分之一); close-today defaults to the close rate when left at zero
  app.add_option("--open-fee-rate", fee.open_by_money,
                 "open commission, ratio of notional");
  app.add_option("--close-fee-rate", fee.close_by_money,
                 "close commission, ratio of notional");
  app.add_option("--close-today-fee-rate", fee.close_today_by_money,
                 "close-today commission, ratio of notional");
  app.add_option("--fee-per-lot", fee.open_by_volume,
                 "flat commission per lot, all offsets");
  app.add_option("--multiple", multiple, "contract volume multiple");

  try {
    app.parse(argc, argv);
  } catch (CLI::ParseError const& e) {
    return app.exit(e);
  }
  fee.close_by_volume = fee.open_by_volume;
  fee.close_today_by_volume = fee.open_by_volume;

  spdlog::set_default_logger(spdlog::stdout_color_mt("cts_backtest"));

  auto const result =
      cts::backtest::RunBacktest(file, config, fee, multiple);

  SPDLOG_INFO("ticks={} orders={} realized_pnl={} fee={} net_pnl={}",
              result.ticks, result.orders_submitted, result.realized_pnl,
              result.total_fee, result.net_pnl());
  for (auto const& p : result.positions) {
    SPDLOG_INFO("position: {} {} today={} yd={} avg={} realized={}",
                p.instrument_id,
                p.side == cts::td::Direction::kBuy ? "long" : "short",
                p.today_volume, p.yd_volume, p.avg_price, p.realized_pnl);
  }
  return 0;
}
