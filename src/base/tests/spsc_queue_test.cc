/**
 * @file spsc_queue_test.cc
 * @author lipingan (lipingan.dev@outlook.com)
 * @brief SpscQueue (drop-newest SPSC) tests.
 */
#include "base/spsc_queue.h"

#include <cstddef>
#include <string>
#include <thread>
#include <utility>

#include <gtest/gtest.h>

namespace cts::base {
namespace {

// Non-default-constructible, non-copyable: verifies raw storage + in-place
// construction
struct MoveOnly {
  MoveOnly(int v, std::string s) : n(v), name(std::move(s)) {}
  MoveOnly(MoveOnly const&) = delete;
  MoveOnly(MoveOnly&&) = default;
  MoveOnly& operator=(MoveOnly&&) = default;
  int n;
  std::string name;
};

// Tracks live object count to verify destruction semantics
struct Tracker {
  static inline int alive = 0;
  Tracker() { ++alive; }
  Tracker(Tracker const&) { ++alive; }
  Tracker(Tracker&&) { ++alive; }
  Tracker& operator=(Tracker const&) { return *this; }
  ~Tracker() { --alive; }
};

TEST(SpscQueueTest, EmptyPopReturnsFalse) {
  SpscQueue<int, 8> q;
  int v = 0;
  EXPECT_FALSE(q.TryPop(v));
}

TEST(SpscQueueTest, FifoOrder) {
  SpscQueue<int, 8> q;
  for (int i = 0; i < 5; ++i) {
    ASSERT_TRUE(q.TryPush(i));
  }
  int v = 0;
  for (int i = 0; i < 5; ++i) {
    ASSERT_TRUE(q.TryPop(v));
    EXPECT_EQ(v, i);
  }
  EXPECT_FALSE(q.TryPop(v));
}

TEST(SpscQueueTest, EmplaceMoveOnlyType) {
  SpscQueue<MoveOnly, 8> q;
  ASSERT_TRUE(q.TryEmplace(1, "one"));
  ASSERT_TRUE(q.TryEmplace(2, "two"));
  MoveOnly out{0, ""};
  ASSERT_TRUE(q.TryPop(out));
  EXPECT_EQ(out.n, 1);
  EXPECT_EQ(out.name, "one");
  ASSERT_TRUE(q.TryPop(out));
  EXPECT_EQ(out.n, 2);
  EXPECT_FALSE(q.TryPop(out));
}

TEST(SpscQueueTest, FullQueueDropsNewAndCounts) {
  SpscQueue<int, 4> q;
  for (int i = 0; i < 4; ++i) {
    ASSERT_TRUE(q.TryPush(i));
  }
  EXPECT_FALSE(q.TryPush(99));
  EXPECT_FALSE(q.TryPush(100));
  EXPECT_EQ(q.dropped(), 2u);

  // Existing elements are unaffected
  int v = 0;
  for (int i = 0; i < 4; ++i) {
    ASSERT_TRUE(q.TryPop(v));
    EXPECT_EQ(v, i);
  }
}

TEST(SpscQueueTest, SlotsReuseAfterWrapAround) {
  SpscQueue<int, 4> q;
  // Push/pop several rounds; slot reuse across capacity wrap-around
  for (int round = 0; round < 3; ++round) {
    for (int i = 0; i < 4; ++i) {
      ASSERT_TRUE(q.TryPush(round * 10 + i));
    }
    int v = 0;
    for (int i = 0; i < 4; ++i) {
      ASSERT_TRUE(q.TryPop(v));
      EXPECT_EQ(v, round * 10 + i);
    }
  }
}

TEST(SpscQueueTest, DestructorDrainsRemainingElements) {
  Tracker::alive = 0;
  {
    SpscQueue<Tracker, 8> q;
    for (int i = 0; i < 5; ++i) {
      ASSERT_TRUE(q.TryEmplace());
    }
    Tracker t;
    ASSERT_TRUE(q.TryPop(t));      // one popped out
    EXPECT_EQ(Tracker::alive, 5);  // t + 4 still queued
  }
  EXPECT_EQ(Tracker::alive,
            0);  // queue drained on destruction; t goes out of scope
}

TEST(SpscQueueTest, ConcurrentStressPreservesOrder) {
  SpscQueue<int, 1024> q;
  constexpr int kN = 1000000;
  std::size_t retries = 0;

  std::thread producer([&] {
    for (int i = 0; i < kN; ++i) {
      while (!q.TryPush(i)) {
        ++retries;  // retry on full queue; not a loss
      }
    }
  });
  std::thread consumer([&] {
    int v = 0;
    for (int expect = 0; expect < kN;) {
      if (q.TryPop(v)) {
        ASSERT_EQ(v, expect);  // strict FIFO, zero reordering
        ++expect;
      }
    }
  });
  producer.join();
  consumer.join();

  // producer retries == queue drop count (every failed TryPush was retried)
  EXPECT_EQ(retries, q.dropped());
}

}  // namespace
}  // namespace cts::base

namespace cts::base {
namespace {

TEST(SpscQueueTest, RvaluePushOverload) {
  SpscQueue<std::string, 4> q;
  std::string s = "moved";
  ASSERT_TRUE(q.TryPush(std::move(s)));  // rvalue overload
  std::string const c = "copied";
  ASSERT_TRUE(q.TryPush(c));  // lvalue overload
  std::string out;
  ASSERT_TRUE(q.TryPop(out));
  EXPECT_EQ(out, "moved");
  ASSERT_TRUE(q.TryPop(out));
  EXPECT_EQ(out, "copied");
}

TEST(SpscQueueTest, MinimalCapacityOne) {
  SpscQueue<int, 1> q;
  ASSERT_TRUE(q.TryPush(1));
  ASSERT_FALSE(q.TryPush(2));  // full
  int v = 0;
  ASSERT_TRUE(q.TryPop(v));
  EXPECT_EQ(v, 1);
  ASSERT_TRUE(q.TryPush(3));  // reusable after pop
  ASSERT_TRUE(q.TryPop(v));
  EXPECT_EQ(v, 3);
}

TEST(SpscQueueTest, InterleavedPushPop) {
  SpscQueue<int, 4> q;
  int v = 0;
  for (int i = 0; i < 1000; ++i) {
    ASSERT_TRUE(q.TryPush(i));
    ASSERT_TRUE(q.TryPop(v));
    EXPECT_EQ(v, i);
  }
  EXPECT_EQ(q.dropped(), 0u);
}

TEST(SpscQueueTest, DropsAccumulateAcrossCycles) {
  SpscQueue<int, 4> q;
  // Three rounds of "fill + overflow 2 + drain"; drops should total 6
  for (int round = 0; round < 3; ++round) {
    for (int i = 0; i < 6; ++i) {
      bool rv = q.TryPush(i);
    }
    int v = 0;
    while (q.TryPop(v)) {
    }
  }
  EXPECT_EQ(q.dropped(), 6u);
}

}  // namespace
}  // namespace cts::base
