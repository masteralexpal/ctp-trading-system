/**
 * @file raw_tick_recorder.h
 * @author lipingan (lipingan.dev@outlook.com)
 * @brief RawTickSink that journals verbatim ticks to per-trading-day files.
 */
#pragma once

#ifndef CTS_RECORDER_RAW_TICK_RECORDER_H_
#define CTS_RECORDER_RAW_TICK_RECORDER_H_

// Boundary header: pulls in the CTP wire struct via raw_tick_sink.h. Only the
// recorder app assembles this.

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

#include "base/sync/futex_park.h"
#include "base/sync/spsc_queue.h"
#include "recorder/raw_tick_file.h"
#include "md/raw_tick_sink.h"

namespace cts::recorder {

// Verbatim tick journal: OnRawTick (CTP callback thread) only stamps
// seq/arrival_ms and enqueues; Run() (writer thread) drains the queue,
// rotates the file when TradingDay changes, and appends records.
//
// Assembly order: construct -> base::Thread.Start([&]{ r.Run(); }) ->
// MdGateway::SetRawTickSink(&r) -> ... -> MdGateway::Disconnect ->
// RequestStop -> Thread.Stop
class RawTickRecorder final : public md::RawTickSink {
 public:
  RawTickRecorder(std::filesystem::path dir, std::string source);
  ~RawTickRecorder() override = default;  // assembler stops/joins first

  RawTickRecorder(RawTickRecorder const&) = delete;
  RawTickRecorder& operator=(RawTickRecorder const&) = delete;

  // md::RawTickSink, CTP callback thread: stamp-and-enqueue only. A full queue
  // drops the tick (seq gap marks the loss; see dropped())
  void OnRawTick(CThostFtdcDepthMarketDataField const& tick) override;

  // Writer loop: parks on a futex when empty; runs until RequestStop and
  // drained
  void Run();
  // Idempotent, any thread; wakes the parked loop
  void RequestStop();

  std::uint64_t recorded() const {
    return recorded_.load(std::memory_order_relaxed);
  }
  // Ticks dropped on queue overflow (each one is a seq gap in the file)
  std::size_t dropped() const { return queue_.dropped(); }
  // Records the writer failed to append (IO error; file stays open)
  std::uint64_t write_errors() const {
    return write_errors_.load(std::memory_order_relaxed);
  }

 private:
  // Writer thread only: (re)opens dir_/ticks_<TradingDay>.raw on day change
  void RotateIfNeeded(std::string_view trading_day);

  std::filesystem::path const dir_;
  std::string const source_;
  base::SpscQueue<RawTickRecord, 65536> queue_;
  base::FutexPark park_;
  std::atomic<bool> stop_requested_{false};
  std::atomic<std::uint64_t> recorded_{0};
  std::atomic<std::uint64_t> write_errors_{0};
  std::uint64_t next_seq_{0};  // producer thread only
  // Writer thread only:
  RawTickFileWriter writer_;
  std::string current_day_;
};

}  // namespace cts::recorder

#endif  // CTS_RECORDER_RAW_TICK_RECORDER_H_
