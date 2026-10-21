/**
 * @file spsc_queue.h
 * @author lipingan (lipingan.dev@outlook.com)
 * @brief Fixed-size lock-free SPSC queue (drops the newest when full).
 */
#pragma once

#ifndef CTS_BASE_SYNC_SPSC_QUEUE_H_
#define CTS_BASE_SYNC_SPSC_QUEUE_H_

#include <atomic>
#include <cstddef>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>

namespace cts::base {

// Fixed-size lock-free SPSC ring; drops the NEWEST when full (counted in
// dropped()) so the producer never blocks and never touches head. Contract:
// push on the producer thread only, pop on the consumer thread only.
// Capacity must be a power of two; slot storage is heap-allocated because
// Capacity * sizeof(T) can be megabytes.
// See latest_spsc_queue.h for the drop-oldest variant.
template <typename T,
          std::size_t Capacity,
          std::size_t CacheLineSize =
              std::hardware_destructive_interference_size>
class SpscQueue {
  static constexpr std::size_t kMask = Capacity - 1;
  static_assert(Capacity > 0 && (Capacity & kMask) == 0,
                "Capacity must be a power of 2");
  static_assert(CacheLineSize >= sizeof(std::atomic<std::size_t>),
                "CacheLineSize too small");

 public:
  SpscQueue()
      : storage_(static_cast<std::byte*>(
            ::operator new(Capacity * sizeof(T),
                           std::align_val_t{alignof(T)}))) {}

  ~SpscQueue() {
    if constexpr (!std::is_trivially_destructible_v<T>) {
      std::size_t const tail = tail_.load(std::memory_order_relaxed);
      for (std::size_t head = head_.load(std::memory_order_relaxed);
           head < tail; ++head) {
        std::destroy_at(SlotPtr(head));
      }
    }
    ::operator delete(storage_, std::align_val_t{alignof(T)});
  }

  SpscQueue(SpscQueue const&) = delete;
  SpscQueue& operator=(SpscQueue const&) = delete;

  [[nodiscard]] bool TryPush(T const& value) { return TryEmplace(value); }
  [[nodiscard]] bool TryPush(T&& value) { return TryEmplace(std::move(value)); }
  template <typename... Args>
  [[nodiscard]] bool TryEmplace(Args&&... args) {
    std::size_t const tail = tail_.load(std::memory_order_relaxed);
    if (tail - cached_head_ >= Capacity) {
      cached_head_ = head_.load(std::memory_order_acquire);
      if (tail - cached_head_ >= Capacity) {
        dropped_.fetch_add(1, std::memory_order_relaxed);
        return false;
      }
    }
    std::construct_at(SlotPtr(tail), std::forward<Args>(args)...);
    tail_.store(tail + 1, std::memory_order_release);
    return true;
  }

  [[nodiscard]] bool TryPop(T& out) {
    std::size_t const head = head_.load(std::memory_order_relaxed);
    if (head == cached_tail_) {
      cached_tail_ = tail_.load(std::memory_order_acquire);
      if (head == cached_tail_) {
        return false;
      }
    }
    out = std::move(*SlotPtr(head));
    std::destroy_at(SlotPtr(head));
    head_.store(head + 1, std::memory_order_release);
    return true;
  }

  std::size_t dropped() const noexcept {
    return dropped_.load(std::memory_order_relaxed);
  }

  // Consumer side only
  bool Empty() const noexcept {
    return head_.load(std::memory_order_relaxed) ==
           tail_.load(std::memory_order_acquire);
  }

 private:
  T* SlotPtr(std::size_t index) noexcept {
    return std::launder(
        reinterpret_cast<T*>(storage_ + (index & kMask) * sizeof(T)));
  }

  std::byte* const storage_;
  alignas(CacheLineSize) std::atomic<std::size_t> head_{0};  // consumer-owned
  alignas(CacheLineSize) std::atomic<std::size_t> tail_{0};  // producer-owned
  alignas(CacheLineSize) std::size_t cached_head_{0};        // producer-owned
  alignas(CacheLineSize) std::size_t cached_tail_{0};        // consumer-owned
  alignas(CacheLineSize) std::atomic<std::size_t> dropped_{0};
};

}  // namespace cts::base

#endif  // CTS_BASE_SYNC_SPSC_QUEUE_H_
