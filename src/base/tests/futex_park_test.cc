/**
 * @file futex_park_test.cc
 * @author lipingan (lipingan.dev@outlook.com)
 * @brief Unit tests for base::FutexPark.
 */
#include "base/futex_park.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <thread>

#include <gtest/gtest.h>

namespace cts::base {
namespace {

TEST(FutexParkTest, WaitUnlessReturnsImmediatelyWhenWorkPending) {
  FutexPark park;
  // Must not park: the predicate is already true
  park.WaitUnless([] { return true; });
}

TEST(FutexParkTest, NotifyWakesParkedConsumer) {
  FutexPark park;
  std::atomic<bool> work{false};
  std::atomic<bool> consumed{false};

  std::thread consumer([&] {
    park.WaitUnless([&] { return work.load(std::memory_order_acquire); });
    consumed.store(true, std::memory_order_release);
  });

  // Let the consumer actually park before notifying
  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  work.store(true, std::memory_order_release);
  park.Notify();

  consumer.join();
  EXPECT_TRUE(consumed.load());
}

// The postmortem case (Documents/ctp/futex_lost_wakeup_postmortem.md): a
// notify landing between the predicate check and the futex wait must not be
// lost. Hammer the interleaving
TEST(FutexParkTest, NoLostWakeupUnderStress) {
  constexpr std::uint64_t kTotal = 200000;
  FutexPark park;
  std::atomic<std::uint64_t> produced{0};
  std::atomic<bool> stop{false};
  std::uint64_t consumed = 0;

  std::thread producer([&] {
    for (std::uint64_t i = 0; i < kTotal; ++i) {
      produced.fetch_add(1, std::memory_order_release);
      park.Notify();
    }
    stop.store(true, std::memory_order_release);
    park.NotifyUnconditionally();
  });

  // Watchdog: a lost wakeup hangs the consumer; break the park and fail
  std::atomic<bool> done{false};
  std::atomic<bool> give_up{false};
  std::thread watchdog([&] {
    for (int i = 0; i < 1500 && !done.load(std::memory_order_acquire); ++i) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    if (!done.load(std::memory_order_acquire)) {
      give_up.store(true, std::memory_order_release);
      park.NotifyUnconditionally();
    }
  });

  while (consumed < kTotal && !give_up.load(std::memory_order_acquire)) {
    while (consumed < produced.load(std::memory_order_acquire)) {
      ++consumed;
    }
    if (consumed >= kTotal) {
      break;
    }
    park.WaitUnless([&] {
      return stop.load(std::memory_order_acquire) ||
             give_up.load(std::memory_order_acquire) ||
             consumed < produced.load(std::memory_order_acquire);
    });
  }

  producer.join();
  done.store(true, std::memory_order_release);
  watchdog.join();
  EXPECT_EQ(consumed, kTotal);
}

TEST(FutexParkTest, NotifyUnconditionallyWakesWithoutWork) {
  FutexPark park;
  // The stop flag is part of the predicate (the dispatcher pattern): a wake
  // landing before the futex wait is caught by the re-check, not lost
  std::atomic<bool> stop{false};

  std::thread consumer([&] {
    park.WaitUnless([&] { return stop.load(std::memory_order_acquire); });
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  stop.store(true, std::memory_order_release);
  park.NotifyUnconditionally();

  consumer.join();
  EXPECT_TRUE(stop.load());
}

}  // namespace
}  // namespace cts::base
