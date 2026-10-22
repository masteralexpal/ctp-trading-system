/**
 * @file oms_listener.h
 * @author lipingan (lipingan.dev@outlook.com)
 * @brief OmsListener: the OMS output channel (effects of the fold).
 */
#pragma once

#ifndef CTS_OMS_OMS_LISTENER_H_
#define CTS_OMS_OMS_LISTENER_H_

#include <memory>
#include <vector>

#include "td/order.h"

namespace cts::oms {

// Effects of the OMS fold, emitted synchronously on the td-dispatch thread in
// arrival order (FIFO). This is the OUTPUT channel: the td broadcast remains
// the input; effects are what the fold decided happened. Replays, absorbed
// stale reports and deduped trades emit nothing.
//
// A terminal order update is delayed until the fills it reports have been
// applied (CTP fires all-traded/canceled BEFORE the trailing trade details):
// a listener only learns "flight over" once the ledger already shows the
// fills, so it can never over-book in the gap.
//
// Contract: a listener must outlive the OrderManager's event flow (register
// at assembly, destroy after the dispatcher stops).
class OmsListener {
 public:
  virtual ~OmsListener() = default;

  // A journal transition: confirm, partial progress, terminal (see above)
  virtual void OnOrderUpdate(td::OrderReport const& report) = 0;
  // A trade applied to the ledger (deduped: each trade_id fires at most once)
  virtual void OnTradeApplied(td::TradeReport const& trade) = 0;
  // The broker position snapshot, after the ledger reconciled against it
  virtual void OnPositionsReconciled(
      std::shared_ptr<std::vector<td::PositionRow> const> rows) = 0;
};

}  // namespace cts::oms

#endif  // CTS_OMS_OMS_LISTENER_H_
