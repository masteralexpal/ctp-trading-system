/**
 * @file md_dispatcher_test.cc
 * @author lipingan (lipingan.dev@outlook.com)
 * @brief MdDispatcher tests.
 */
#include "md/md_dispatcher.h"

#include <chrono>
#include <cstring>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "base/thread.h"

namespace cts::md {
namespace {

// Recording listener: captures the event sequence for assertions
struct RecordingListener : MdListener {
  std::vector<std::string> events;

  void OnTick(Tick const& tick) override {
    events.emplace_back(std::string("tick:") + tick.instrument_id);
  }
  void OnMdState(bool connected, bool login) override {
    events.emplace_back("state:" + std::to_string(connected) + "," +
                        std::to_string(login));
  }
  void OnMdError(int error_id, std::string_view msg) override {
    events.emplace_back("error:" + std::to_string(error_id) + ":" +
                        std::string(msg));
  }
};

// Run on a dedicated thread until the condition holds or times out
template <typename F>
bool RunUntil(MdDispatcher& dispatcher, base::Thread& thread, F&& cond) {
  thread.Start([&] { dispatcher.Run(); });
  auto const deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds{5};
  while (!cond() && std::chrono::steady_clock::now() < deadline) {
    std::this_thread::yield();
  }
  dispatcher.RequestStop();
  thread.Stop();
  return cond();
}

TEST(MdDispatcherTest, EventsFanOutInOrder) {
  MdDispatcher dispatcher;
  RecordingListener listener;
  dispatcher.AddListener(listener);

  Tick tick{};
  std::strncpy(tick.instrument_id, "rb2701", sizeof(tick.instrument_id) - 1);

  // Enqueue first, then start the consumer (queue decouples the two)
  dispatcher.OnMdConnected();
  dispatcher.OnMdLogin(true, "ok");
  dispatcher.OnMdTick(tick);
  dispatcher.OnMdError(7, "bad");

  base::Thread thread{"test-md-dispatch"};
  bool const ok =
      RunUntil(dispatcher, thread, [&] { return listener.events.size() >= 4; });
  ASSERT_TRUE(ok);
  ASSERT_EQ(listener.events.size(), 4u);
  EXPECT_EQ(listener.events[0], "state:1,0");
  EXPECT_EQ(listener.events[1], "state:1,1");
  EXPECT_EQ(listener.events[2], "tick:rb2701");
  EXPECT_EQ(listener.events[3], "error:7:bad");
}

TEST(MdDispatcherTest, MultipleListenersAllReceive) {
  MdDispatcher dispatcher;
  RecordingListener a, b;
  dispatcher.AddListener(a);
  dispatcher.AddListener(b);

  dispatcher.OnMdConnected();

  base::Thread thread{"test-md-dispatch"};
  bool const ok = RunUntil(dispatcher, thread, [&] {
    return a.events.size() >= 1 && b.events.size() >= 1;
  });
  ASSERT_TRUE(ok);
  EXPECT_EQ(a.events, b.events);
}

TEST(MdDispatcherTest, RequestStopIsIdempotent) {
  MdDispatcher dispatcher;
  dispatcher.RequestStop();
  dispatcher.RequestStop();  // simply must not crash

  // Run must return immediately after RequestStop (empty queue)
  base::Thread thread{"test-md-dispatch"};
  thread.Start([&] { dispatcher.Run(); });
  thread.Stop();
  SUCCEED();
}

TEST(MdDispatcherTest, OverflowDropsAndCounts) {
  MdDispatcher dispatcher;  // no consumer thread: the queue must overflow
  Tick tick{};
  constexpr int kPush = 5000;  // capacity 4096
  for (int i = 0; i < kPush; ++i) {
    dispatcher.OnMdTick(tick);
  }
  EXPECT_EQ(dispatcher.dropped_events(),
            static_cast<std::size_t>(kPush) - 4096);
}

TEST(MdDispatcherTest, DisconnectEventOrder) {
  MdDispatcher dispatcher;
  RecordingListener listener;
  dispatcher.AddListener(listener);

  dispatcher.OnMdConnected();
  dispatcher.OnMdDisconnected(4097);  // the common heartbeat-timeout reason

  base::Thread thread{"test-md-dispatch"};
  bool const ok =
      RunUntil(dispatcher, thread, [&] { return listener.events.size() >= 2; });
  ASSERT_TRUE(ok);
  EXPECT_EQ(listener.events[0], "state:1,0");
  EXPECT_EQ(listener.events[1], "state:0,0");
}

}  // namespace
}  // namespace cts::md
