/**
 * @file latest_spsc_queue_test.cc
 * @author lipingan (lipingan.dev@outlook.com)
 * @brief LatestSpscQueue (overwrite SPSC) tests.
 */
#include "base/sync/latest_spsc_queue.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <thread>

#include <gtest/gtest.h>

namespace cts::base {
namespace {

struct Quote {  // trivially copyable
  long instrument;
  double price;
  long volume;
};

TEST(LatestSpscQueueTest, FifoBelowCapacity) {
  LatestSpscQueue<int, 8> q;
  for (int i = 0; i < 5; ++i) {
    q.Push(i);
  }
  int v = 0;
  for (int i = 0; i < 5; ++i) {
    ASSERT_TRUE(q.TryPop(v));
    EXPECT_EQ(v, i);
  }
  EXPECT_FALSE(q.TryPop(v));
  EXPECT_EQ(q.dropped(), 0u);
}

TEST(LatestSpscQueueTest, FillExactlyNoDrop) {
  LatestSpscQueue<int, 4> q;
  for (int i = 0; i < 4; ++i) {
    q.Push(i);
  }
  EXPECT_EQ(q.dropped(), 0u);
  int v = 0;
  for (int i = 0; i < 4; ++i) {
    ASSERT_TRUE(q.TryPop(v));
    EXPECT_EQ(v, i);
  }
}

// Core semantics: a full queue overwrites the oldest
TEST(LatestSpscQueueTest, OverwriteDropsOldest) {
  LatestSpscQueue<int, 4> q;
  for (int i = 0; i < 6; ++i) {  // 0..5, capacity 4
    q.Push(i);
  }
  int v = 0;
  for (int expect = 2; expect < 6; ++expect) {  // 0 and 1 overwritten
    ASSERT_TRUE(q.TryPop(v));
    EXPECT_EQ(v, expect);
  }
  EXPECT_FALSE(q.TryPop(v));
  // Drop accounting is consumer-side: counted when the consumer walks past them
  EXPECT_EQ(q.dropped(), 2u);
}

TEST(LatestSpscQueueTest, EmplaceNonDefaultConstructible) {
  LatestSpscQueue<Quote, 8> q;
  q.Emplace(Quote{.instrument = 2601, .price = 3050.5, .volume = 100});
  Quote out{};
  ASSERT_TRUE(q.TryPop(out));
  EXPECT_EQ(out.instrument, 2601);
  EXPECT_DOUBLE_EQ(out.price, 3050.5);
}

// Two-thread stress: consumer sees strictly increasing values (old dropped, new
// kept), and the invariant received + dropped == produced holds
TEST(LatestSpscQueueTest, ConcurrentStressMonotonicAndAccounted) {
  LatestSpscQueue<int, 64> q;  // deliberately small to force overwrites
  constexpr int kN = 2000000;

  std::thread producer([&] {
    for (int i = 0; i < kN; ++i) {
      q.Push(i);  // never blocks
    }
  });
  std::size_t received = 0;
  int last = -1;
  std::thread consumer([&] {
    int v = 0;
    for (;;) {
      if (q.TryPop(v)) {
        ASSERT_GT(v, last);  // strictly increasing: the old are dropped
        last = v;
        ++received;
        if (v == kN - 1) {
          break;  // last element received; wrap up
        }
      }
    }
    // drain the rest
    while (q.TryPop(v)) {
      ASSERT_GT(v, last);
      last = v;
      ++received;
    }
  });
  producer.join();
  consumer.join();

  EXPECT_EQ(last, kN - 1);  // the last element always arrives (newest is kept)
  EXPECT_EQ(received + q.dropped(), static_cast<std::size_t>(kN));
}

}  // namespace
}  // namespace cts::base

namespace cts::base {
namespace {

TEST(LatestSpscQueueTest, MinimalCapacityOneKeepsNewest) {
  LatestSpscQueue<int, 1> q;  // single slot: every Push overwrites
  for (int i = 0; i < 5; ++i) {
    q.Push(i);
  }
  int v = 0;
  ASSERT_TRUE(q.TryPop(v));
  EXPECT_EQ(v, 4);  // only the newest survives
  EXPECT_FALSE(q.TryPop(v));
  EXPECT_EQ(q.dropped(), 4u);
}

TEST(LatestSpscQueueTest, InterleavedPushPopNoLoss) {
  // Interleaved push/pop (single-threaded, deterministic): no loss when keeping
  // up. "No loss" cannot be guaranteed deterministically across threads
  // (scheduler-dependent); the concurrent paths are covered by TornRead and the
  // stress tests.
  LatestSpscQueue<int, 8> q;
  int v = 0;
  for (int i = 0; i < 10000; ++i) {
    q.Push(i);
    ASSERT_TRUE(q.TryPop(v));
    EXPECT_EQ(v, i);
  }
  EXPECT_EQ(q.dropped(), 0u);
}

// Torn-read detection: a wide struct with four identical fields; accepted items
// must agree -- a torn copy that slipped past the seq check shows up here
TEST(LatestSpscQueueTest, TornReadNeverAccepted) {
  struct alignas(32) Wide {  // spans words; overwrites are never atomic
    uint64_t a, b, c, d;
  };
  static_assert(std::is_trivially_copyable_v<Wide>);

  LatestSpscQueue<Wide, 16>
      q;  // small capacity + wide elements = high torn probability
  constexpr uint64_t kN = 500000;

  std::atomic<bool> producer_done{false};
  std::thread producer([&] {
    for (uint64_t i = 1; i <= kN; ++i) {
      q.Emplace(Wide{i, i, i, i});
    }
    producer_done.store(true, std::memory_order_release);
  });
  uint64_t received = 0;
  uint64_t last = 0;
  std::thread consumer([&] {
    Wide w{};
    for (;;) {
      if (q.TryPop(w)) {
        ASSERT_EQ(w.a, w.b);  // a torn copy fails here
        ASSERT_EQ(w.b, w.c);
        ASSERT_EQ(w.c, w.d);
        ASSERT_GT(w.a, last);  // and must increase
        last = w.a;
        ++received;
      } else if (producer_done.load(std::memory_order_acquire)) {
        break;
      }
    }
  });
  producer.join();
  consumer.join();
  EXPECT_EQ(last, kN);  // the newest must arrive
  EXPECT_EQ(static_cast<uint64_t>(q.dropped()) + received, kN);  // conservation
}

}  // namespace
}  // namespace cts::base
