/**
 * @file backtest.cc
 * @author lipingan (lipingan.dev@outlook.com)
 * @brief Backtest runner: replay a raw tick journal through a strategy.
 */
#include "backtest/backtest.h"

#include "backtest/sim_order_sink.h"
#include "md/detail/tick_ctp.h"
#include "recorder/raw_tick_file.h"

namespace cts::backtest {

BacktestResult RunBacktest(std::filesystem::path const& file,
                           strategy::SmaCrossConfig const& config,
                           td::CommissionRate const& fee,
                           int volume_multiple) {
  SimOrderSink sink;
  sink.SetFee(fee, volume_multiple);
  oms::OrderManager ledger;
  strategy::SmaCrossStrategy strategy{config, sink};
  sink.AddClient(strategy);
  sink.AddClient(ledger);
  strategy.Start();

  recorder::RawTickFileReader reader;
  reader.Open(file);

  BacktestResult result;
  recorder::RawTickRecord record;
  while (reader.Next(record)) {
    ++result.ticks;
    sink.OnTick(record.tick);  // resting orders react first
    strategy.OnTick(md::detail::TickFromCtp(record.tick));
  }
  strategy.Stop();

  result.orders_submitted = ledger.orders().size();
  result.positions = ledger.positions();
  for (auto const& p : result.positions) {
    result.realized_pnl += p.realized_pnl;
  }
  result.total_fee = sink.total_fee();
  return result;
}

}  // namespace cts::backtest
