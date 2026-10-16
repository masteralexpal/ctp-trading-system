/**
 * @file task_queue.cc
 * @author lipingan (lipingan.dev@outlook.com)
 * @brief Task queue: FIFO for plain tasks, min-heap for delayed tasks.
 */
#include "base/task/task_queue.h"

namespace cts::base {

void TaskQueue::Push(Task task) {
  std::lock_guard lock(mutex_);
  incoming_.push_back(std::move(task));
}

void TaskQueue::PushDelayed(Task task, std::chrono::milliseconds delay) {
  std::lock_guard lock(mutex_);
  delayed_.push(
      {std::chrono::steady_clock::now() + delay, next_seq_++, std::move(task)});
}

void TaskQueue::TakeWork(std::vector<Task>& out) {
  auto const now = std::chrono::steady_clock::now();
  std::lock_guard lock(mutex_);
  while (!delayed_.empty() && delayed_.top().due <= now) {
    // priority_queue::top() is const; the pop below discards the node
    out.push_back(std::move(const_cast<DelayedTask&>(delayed_.top()).task));
    delayed_.pop();
  }
  for (auto& task : incoming_) {
    out.push_back(std::move(task));
  }
  incoming_.clear();
}

std::optional<std::chrono::milliseconds> TaskQueue::TimeToNextTask() const {
  std::lock_guard lock(mutex_);
  if (delayed_.empty()) {
    return std::nullopt;
  }
  auto const remain = delayed_.top().due - std::chrono::steady_clock::now();
  // Ceil: truncating would wake before due and spin one extra round
  return std::chrono::duration_cast<std::chrono::milliseconds>(remain) +
         std::chrono::milliseconds{1};
}

bool TaskQueue::Empty() const {
  std::lock_guard lock(mutex_);
  if (!incoming_.empty()) {
    return false;
  }
  return delayed_.empty() ||
         delayed_.top().due > std::chrono::steady_clock::now();
}

}  // namespace cts::base
