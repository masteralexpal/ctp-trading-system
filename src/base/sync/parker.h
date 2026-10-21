/**
 * @file parker.h
 * @author lipingan (lipingan.dev@outlook.com)
 * @brief Parker: zero-CPU park/notify for producer-consumer worker loops.
 */
#pragma once

#ifndef CTS_BASE_SYNC_PARKER_H_
#define CTS_BASE_SYNC_PARKER_H_

#include <atomic>
#include <cstdint>
#include <utility>

namespace cts::base {

// Park/notify for a single consumer loop: zero-CPU idle without lost
// wakeups. The blocking mechanism is an implementation detail living in the
// .cc -- this header exposes no syscall surface.
//
// Consumer: loop { drain work; parker.WaitUnless(work_pending); }
// Producer: make work visible, then parker.Notify() -- ALWAYS in this order.
//
// Invariant: producers publish state BEFORE bumping the wake word; the
// consumer samples the word BEFORE re-checking its conditions -- an absorbed
// bump implies the state is visible to the re-check, a later bump wakes the
// wait. See Documents/ctp/futex_lost_wakeup_postmortem.md
class Parker {
 public:
  Parker() = default;

  Parker(Parker const&) = delete;
  Parker& operator=(Parker const&) = delete;

  // Producer side, after publishing work. Cheap when unparked
  void Notify();

  // Stop/shutdown paths: never gated by a stale parked read
  void NotifyUnconditionally();

  // Consumer side: returns immediately while has_work() is true, else parks
  // until a Notify. has_work must re-read the producer-published state
  template <typename F>
  void WaitUnless(F&& has_work) {
    parked_.store(true);
    std::uint32_t const sample = word_.load(std::memory_order_acquire);
    if (std::forward<F>(has_work)()) {
      parked_.store(false, std::memory_order_release);
      return;
    }
    WaitWord(sample);
    parked_.store(false, std::memory_order_release);
  }

 private:
  void WakeOne();
  void WaitWord(std::uint32_t expected);

  std::atomic<std::uint32_t> word_{0};
  std::atomic<bool> parked_{false};
};

}  // namespace cts::base

#endif  // CTS_BASE_SYNC_PARKER_H_
