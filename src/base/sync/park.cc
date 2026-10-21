/**
 * @file park.cc
 * @author lipingan (lipingan.dev@outlook.com)
 * @brief Parker: zero-CPU park/notify for producer-consumer worker loops.
 */
#include "base/sync/park.h"

#include <linux/futex.h>
#include <sys/syscall.h>
#include <unistd.h>

namespace cts::base {
namespace {

// Raw futex syscalls, signal-handler safe (a bare syscall, no libc user-space
// state). Do NOT mix with std::atomic::wait/notify: libstdc++ routes those
// through a waiter pool, not the value's own address, so the two families do
// not wake each other
void FutexWakeOne(std::atomic<std::uint32_t>& word) {
  syscall(SYS_futex, reinterpret_cast<std::uint32_t*>(&word),
          FUTEX_WAKE_PRIVATE, 1, nullptr, nullptr, 0);
}

// Parks while word == expected; a spurious/EAGAIN return is fine -- the
// consumer loop re-checks its predicate on every iteration
void FutexWait(std::atomic<std::uint32_t>& word, std::uint32_t expected) {
  syscall(SYS_futex, reinterpret_cast<std::uint32_t*>(&word),
          FUTEX_WAIT_PRIVATE, expected, nullptr, nullptr, 0);
}

}  // namespace

void Parker::Notify() {
  word_.fetch_add(1, std::memory_order_release);
  // seq_cst pairs with the consumer's parked_ store
  if (parked_.load()) {
    FutexWakeOne(word_);
  }
}

void Parker::NotifyUnconditionally() {
  word_.fetch_add(1, std::memory_order_release);
  FutexWakeOne(word_);
}

void Parker::WaitWord(std::uint32_t expected) {
  FutexWait(word_, expected);
}

}  // namespace cts::base
