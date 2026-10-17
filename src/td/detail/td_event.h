/**
 * @file td_event.h
 * @author lipingan (lipingan.dev@outlook.com)
 * @brief Boxed trading event carried by TdDispatcher's queue.
 */
#pragma once

#ifndef CTS_TD_DETAIL_TD_EVENT_H_
#define CTS_TD_DETAIL_TD_EVENT_H_

#include <memory>
#include <string>
#include <vector>

#include "td/order.h"

namespace cts::td::detail {

// Uniformly boxed event: every TdDelegate callback enters one queue,
// preserving global order
struct TdEvent {
  enum class Kind {
    kConnected,
    kDisconnected,
    kReady,
    kOrder,
    kTrade,
    kPositions,
    kError,
  };
  Kind kind;
  OrderReport order{};  // kOrder only
  TradeReport trade{};  // kTrade only
  int code{0};          // kDisconnected=reason / kError=error_id
  bool success{false};  // kReady only
  std::string msg;      // kReady / kError (UTF-8)
  // kPositions only: broker position snapshot (reconciliation input)
  std::shared_ptr<std::vector<PositionRow> const> positions;
};

}  // namespace cts::td::detail

#endif  // CTS_TD_DETAIL_TD_EVENT_H_
