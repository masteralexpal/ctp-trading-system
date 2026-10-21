/**
 * @file task_loop.h
 * @author lipingan (lipingan.dev@outlook.com)
 * @brief Task-centric event loop (Chromium MessagePump spirit, minimal).
 */
#pragma once

#ifndef CTS_BASE_THREADING_TASK_LOOP_H_
#define CTS_BASE_THREADING_TASK_LOOP_H_

#include <atomic>
#include <chrono>
#include <map>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#include "base/threading/run_loop.h"
#include "base/threading/task.h"
#include "base/threading/task_queue.h"
#include "base/sync/waitable_event.h"

namespace cts::base {

// Task-centric RunLoop backend: a TaskQueue plus an epoll set for parking
// (wake event + watched fds). Multi-producer: PostTask is any-thread. Plain
// tasks run FIFO; delayed tasks fire at earliest after their delay (no
// cancellation). Run() parks with zero CPU, drains plain tasks on stop and
// drops pending delayed ones; a process shutdown request (base/shutdown.h)
// also ends it
class TaskLoop final : public RunLoop {
 public:
  explicit TaskLoop(std::string_view name = "task");
  ~TaskLoop() override;

  TaskLoop(TaskLoop const&) = delete;
  TaskLoop& operator=(TaskLoop const&) = delete;

  std::string_view name() const override { return name_; }

  // Any thread
  void PostTask(Task task);
  void PostDelayedTask(Task task, std::chrono::milliseconds delay);

  // Runs cb on the loop thread whenever fd is readable (level-triggered).
  // Any thread. Safe to UnwatchFd from inside the callback itself: the
  // removed task retires to a graveyard cleared between callbacks, so an
  // executing callback is never destroyed mid-run
  void WatchFd(int fd, Task cb);
  void UnwatchFd(int fd);

  // RunLoop
  void RequestStop() override;
  void Wakeup() override { wake_event_.Signal(); }

 protected:
  void RunImpl() override;

 private:
  // One epoll pass; returns events processed. timeout_ms -1 = park forever
  int WaitForEvents(int timeout_ms);

  std::string name_;
  TaskQueue task_queue_;
  std::mutex mutex_;  // guards watchers_ and graveyard_
  std::map<int, Task> watchers_;
  // Retired watchers, destroyed at the top of the next Run iteration
  std::vector<Task> graveyard_;
  WaitableEvent wake_event_{WaitableEvent::ResetPolicy::kAutomatic};
  int epoll_fd_ = -1;
  std::atomic<bool> stop_{false};
};

}  // namespace cts::base

#endif  // CTS_BASE_THREADING_TASK_LOOP_H_
