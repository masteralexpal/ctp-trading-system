/**
 * @file sim_order_sink.cc
 * @author lipingan (lipingan.dev@outlook.com)
 * @brief Simulated OrderSink for backtests: touch-fill on replayed ticks.
 */
#include "backtest/sim_order_sink.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace cts::backtest {

std::string SimOrderSink::InsertOrder(td::OrderRequest const& req) {
  Working w;
  w.req = req;
  char buf[13];
  std::snprintf(buf, sizeof(buf), "%012d", next_ref_++);
  w.ref = buf;
  working_.push_back(w);

  td::OrderReport report{};
  std::memcpy(report.instrument_id, req.instrument_id,
              sizeof(report.instrument_id));
  std::memcpy(report.exchange_id, req.exchange_id, sizeof(report.exchange_id));
  std::memcpy(report.order_ref, w.ref.c_str(), sizeof(report.order_ref) - 1);
  report.direction = req.direction;
  report.offset = req.offset;
  report.limit_price = req.limit_price;
  report.volume_total_original = req.volume;
  report.volume_total = req.volume;
  report.status = td::OrderStatus::kSubmitted;
  FireOrder(report);
  return w.ref;
}

int SimOrderSink::CancelOrder(std::string_view order_ref) {
  auto const it = std::find_if(working_.begin(), working_.end(),
                               [&](Working const& w) {
                                 return w.ref == order_ref;
                               });
  if (it == working_.end()) {
    return -1;
  }
  td::OrderReport report{};
  std::memcpy(report.instrument_id, it->req.instrument_id,
              sizeof(report.instrument_id));
  std::memcpy(report.exchange_id, it->req.exchange_id,
              sizeof(report.exchange_id));
  std::memcpy(report.order_ref, it->ref.c_str(), sizeof(report.order_ref) - 1);
  report.direction = it->req.direction;
  report.offset = it->req.offset;
  report.limit_price = it->req.limit_price;
  report.volume_total_original = it->req.volume;
  report.volume_total = it->req.volume;
  report.status = td::OrderStatus::kCanceled;
  FireOrder(report);
  working_.erase(it);
  return 0;
}

void SimOrderSink::OnTick(CThostFtdcDepthMarketDataField const& tick) {
  for (std::size_t i = 0; i < working_.size();) {
    Working const& w = working_[i];
    bool const touched =
        w.req.direction == td::Direction::kBuy
            ? tick.AskPrice1 <= w.req.limit_price && tick.AskVolume1 > 0
            : tick.BidPrice1 >= w.req.limit_price && tick.BidVolume1 > 0;
    if (!touched) {
      ++i;
      continue;
    }
    td::OrderReport report{};
    std::memcpy(report.instrument_id, w.req.instrument_id,
                sizeof(report.instrument_id));
    std::memcpy(report.exchange_id, w.req.exchange_id,
                sizeof(report.exchange_id));
    std::memcpy(report.order_ref, w.ref.c_str(), sizeof(report.order_ref) - 1);
    report.direction = w.req.direction;
    report.offset = w.req.offset;
    report.limit_price = w.req.limit_price;
    report.volume_total_original = w.req.volume;
    report.volume_traded = w.req.volume;
    report.volume_total = 0;
    report.status = td::OrderStatus::kAllTraded;
    FireOrder(report);  // the CTP quirk: order terminal report comes FIRST

    td::TradeReport trade{};
    std::memcpy(trade.instrument_id, w.req.instrument_id,
                sizeof(trade.instrument_id));
    std::memcpy(trade.exchange_id, w.req.exchange_id,
                sizeof(trade.exchange_id));
    std::memcpy(trade.order_ref, w.ref.c_str(), sizeof(trade.order_ref) - 1);
    std::strncpy(trade.trade_id, w.ref.c_str(), sizeof(trade.trade_id) - 1);
    std::strncpy(trade.trading_day, tick.TradingDay,
                 sizeof(trade.trading_day) - 1);
    trade.direction = w.req.direction;
    trade.offset = w.req.offset;
    trade.price = w.req.limit_price;
    trade.volume = w.req.volume;
    FireTrade(trade);

    working_.erase(working_.begin() + static_cast<long>(i));
  }
}

void SimOrderSink::FireOrder(td::OrderReport const& report) {
  for (auto* client : clients_) {
    client->OnTdOrder(report);
  }
}

void SimOrderSink::FireTrade(td::TradeReport const& trade) {
  for (auto* client : clients_) {
    client->OnTdTrade(trade);
  }
}

}  // namespace cts::backtest
