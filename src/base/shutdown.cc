/**
 * @file shutdown.cc
 * @author lipingan (lipingan.dev@outlook.com)
 * @brief Process-wide shutdown coordination (signal-safe flag + futex park).
 */
#include "base/shutdown.h"

#include <atomic>
#include <csignal>
#include <cstdlib>

#include "base/detail/futex.h"

namespace {

std::atomic<bool> g_shutdown{false};
std::atomic<std::atomic<std::uint32_t>*> g_wake_word{nullptr};

void HandleShutdownSignal(int sig) {
  // A lock-free atomic exchange is signal-safe per the C++ standard. The old
  // value doubles as the press counter: second press forfeits the graceful
  // path and kills now. notify_one is a bare futex-wake syscall: no libc
  // user-space state, safe in practice.
  if (g_shutdown.exchange(true, std::memory_order_acq_rel)) {
    std::_Exit(128 + sig);
  }
  g_shutdown.notify_one();
  if (auto* word = g_wake_word.load(std::memory_order_acquire)) {
    word->fetch_add(1, std::memory_order_relaxed);
    cts::base::detail::FutexWakeOne(*word);
  }
}

}  // namespace

namespace cts::base {

void InstallShutdownHandlers() {
  std::signal(SIGINT, HandleShutdownSignal);
  std::signal(SIGTERM, HandleShutdownSignal);
}

bool ShutdownRequested() {
  return g_shutdown.load(std::memory_order_acquire);
}

void WaitForShutdown() {
  // wait(false) parks while the value stays false; a store racing the park is
  // caught either by the value check (store first) or by the wake (park
  // first). The loop guards spurious wakeups
  while (!g_shutdown.load(std::memory_order_acquire)) {
    g_shutdown.wait(false, std::memory_order_acquire);
  }
}

void RegisterShutdownWakeWord(std::atomic<std::uint32_t>* word) {
  g_wake_word.store(word, std::memory_order_release);
}

void UnregisterShutdownWakeWord(std::atomic<std::uint32_t>* word) {
  std::atomic<std::uint32_t>* expected = word;
  g_wake_word.compare_exchange_strong(expected, nullptr,
                                      std::memory_order_acq_rel);
}

}  // namespace cts::base
