/**
 * @file waitable_event_test.cc
 * @author lipingan (lipingan.dev@outlook.com)
 * @brief WaitableEvent tests.
 */
#include "base/sync/waitable_event.h"

#include <atomic>
#include <chrono>
#include <thread>

#include <gtest/gtest.h>

namespace cts::base {
namespace {

using namespace std::chrono_literals;

TEST(WaitableEventTest, InitialState) {
  WaitableEvent off{WaitableEvent::ResetPolicy::kManual};
  EXPECT_FALSE(off.IsSignaled());
  WaitableEvent on{WaitableEvent::ResetPolicy::kManual,
                   WaitableEvent::InitialState::kSignaled};
  EXPECT_TRUE(on.IsSignaled());
}

TEST(WaitableEventTest, SignalWakesWaiter) {
  WaitableEvent ev{WaitableEvent::ResetPolicy::kAutomatic};
  std::atomic<bool> woken{false};
  std::thread t([&] {
    ev.Wait();
    woken.store(true);
  });
  std::this_thread::sleep_for(30ms);
  EXPECT_FALSE(woken.load());
  ev.Signal();
  t.join();
  EXPECT_TRUE(woken.load());
}

TEST(WaitableEventTest, AutomaticResetReleasesExactlyOne) {
  WaitableEvent ev{WaitableEvent::ResetPolicy::kAutomatic};
  ev.Signal();
  EXPECT_TRUE(ev.TimedWait(50ms));   // consumes the one unit
  EXPECT_FALSE(ev.TimedWait(50ms));  // nothing left
}

TEST(WaitableEventTest, ManualResetStaysSignaledUntilReset) {
  WaitableEvent ev{WaitableEvent::ResetPolicy::kManual};
  ev.Signal();
  EXPECT_TRUE(ev.TimedWait(50ms));
  EXPECT_TRUE(ev.TimedWait(50ms));  // still signaled: non-destructive
  ev.Reset();
  EXPECT_FALSE(ev.TimedWait(50ms));
}

TEST(WaitableEventTest, TimedWaitTimesOut) {
  WaitableEvent ev{WaitableEvent::ResetPolicy::kManual};
  auto const begin = std::chrono::steady_clock::now();
  EXPECT_FALSE(ev.TimedWait(60ms));
  EXPECT_GE(std::chrono::steady_clock::now() - begin, 60ms);
}

TEST(WaitableEventTest, IsSignaledIsNonDestructive) {
  WaitableEvent ev{WaitableEvent::ResetPolicy::kAutomatic};
  ev.Signal();
  EXPECT_TRUE(ev.IsSignaled());
  EXPECT_TRUE(ev.IsSignaled());  // reading the state consumed nothing
  EXPECT_TRUE(ev.TimedWait(50ms));
}

}  // namespace
}  // namespace cts::base
