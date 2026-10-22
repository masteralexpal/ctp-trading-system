/**
 * @file op_log.h
 * @author lipingan (lipingan.dev@outlook.com)
 * @brief OpLog: append-only binary journal of accepted OMS ops.
 */
#pragma once

#ifndef CTS_OMS_OP_LOG_H_
#define CTS_OMS_OP_LOG_H_

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <string_view>

#include "td/order.h"

namespace cts::oms {

// Append-only binary log of the ops that built the OMS state: accepted order
// transitions, applied trades, registered intents. All payloads are POD
// (td/order.h static_asserts); a record is [u8 kind][u32 length][payload].
// Every append is flushed: kill -9 loses at most the torn tail record, which
// replay skips (and the next Open truncates away).
//
// The snapshot subsumes the log up to its recorded offset; the day roll
// checkpoints (snapshot + Reset). Replay of the log into a snapshot-restored
// manager converges with replay into an empty one: the fold is idempotent.
class OpLog {
 public:
  OpLog() = default;
  ~OpLog();

  OpLog(OpLog const&) = delete;
  OpLog& operator=(OpLog const&) = delete;

  // Creates or validates the file, truncates a torn tail, seeks to the end.
  // False = logging disabled (state still recovers via the broker query)
  bool Open(std::filesystem::path const& path);

  void AppendOrder(td::OrderReport const& report);
  void AppendTrade(td::TradeReport const& trade);
  void AppendIntent(std::string_view order_ref, td::OrderRequest const& req);

  // Current byte size, recorded into snapshots as the replay offset
  std::uint64_t size() const { return size_; }
  bool open() const { return file_ != nullptr; }
  // Day-roll checkpoint: the snapshot taken first subsumes everything
  void Reset();

  // Visits each intact record with its byte offset; stops at a torn tail
  bool ForEach(
      std::function<void(std::uint64_t, td::OrderReport const&)> on_order,
      std::function<void(std::uint64_t, td::TradeReport const&)> on_trade,
      std::function<void(std::uint64_t, std::string_view,
                         td::OrderRequest const&)> on_intent) const;

 private:
  void Append(std::uint8_t kind, void const* payload, std::uint32_t len);

  FILE* file_{nullptr};
  std::filesystem::path path_;
  std::uint64_t size_{0};
};

}  // namespace cts::oms

#endif  // CTS_OMS_OP_LOG_H_
