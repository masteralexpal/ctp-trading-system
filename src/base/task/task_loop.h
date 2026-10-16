/**
 * @file task_loop.h
 * @author lipingan (lipingan.dev@outlook.com)
 * @brief Task-centric event loop (Chromium MessagePump spirit, minimal).
 */
#pragma once

#ifndef CTS_BASE_TASK_TASK_LOOP_H_
#define CTS_BASE_TASK_TASK_LOOP_H_

#include <atomic>
#include <chrono>
#include <map>
#include <memory>
#include <mutex>

#include "base/task/task.h"
#include "base/task/task_queue.h"
#include "base/waitable_event.h"

namespace cts::base {

// Task-centric loop in the Chromium MessagePump spirit: a TaskQueue for
// scheduling plus an epoll set for parking. Plain tasks run FIFO; delayed
// tasks fire at earliest after their delay (steady clock, no cancellation).
//
// Run() parks on epoll (zero CPU): the set holds the wake event plus any
// watched fds. It returns after Stop() or a process shutdown request
// (base/shutdown.h), draining queued plain tasks; pending delayed tasks are
// dropped. One active Run() per process: it registers the loop's wake event
// for shutdown notification.
class TaskLoop {
 public:
  TaskLoop();
  ~TaskLoop();

  TaskLoop(TaskLoop const&) = delete;
  TaskLoop& operator=(TaskLoop const&) = delete;

  // Any thread
  void PostTask(Task task);
  void PostDelayedTask(Task task, std::chrono::milliseconds delay);

  // Runs cb on the loop thread whenever fd is readable (level-triggered).
  // Any thread. UnwatchFd removes the watcher (the fd itself stays open)
  void WatchFd(int fd, Task cb);
  void UnwatchFd(int fd);

  void Run();
  // Any thread
  void Stop();

 private:
  // One epoll pass; returns events processed. timeout_ms -1 = park forever
  int WaitForEvents(int timeout_ms);

  TaskQueue task_queue_;
  std::mutex mutex_;                               // guards watchers_ only
  std::map<int, std::shared_ptr<Task>> watchers_;  // shared_ptr: move-only Task
  WaitableEvent wake_event_{WaitableEvent::ResetPolicy::kAutomatic};
  int epoll_fd_ = -1;
  std::atomic<bool> stop_{false};
};

}  // namespace cts::base

#endif  // CTS_BASE_TASK_TASK_LOOP_H_
