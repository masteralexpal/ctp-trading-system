/**
 * @file futex_park.h
 * @author lipingan (lipingan.dev@outlook.com)
 * @brief Futex-based park/notify for producer-consumer worker loops.
 */
#pragma once

#ifndef CTS_BASE_FUTEX_PARK_H_
#define CTS_BASE_FUTEX_PARK_H_

#include <atomic>
#include <cstdint>
#include <optional>
#include <utility>

#include "base/sync/futex_utils.h"

namespace cts::base {

// Park/notify for a single consumer loop (MdDispatcher and RawTickRecorder
// use this): zero-CPU idle without lost wakeups. The td dispatcher does NOT:
// its events are low-rate, so it runs on a TaskLoop instead.
//
// Consumer: loop { drain work; park.WaitUnless(work_pending); }
// Producer: make work visible, then park.Notify() -- ALWAYS in this order.
//
// Invariant: producers publish state BEFORE bumping the wake word; the
// consumer samples the word BEFORE re-checking its conditions -- an absorbed
// bump implies the state is visible to the re-check, a later bump returns
// EAGAIN or wakes the wait. See Documents/ctp/futex_lost_wakeup_postmortem.md
class FutexPark {
 public:
  FutexPark() = default;

  FutexPark(FutexPark const&) = delete;
  FutexPark& operator=(FutexPark const&) = delete;

  // Producer side, after publishing work. Cheap when unparked
  void Notify() {
    word_.fetch_add(1, std::memory_order_release);
    // seq_cst pairs with the consumer's parked_ store
    if (parked_.load()) {
      detail::FutexWakeOne(word_);
    }
  }

  // Stop/shutdown paths: never gated by a stale parked_ read
  void NotifyUnconditionally() {
    word_.fetch_add(1, std::memory_order_release);
    detail::FutexWakeOne(word_);
  }

  // Consumer side: returns immediately while `has_work()` is true, else parks
  // until a Notify. has_work must re-read the producer-published state
  template <typename F>
  void WaitUnless(F&& has_work) {
    parked_.store(true);
    std::uint32_t const sample = word_.load(std::memory_order_acquire);
    if (std::forward<F>(has_work)()) {
      parked_.store(false, std::memory_order_release);
      return;
    }
    detail::FutexWait(word_, sample, std::nullopt);
    parked_.store(false, std::memory_order_release);
  }

 private:
  std::atomic<std::uint32_t> word_{0};
  std::atomic<bool> parked_{false};
};

}  // namespace cts::base

#endif  // CTS_BASE_FUTEX_PARK_H_
