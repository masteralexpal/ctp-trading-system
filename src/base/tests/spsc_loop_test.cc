/**
 * @file spsc_loop_test.cc
 * @author lipingan (lipingan.dev@outlook.com)
 * @brief SpscLoop tests: drain order, drop accounting, stop semantics.
 */
#include "base/spsc_loop.h"

#include <vector>

#include <gtest/gtest.h>

#include "base/run_loop.h"
#include "base/thread.h"

namespace cts::base {
namespace {

TEST(SpscLoop, DrainsInFifoOrder) {
  std::vector<int> got;
  SpscLoop<int, 16> loop{"test", [&](int const& e) {
                         EXPECT_EQ(RunLoop::Current(), &loop);
                         got.push_back(e);
                       }};
  Thread t{"spsc-test"};
  t.Start([&] { loop.Run(); });
  for (int i = 0; i < 10; ++i) {
    EXPECT_TRUE(loop.TryPush(i));
  }
  // RequestStop drains before Run returns; after the join all ten landed
  loop.RequestStop();
  t.Stop();
  ASSERT_EQ(got.size(), 10u);
  for (int i = 0; i < 10; ++i) {
    EXPECT_EQ(got[i], i);
  }
  EXPECT_EQ(loop.dropped(), 0u);
}

TEST(SpscLoop, FullQueueDropsNewestAndCounts) {
  SpscLoop<int, 4> loop{"test", [](int const&) {}};
  for (int i = 0; i < 4; ++i) {
    EXPECT_TRUE(loop.TryPush(i));
  }
  EXPECT_FALSE(loop.TryPush(4));
  EXPECT_FALSE(loop.TryPush(5));
  EXPECT_EQ(loop.dropped(), 2u);
  loop.RequestStop();  // drain-on-destruction safety: nothing runs, no hang
}

TEST(SpscLoop, StopFromInsideTheHandlerDrainsAndReturns) {
  std::vector<int> got;
  SpscLoop<int, 16> loop{"test", [&](int const& e) {
                         got.push_back(e);
                         if (e == 2) {
                           loop.RequestStop();
                         }
                       }};
  for (int i = 0; i < 5; ++i) {
    loop.TryPush(i);
  }
  loop.Run();  // on this thread; returns after draining all five
  EXPECT_EQ(got.size(), 5u);
}

}  // namespace
}  // namespace cts::base
