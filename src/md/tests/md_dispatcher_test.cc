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

#include "base/threading/thread.h"

namespace cts::md {
namespace {

// Recording client: captures the event sequence for assertions
struct RecordingClient : MdClient {
  std::vector<std::string> events;

  void OnMdConnected() override { events.emplace_back("connected"); }
  void OnMdDisconnected(int reason) override {
    events.emplace_back("disconnected:" + std::to_string(reason));
  }
  void OnMdLogin(bool success, std::string_view msg) override {
    events.emplace_back(std::string("login:") + (success ? "ok:" : "fail:") +
                        std::string(msg));
  }
  void OnTick(Tick const& tick) override {
    events.emplace_back(std::string("tick:") + tick.instrument_id);
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
  RecordingClient client;
  dispatcher.AddClient(client);

  Tick tick{};
  std::strncpy(tick.instrument_id, "rb2701", sizeof(tick.instrument_id) - 1);

  // Enqueue first, then start the consumer (queue decouples the two)
  dispatcher.OnMdConnected();
  dispatcher.OnMdLogin(true, "");
  dispatcher.OnMdTick(tick);
  dispatcher.OnMdError(7, "bad");

  base::Thread thread{"test-md-dispatch"};
  bool const ok =
      RunUntil(dispatcher, thread, [&] { return client.events.size() >= 4; });
  ASSERT_TRUE(ok);
  ASSERT_EQ(client.events.size(), 4u);
  EXPECT_EQ(client.events[0], "connected");
  EXPECT_EQ(client.events[1], "login:ok:");
  EXPECT_EQ(client.events[2], "tick:rb2701");
  EXPECT_EQ(client.events[3], "error:7:bad");
}

TEST(MdDispatcherTest, MultipleClientsAllReceive) {
  MdDispatcher dispatcher;
  RecordingClient a, b;
  dispatcher.AddClient(a);
  dispatcher.AddClient(b);

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
  RecordingClient client;
  dispatcher.AddClient(client);

  dispatcher.OnMdConnected();
  dispatcher.OnMdDisconnected(4097);  // the common heartbeat-timeout reason

  base::Thread thread{"test-md-dispatch"};
  bool const ok =
      RunUntil(dispatcher, thread, [&] { return client.events.size() >= 2; });
  ASSERT_TRUE(ok);
  EXPECT_EQ(client.events[0], "connected");
  EXPECT_EQ(client.events[1], "disconnected:4097");
}

TEST(MdDispatcherTest, LoginStateIsTrackedByDispatcher) {
  MdDispatcher dispatcher;
  RecordingClient client;
  dispatcher.AddClient(client);

  EXPECT_FALSE(dispatcher.logged_in());
  dispatcher.OnMdConnected();
  dispatcher.OnMdLogin(true, "");

  base::Thread thread{"test-md-dispatch"};
  bool const ok =
      RunUntil(dispatcher, thread, [&] { return dispatcher.logged_in(); });
  ASSERT_TRUE(ok);

  // A later disconnect flips it back
  MdDispatcher dispatcher2;
  RecordingClient client2;
  dispatcher2.AddClient(client2);
  dispatcher2.OnMdLogin(true, "");
  dispatcher2.OnMdDisconnected(4097);

  base::Thread thread2{"test-md-dispatch"};
  bool const ok2 = RunUntil(dispatcher2, thread2,
                            [&] { return client2.events.size() >= 2; });
  ASSERT_TRUE(ok2);
  EXPECT_FALSE(dispatcher2.logged_in());
}

TEST(MdDispatcherTest, RequestStopWakesParkedLoop) {
  MdDispatcher dispatcher;  // nothing queued: Run parks immediately
  base::Thread thread{"test-md-dispatch"};
  thread.Start([&] { dispatcher.Run(); });

  std::this_thread::sleep_for(std::chrono::milliseconds{50});  // let it park
  auto const begin = std::chrono::steady_clock::now();
  dispatcher.RequestStop();
  thread.Stop();  // joins; hangs forever if the wake is broken
  EXPECT_LT(std::chrono::steady_clock::now() - begin, std::chrono::seconds{5});
}

TEST(MdDispatcherTest, PushWakesParkedConsumer) {
  MdDispatcher dispatcher;
  RecordingClient client;
  dispatcher.AddClient(client);

  base::Thread thread{"test-md-dispatch"};
  thread.Start([&] { dispatcher.Run(); });
  std::this_thread::sleep_for(std::chrono::milliseconds{50});  // let it park

  dispatcher.OnMdConnected();
  auto const deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds{5};
  while (client.events.empty() && std::chrono::steady_clock::now() < deadline) {
    std::this_thread::yield();
  }
  dispatcher.RequestStop();
  thread.Stop();
  ASSERT_EQ(client.events.size(), 1u);
  EXPECT_EQ(client.events[0], "connected");
}

}  // namespace
}  // namespace cts::md
