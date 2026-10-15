/**
 * @file shutdown_test.cc
 * @author lipingan (lipingan.dev@outlook.com)
 * @brief Shutdown flag tests.
 */
#include "base/shutdown.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <thread>

#include <gtest/gtest.h>

namespace cts::base {
namespace {

using namespace std::chrono_literals;

// One raise per process: the flag is process-global and sticky by design.
// Tolerant of test order: another suite (task_loop_test) may have raised first
TEST(ShutdownTest, ParksUntilSignalArrives) {
  InstallShutdownHandlers();
  bool const already_set = ShutdownRequested();

  std::atomic<bool> entered{false};
  std::atomic<bool> returned{false};
  std::thread waiter([&] {
    entered.store(true, std::memory_order_release);
    WaitForShutdown();
    returned.store(true, std::memory_order_release);
  });

  while (!entered.load(std::memory_order_acquire)) {
    std::this_thread::yield();
  }
  if (!already_set) {
    std::this_thread::sleep_for(50ms);
    EXPECT_FALSE(returned.load());  // still parked
    std::raise(SIGINT);             // exercises the real handler path
  }
  waiter.join();
  EXPECT_TRUE(returned.load());
  EXPECT_TRUE(ShutdownRequested());
}

}  // namespace
}  // namespace cts::base
