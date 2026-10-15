/**
 * @file futex.h
 * @author lipingan (lipingan.dev@outlook.com)
 * @brief Raw futex primitives (signal-safe; used by shutdown/task_loop).
 */
#pragma once

#ifndef CTS_BASE_DETAIL_FUTEX_H_
#define CTS_BASE_DETAIL_FUTEX_H_

// Internal: raw futex syscalls. Do NOT mix with std::atomic::wait/notify --
// libstdc++ routes those through a waiter pool, not the value's own address,
// so the two families do not wake each other.

#include <linux/futex.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <optional>

namespace cts::base::detail {

// Signal-handler safe: a bare syscall, no libc user-space state
inline void FutexWakeOne(std::atomic<std::uint32_t>& word) {
  syscall(SYS_futex, reinterpret_cast<std::uint32_t*>(&word),
          FUTEX_WAKE_PRIVATE, 1, nullptr, nullptr, 0);
}

// Parks while word == expected, at most `timeout` (nullopt = forever)
inline void FutexWait(std::atomic<std::uint32_t>& word,
                      std::uint32_t expected,
                      std::optional<std::chrono::milliseconds> timeout) {
  struct timespec ts;
  struct timespec* tsp = nullptr;
  if (timeout.has_value()) {
    ts.tv_sec = timeout->count() / 1000;
    ts.tv_nsec = timeout->count() % 1000 * 1000000;
    tsp = &ts;
  }
  syscall(SYS_futex, reinterpret_cast<std::uint32_t*>(&word),
          FUTEX_WAIT_PRIVATE, expected, tsp, nullptr, 0);
}

}  // namespace cts::base::detail

#endif  // CTS_BASE_DETAIL_FUTEX_H_
