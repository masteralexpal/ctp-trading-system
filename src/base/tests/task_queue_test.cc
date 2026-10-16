/**
 * @file task_queue_test.cc
 * @author lipingan (lipingan.dev@outlook.com)
 * @brief TaskQueue tests.
 */
#include "base/task/task_queue.h"

#include <chrono>
#include <vector>

#include <gtest/gtest.h>

namespace cts::base {
namespace {

using namespace std::chrono_literals;

TEST(TaskQueueTest, PlainTasksAreFifo) {
  TaskQueue queue;
  std::vector<int> order;
  for (int i = 0; i < 3; ++i) {
    queue.Push([&order, i] { order.push_back(i); });
  }
  std::vector<Task> work;
  queue.TakeWork(work);
  ASSERT_EQ(work.size(), 3u);
  for (auto& task : work) {
    task();
  }
  EXPECT_EQ(order, (std::vector<int>{0, 1, 2}));
}

TEST(TaskQueueTest, TakeWorkLeavesNothingRunnable) {
  TaskQueue queue;
  queue.Push([] {});
  std::vector<Task> work;
  queue.TakeWork(work);
  EXPECT_TRUE(queue.Empty());
  EXPECT_FALSE(queue.TimeToNextTask().has_value());
}

TEST(TaskQueueTest, DelayedTaskIsNotRunnableBeforeDue) {
  TaskQueue queue;
  bool fired = false;
  queue.PushDelayed([&] { fired = true; }, 10s);

  std::vector<Task> work;
  queue.TakeWork(work);
  EXPECT_TRUE(work.empty());
  EXPECT_FALSE(fired);

  auto const to_next = queue.TimeToNextTask();
  ASSERT_TRUE(to_next.has_value());
  EXPECT_GT(*to_next, 9s);
  EXPECT_LE(*to_next, 10s + 1ms);
}

TEST(TaskQueueTest, DelayedTaskFiresWhenDue) {
  TaskQueue queue;
  queue.PushDelayed([] {}, 0ms);  // due immediately
  std::vector<Task> work;
  queue.TakeWork(work);
  EXPECT_EQ(work.size(), 1u);
}

TEST(TaskQueueTest, SameDelayKeepsInsertionOrder) {
  TaskQueue queue;
  std::vector<int> order;
  for (int i = 0; i < 3; ++i) {
    queue.PushDelayed([&order, i] { order.push_back(i); }, 0ms);
  }
  std::vector<Task> work;
  queue.TakeWork(work);
  for (auto& task : work) {
    task();
  }
  EXPECT_EQ(order, (std::vector<int>{0, 1, 2}));
}

}  // namespace
}  // namespace cts::base
