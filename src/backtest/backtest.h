/**
 * @file backtest.h
 * @author lipingan (lipingan.dev@outlook.com)
 * @brief Backtest runner: replay a raw tick journal through a strategy.
 */
#pragma once

#ifndef CTS_BACKTEST_BACKTEST_H_
#define CTS_BACKTEST_BACKTEST_H_

#include <cstdint>
#include <filesystem>
#include <vector>

#include "oms/order_manager.h"
#include "strategy/sma_cross_strategy.h"

namespace cts::backtest {

struct BacktestResult {
  std::uint64_t ticks{0};
  std::uint64_t orders_submitted{0};
  double realized_pnl{0};  // gross, from the ledger
  double total_fee{0};     // commissions charged on fills
  double net_pnl() const { return realized_pnl - total_fee; }
  std::vector<oms::Position> positions;
};

// Replays a CTSRAWT journal through an SMA-cross strategy with simulated
// execution (SimOrderSink); accounting by the real oms::OrderManager -- the
// same code paths as live trading, only the gateways differ.
// fee: commission schedule applied to every fill (zero rates = no fees);
// volume_multiple: contract multiplier for fee notionals
BacktestResult RunBacktest(std::filesystem::path const& file,
                           strategy::SmaCrossConfig const& config,
                           td::CommissionRate const& fee = {},
                           int volume_multiple = 1);

}  // namespace cts::backtest

#endif  // CTS_BACKTEST_BACKTEST_H_
