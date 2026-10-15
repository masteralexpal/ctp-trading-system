/**
 * @file task_loop_test.cc
 * @author lipingan (lipingan.dev@outlook.com)
 * @brief TaskLoop tests.
 */
#include "base/task/task_loop.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "base/shutdown.h"
#include "base/thread.h"

namespace cts::base {
namespace {

using namespace std::chrono_literals;

TEST(TaskLoopTest, RunsPostedTasksInFifoOrder) {
  TaskLoop loop;
  std::vector<int> order;
  for (int i = 0; i < 3; ++i) {
    loop.PostTask([&order, i] { order.push_back(i); });
  }
  loop.PostTask([&loop] { loop.Stop(); });

  Thread thread{"test-loop"};
  thread.Start([&] { loop.Run(); });
  thread.Stop();

  EXPECT_EQ(order, (std::vector<int>{0, 1, 2}));
}

TEST(TaskLoopTest, TasksMayPostTasks) {
  TaskLoop loop;
  std::vector<std::string> order;
  loop.PostTask([&] {
    order.emplace_back("outer");
    loop.PostTask([&] { order.emplace_back("inner"); });
    loop.PostTask([&] { order.emplace_back("after-inner"); });
  });
  loop.PostTask([&] { order.emplace_back("last"); });
  loop.PostTask([&loop] { loop.Stop(); });

  Thread thread{"test-loop"};
  thread.Start([&] { loop.Run(); });
  thread.Stop();

  // The Stop task was already queued when "outer" posted its children:
  // inner tasks run in the next drain round, Stop keeps its FIFO position
  EXPECT_EQ(order, (std::vector<std::string>{"outer", "last", "inner",
                                             "after-inner"}));
}

TEST(TaskLoopTest, DelayedTaskFiresAfterDelay) {
  TaskLoop loop;
  auto const start = std::chrono::steady_clock::now();
  std::chrono::milliseconds elapsed{0};
  loop.PostDelayedTask(
      [&] {
        elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start);
        loop.Stop();
      },
      100ms);

  Thread thread{"test-loop"};
  thread.Start([&] { loop.Run(); });
  thread.Stop();

  EXPECT_GE(elapsed, 100ms);
  EXPECT_LT(elapsed, 5s);  // generous ceiling, timing-only check
}

TEST(TaskLoopTest, StopWakesParkedLoop) {
  TaskLoop loop;  // nothing posted: Run parks immediately
  Thread thread{"test-loop"};
  thread.Start([&] { loop.Run(); });

  std::this_thread::sleep_for(50ms);  // let it park
  auto const begin = std::chrono::steady_clock::now();
  loop.Stop();
  thread.Stop();  // joins; hangs forever if the wake is broken
  EXPECT_LT(std::chrono::steady_clock::now() - begin, 5s);
}

TEST(TaskLoopTest, PlainTasksDrainOnStop) {
  TaskLoop loop;
  std::atomic<int> ran{0};
  Thread thread{"test-loop"};
  thread.Start([&] { loop.Run(); });
  std::this_thread::sleep_for(50ms);  // parked

  loop.PostTask([&] { ++ran; });
  loop.PostTask([&] { ++ran; });
  loop.Stop();
  thread.Stop();
  EXPECT_EQ(ran.load(), 2);
}

// Tolerant of test order: shutdown_test may have raised a signal already
TEST(TaskLoopTest, ShutdownRequestStopsRun) {
  InstallShutdownHandlers();
  TaskLoop loop;
  Thread thread{"test-loop"};
  thread.Start([&] { loop.Run(); });
  std::this_thread::sleep_for(50ms);  // parked

  if (!ShutdownRequested()) {
    std::raise(SIGTERM);  // handler bumps the loop's wake word
  }
  auto const begin = std::chrono::steady_clock::now();
  thread.Stop();  // joins; Run must have returned on its own
  EXPECT_LT(std::chrono::steady_clock::now() - begin, 5s);
}

TEST(TaskLoopTest, PendingDelayedTaskIsDroppedOnStop) {
  TaskLoop loop;
  std::atomic<bool> fired{false};
  loop.PostDelayedTask([&] { fired.store(true); }, 10s);
  loop.PostTask([&loop] { loop.Stop(); });

  Thread thread{"test-loop"};
  thread.Start([&] { loop.Run(); });
  thread.Stop();
  EXPECT_FALSE(fired.load());
}

}  // namespace
}  // namespace cts::base
