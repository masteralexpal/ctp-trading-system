/**
 * @file latest_spsc_queue.h
 * @author lipingan (lipingan.dev@outlook.com)
 * @brief Overwriting lock-free SPSC queue (drops the oldest when full; seqlock
 * protocol).
 */
#pragma once

#ifndef CTS_BASE_SYNC_LATEST_SPSC_QUEUE_H_
#define CTS_BASE_SYNC_LATEST_SPSC_QUEUE_H_

#include <atomic>
#include <cstddef>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>

namespace cts::base {

template <typename T,
          std::size_t Capacity,
          std::size_t CacheLineSize =
              std::hardware_destructive_interference_size>
class LatestSpscQueue {
  static constexpr std::size_t kMask = Capacity - 1;
  static_assert(Capacity > 0 && (Capacity & kMask) == 0,
                "Capacity must be a power of 2");
  static_assert(CacheLineSize >= sizeof(std::atomic<std::size_t>),
                "CacheLineSize too small");
  // Overwriting writes race consumer reads at the byte level (a seqlock
  // property): a torn copy must be harmless, which only trivially copyable
  // types guarantee
  static_assert(std::is_trivially_copyable_v<T>,
                "LatestSpscQueue requires trivially copyable T");

 public:
  LatestSpscQueue() = default;

  ~LatestSpscQueue() {
    // Pops move out but never destroy; reclaim every written slot here
    for (auto& slot : slots_) {
      if (slot.constructed) {
        std::destroy_at(slot.Ptr());
      }
    }
  }

  LatestSpscQueue(LatestSpscQueue const&) = delete;
  LatestSpscQueue& operator=(LatestSpscQueue const&) = delete;

  // Producer side: never fails. Overwrites the oldest when full (counted in
  // dropped_)
  void Push(T const& value) { Emplace(value); }
  void Push(T&& value) { Emplace(std::move(value)); }

  template <typename... Args>
  void Emplace(Args&&... args) {
    std::size_t const tail = tail_.load(std::memory_order_relaxed);
    Slot& slot = slots_[tail & kMask];
    // Mark "writing" (odd seq) before touching data, so a consumer mid-copy
    // always detects the overwrite
    slot.seq.store(((tail + 1) << 1) | 1, std::memory_order_release);
    if (slot.constructed) {
      std::destroy_at(slot.Ptr());  // reclaim before overwriting
    }
    std::construct_at(slot.Ptr(), std::forward<Args>(args)...);
    slot.constructed = true;
    slot.seq.store((tail + 1) << 1, std::memory_order_release);
    tail_.store(tail + 1, std::memory_order_release);
  }

  // Consumer side: false when empty. Drop accounting lives here (single
  // writer): every overwritten or torn element is counted exactly once
  [[nodiscard]] bool TryPop(T& out) {
    for (;;) {
      std::size_t const head = head_.load(std::memory_order_relaxed);
      if (head == tail_.load(std::memory_order_acquire)) {
        return false;
      }
      Slot& slot = slots_[head & kMask];
      std::size_t const seq = slot.seq.load(std::memory_order_acquire);
      if (seq == 0 || (seq >> 1) - 1 != head) {
        // Slot holds a different round: this element was overwritten. Step one
        // slot at a time; never jump -- live elements may sit in between
        dropped_.fetch_add(1, std::memory_order_relaxed);
        head_.store(head + 1, std::memory_order_relaxed);
        continue;
      }
      if ((seq & 1) != 0) {
        // Producer is mid-write on this slot; the window is tens of ns, spin
        continue;
      }
      out = std::move(*slot.Ptr());
      if (slot.seq.load(std::memory_order_acquire) != seq) {
        // Overwritten mid-copy (torn): discard, count, advance
        dropped_.fetch_add(1, std::memory_order_relaxed);
        head_.store(head + 1, std::memory_order_relaxed);
        continue;
      }
      head_.store(head + 1, std::memory_order_relaxed);
      return true;
    }
  }

  std::size_t dropped() const noexcept {
    return dropped_.load(std::memory_order_relaxed);
  }

 private:
  struct Slot {
    T* Ptr() noexcept { return std::launder(reinterpret_cast<T*>(storage)); }
    // (element index + 1) << 1, LSB is the "writing" flag; 0 means never
    // written
    std::atomic<std::size_t> seq{0};
    // Slot lifecycle flag (producer-written; read by producer and dtor)
    bool constructed{false};
    alignas(T) std::byte storage[sizeof(T)];
  };

  // Slots are deliberately not cache-line aligned: this queue targets
  // Tick-sized (hundreds of bytes) elements
  Slot slots_[Capacity];

  alignas(CacheLineSize) std::atomic<std::size_t> head_{0};  // consumer writes
  alignas(CacheLineSize) std::atomic<std::size_t> tail_{0};  // producer writes
  alignas(CacheLineSize) std::atomic<std::size_t> dropped_{
      0};  // consumer writes
};

}  // namespace cts::base

#endif  // CTS_BASE_SYNC_LATEST_SPSC_QUEUE_H_
