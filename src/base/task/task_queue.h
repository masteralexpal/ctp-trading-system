/**
 * @file task_queue.h
 * @author lipingan (lipingan.dev@outlook.com)
 * @brief Task queue: FIFO for plain tasks, min-heap for delayed tasks.
 */
#pragma once

#ifndef CTS_BASE_TASK_TASK_QUEUE_H_
#define CTS_BASE_TASK_TASK_QUEUE_H_

#include <chrono>
#include <cstdint>
#include <mutex>
#include <optional>
#include <queue>
#include <vector>

#include "base/task/task.h"

namespace cts::base {

// The storage half of a task loop: plain tasks FIFO, delayed tasks ordered by
// (due, seq). Thread-safe; all methods callable from any thread. Pure
// scheduling policy -- no parking, no threads, no IO.
class TaskQueue {
 public:
  TaskQueue() = default;
  ~TaskQueue() = default;

  TaskQueue(TaskQueue const&) = delete;
  TaskQueue& operator=(TaskQueue const&) = delete;

  void Push(Task task);
  void PushDelayed(Task task, std::chrono::milliseconds delay);

  // Moves every runnable task (plain + due delayed) into out, preserving order
  void TakeWork(std::vector<Task>& out);

  // Milliseconds until the next delayed task is due; nullopt if none pending.
  // Runnable-plain-tasks are gone by construction after TakeWork
  std::optional<std::chrono::milliseconds> TimeToNextTask() const;

  // True if nothing runnable right now (delayed tasks in the future count as
  // not runnable)
  bool Empty() const;

 private:
  struct DelayedTask {
    std::chrono::steady_clock::time_point due;
    std::uint64_t seq;
    Task task;
    bool operator>(DelayedTask const& other) const {
      return due != other.due ? due > other.due : seq > other.seq;
    }
  };

  mutable std::mutex mutex_;
  std::vector<Task> incoming_;
  std::priority_queue<DelayedTask, std::vector<DelayedTask>, std::greater<>>
      delayed_;
  std::uint64_t next_seq_{0};
};

}  // namespace cts::base

#endif  // CTS_BASE_TASK_TASK_QUEUE_H_
