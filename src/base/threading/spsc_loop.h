/**
 * @file spsc_loop.h
 * @author lipingan (lipingan.dev@outlook.com)
 * @brief SpscLoop: RunLoop backend for single-producer POD event streams.
 */
#pragma once

#ifndef CTS_BASE_SPSC_LOOP_H_
#define CTS_BASE_SPSC_LOOP_H_

#include <atomic>
#include <cstddef>
#include <functional>
#include <string>
#include <string_view>
#include <utility>

#include "base/sync/parker.h"
#include "base/threading/run_loop.h"
#include "base/sync/spsc_queue.h"

namespace cts::base {

// RunLoop backend for latency-sensitive POD streams: an SPSC ring buffer
// plus a futex park, zero allocation per event. The producer is SINGLE by
// contract -- TryPush from one thread only (the queue's single-writer
// semantics depend on it). Overflow policy is drop-newest, counted in
// dropped(): the producer never blocks.
//
// The handler runs on the loop thread once per drained event, in order.
// RequestStop drains the queue before Run returns.
template <typename E, std::size_t Capacity>
class SpscLoop final : public RunLoop {
 public:
  using Handler = std::function<void(E const&)>;

  SpscLoop(std::string_view name, Handler handler)
      : name_(name), handler_(std::move(handler)) {}

  std::string_view name() const override { return name_; }

  // Producer thread ONLY. False = dropped (queue full), counted in dropped()
  bool TryPush(E const& e) { return Publish(e); }
  bool TryPush(E&& e) { return Publish(std::move(e)); }

  std::size_t dropped() const { return queue_.dropped(); }

  void RequestStop() override {
    stop_.store(true, std::memory_order_release);
    park_.NotifyUnconditionally();
  }
  void Wakeup() override { park_.Notify(); }

 protected:
  void RunImpl() override {
    E event;
    for (;;) {
      while (queue_.TryPop(event)) {
        handler_(event);
      }
      if (stop_.load(std::memory_order_acquire)) {
        // Drain once more after the stop request to close the producer gap
        while (queue_.TryPop(event)) {
          handler_(event);
        }
        return;
      }
      park_.WaitUnless([this] {
        return stop_.load(std::memory_order_acquire) || !queue_.Empty();
      });
    }
  }

 private:
  template <typename F>
  bool Publish(F&& e) {
    // Publish state first, then bump the wake word (Parker's invariant)
    if (queue_.TryPush(std::forward<F>(e))) {
      park_.Notify();
      return true;
    }
    return false;
  }

  std::string name_;
  Handler handler_;
  SpscQueue<E, Capacity> queue_;
  Parker park_;
  std::atomic<bool> stop_{false};
};

}  // namespace cts::base

#endif  // CTS_BASE_SPSC_LOOP_H_
