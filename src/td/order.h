/**
 * @file order.h
 * @author lipingan (lipingan.dev@outlook.com)
 * @brief Normalized trading types: order request, order/trade reports.
 */
#pragma once

#ifndef CTS_TD_ORDER_H_
#define CTS_TD_ORDER_H_

#include <cstdint>
#include <type_traits>

namespace cts::td {

// Normalized trading types: the only trading vocabulary visible outside the
// td module. Translated once from the vendor structs at the gateway boundary
// (see detail/order_ctp.h); everything downstream is vendor-free.
//
// Trivially copyable on purpose: SPSC queues and binary journals rely on
// plain memcpy. Never add std::string / std::vector members here.

enum class Direction : char {
  kBuy,
  kSell,
};

enum class Offset : char {
  kOpen,
  kClose,
  kCloseToday,  // SHFE/INE distinguish today vs yesterday positions
  kCloseYesterday,
};

// Order request: limit-price GFD only (the MVP order type)
struct OrderRequest {
  char instrument_id[31];  // NUL-terminated, e.g. "rb2701"
  char exchange_id[9];     // e.g. "SHFE"
  Direction direction;
  Offset offset;
  double limit_price;
  std::int32_t volume;
};

enum class OrderStatus : char {
  kSubmitted,   // accepted, no fill yet
  kPartTraded,  // partial fill, still working
  kAllTraded,
  kCanceled,
  kRejected,  // refused by broker (OnRspOrderInsert) or exchange (ErrRtn)
  kUnknown,
};

// Order state report, normalized from OnRtnOrder / OnRspOrderInsert /
// OnErrRtnOrderInsert. order_ref correlates back to the request; front_id +
// session_id + order_ref is the cancel key.
struct OrderReport {
  char instrument_id[31];
  char exchange_id[9];
  char order_ref[13];     // our reference (zero-padded decimal)
  char order_sys_id[21];  // exchange-side id; empty before exchange accept
  Direction direction;
  Offset offset;
  double limit_price;
  std::int32_t volume_total_original;
  std::int32_t volume_traded;
  std::int32_t volume_total;  // remaining
  OrderStatus status;
  std::int32_t front_id;
  std::int32_t session_id;
};

// Fill report, normalized from OnRtnTrade
struct TradeReport {
  char instrument_id[31];
  char exchange_id[9];
  char order_ref[13];
  char trade_id[21];
  char trading_day[9];  // business day; a change vs the ledger rolls buckets
  Direction direction;
  Offset offset;
  double price;
  std::int32_t volume;
  std::int64_t epoch_ms;  // exchange trade time folded to epoch ms; 0 if absent
};

// One row of the broker position snapshot, normalized from
// OnRspQryInvestorPosition; PositionDate is already folded into the buckets
struct PositionRow {
  char instrument_id[31];
  char exchange_id[9];
  Direction direction;  // position side: kBuy = long, kSell = short
  std::int32_t yd_volume;
  std::int32_t today_volume;
};

static_assert(std::is_trivially_copyable_v<OrderRequest>);
static_assert(std::is_trivially_copyable_v<OrderReport>);
static_assert(std::is_trivially_copyable_v<TradeReport>);
static_assert(std::is_trivially_copyable_v<PositionRow>);

}  // namespace cts::td

#endif  // CTS_TD_ORDER_H_
