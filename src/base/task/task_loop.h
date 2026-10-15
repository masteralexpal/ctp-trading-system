/**
 * @file task_loop.h
 * @author lipingan (lipingan.dev@outlook.com)
 * @brief Task-centric event loop (Chromium MessageLoop spirit, minimal).
 */
#pragma once

#ifndef CTS_BASE_TASK_TASK_LOOP_H_
#define CTS_BASE_TASK_TASK_LOOP_H_

#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <queue>
#include <vector>

#include "base/task/task.h"

namespace cts::base {
class TaskLoop {
 public:
  TaskLoop() = default;
  ~TaskLoop() = default;

  TaskLoop(TaskLoop const&) = delete;
  TaskLoop& operator=(TaskLoop const&) = delete;

  // Any thread
  void PostTask(Task task);
  void PostDelayedTask(Task task, std::chrono::milliseconds delay);

  void Run();
  // Any thread
  void Stop();

 private:
  struct DelayedTask {
    std::chrono::steady_clock::time_point due;
    std::uint64_t seq;
    Task task;
    bool operator>(DelayedTask const& other) const {
      return due != other.due ? due > other.due : seq > other.seq;
    }
  };

  std::mutex mutex_;
  std::vector<Task> incoming_;
  std::priority_queue<DelayedTask, std::vector<DelayedTask>, std::greater<>>
      delayed_;
  std::uint64_t next_seq_{0};
  std::atomic<std::uint32_t> wake_{0};
  std::atomic<bool> stop_{false};
};

}  // namespace cts::base

#endif  // CTS_BASE_TASK_TASK_LOOP_H_
