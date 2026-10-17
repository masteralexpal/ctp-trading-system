/**
 * @file raw_tick_recorder.cc
 * @author lipingan (lipingan.dev@outlook.com)
 * @brief RawTickSink that journals verbatim ticks to per-trading-day files.
 */
#include "recorder/raw_tick_recorder.h"

#include <chrono>
#include <cstring>
#include <utility>

#include <spdlog/spdlog.h>

namespace cts::recorder {
namespace {

// CTP fixed arrays are NUL-terminated unless completely full; never trust
// the wire
std::string_view FixedString(char const* field, std::size_t capacity) {
  return {field, strnlen(field, capacity)};
}

std::int64_t NowEpochMs() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

}  // namespace

RawTickRecorder::RawTickRecorder(std::filesystem::path dir, std::string source)
    : dir_(std::move(dir)), source_(std::move(source)) {}

void RawTickRecorder::OnRawTick(CThostFtdcDepthMarketDataField const& tick) {
  RawTickRecord record{};
  record.seq = ++next_seq_;
  record.arrival_ms = NowEpochMs();
  record.tick = tick;
  if (queue_.TryPush(record)) {
    park_.Notify();
  }
  // Full queue: the newest is dropped (SpscQueue contract); the missing seq
  // marks the loss in the file
}

void RawTickRecorder::Run() {
  RawTickRecord record;
  auto const drain = [&] {
    while (queue_.TryPop(record)) {
      RotateIfNeeded(
          FixedString(record.tick.TradingDay, sizeof(record.tick.TradingDay)));
      if (writer_.is_open() && writer_.Write(record)) {
        recorded_.fetch_add(1, std::memory_order_relaxed);
      } else {
        write_errors_.fetch_add(1, std::memory_order_relaxed);
      }
    }
  };
  for (;;) {
    drain();
    if (stop_requested_.load(std::memory_order_acquire)) {
      drain();  // once more to close the producer gap
      return;
    }
    park_.WaitUnless([this] {
      return stop_requested_.load(std::memory_order_acquire) || !queue_.Empty();
    });
  }
}

void RawTickRecorder::RequestStop() {
  stop_requested_.store(true, std::memory_order_release);
  park_.NotifyUnconditionally();
}

void RawTickRecorder::RotateIfNeeded(std::string_view trading_day) {
  if (writer_.is_open() && trading_day == current_day_) {
    return;
  }
  writer_.Close();
  current_day_ = trading_day;
  auto const path = dir_ / ("ticks_" + std::string(trading_day) + ".raw");
  if (!writer_.Open(path, source_)) {
    SPDLOG_ERROR("recorder: open {} failed (errno={})", path.string(), errno);
    return;
  }
  SPDLOG_INFO("recorder: writing {} (resumed at seq {})", path.string(),
              writer_.last_seq());
}

}  // namespace cts::recorder
