/**
 * @file shutdown.cc
 * @author lipingan (lipingan.dev@outlook.com)
 * @brief Process-wide shutdown coordination (signal-safe flag + futex park).
 */
#include "base/shutdown.h"

#include <atomic>
#include <csignal>
#include <cstdlib>

#include "base/sync/waitable_event.h"

namespace {

std::atomic<bool> g_shutdown{false};

// Wake-event registry: a fixed array of atomic slots (no container, no lock)
// because the shutdown SIGNAL HANDLER walks it -- only bare atomic ops are
// allowed there. Capacity 4: main loop + md/td dispatch loops + headroom
constexpr std::size_t kMaxWakeEvents = 4;
std::atomic<cts::base::WaitableEvent*> g_wake_events[kMaxWakeEvents] = {};

void HandleShutdownSignal(int sig) {
  // A lock-free atomic exchange is signal-safe per the C++ standard. The old
  // value doubles as the press counter: second press forfeits the graceful
  // path and kills now. WaitableEvent::Signal() is a bare write(2).
  if (g_shutdown.exchange(true, std::memory_order_acq_rel)) {
    std::_Exit(128 + sig);
  }
  g_shutdown.notify_one();
  for (auto& slot : g_wake_events) {
    if (auto* event = slot.load(std::memory_order_acquire)) {
      event->Signal();
    }
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

void RequestShutdown() {
  if (!g_shutdown.exchange(true, std::memory_order_acq_rel)) {
    g_shutdown.notify_one();
    for (auto& slot : g_wake_events) {
      if (auto* event = slot.load(std::memory_order_acquire)) {
        event->Signal();
      }
    }
  }
}

void WaitForShutdown() {
  // wait(false) parks while the value stays false; a store racing the park is
  // caught either by the value check (store first) or by the wake (park
  // first). The loop guards spurious wakeups
  while (!g_shutdown.load(std::memory_order_acquire)) {
    g_shutdown.wait(false, std::memory_order_acquire);
  }
}

void RegisterShutdownWakeEvent(WaitableEvent* event) {
  for (auto& slot : g_wake_events) {
    WaitableEvent* expected = nullptr;
    if (slot.compare_exchange_strong(expected, event,
                                     std::memory_order_acq_rel)) {
      return;
    }
  }
  // No free slot: more loops than kMaxWakeEvents -- programming error, but
  // shutdown correctness is preserved for the registered ones
}

void UnregisterShutdownWakeEvent(WaitableEvent* event) {
  for (auto& slot : g_wake_events) {
    WaitableEvent* expected = event;
    if (slot.compare_exchange_strong(expected, nullptr,
                                     std::memory_order_acq_rel)) {
      return;
    }
  }
}

}  // namespace cts::base
