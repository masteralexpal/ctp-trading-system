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
#include <mutex>
#include <vector>

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
// dropped. Multiple loops may Run() concurrently: each registers its wake
// event in the shutdown registry.
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
  // Any thread. UnwatchFd removes the watcher (the fd itself stays open).
  // Safe to call from inside the callback itself: the removed task retires
  // to a graveyard cleared by the loop thread, so an executing callback is
  // never destroyed mid-run
  void WatchFd(int fd, Task cb);
  void UnwatchFd(int fd);

  void Run();
  // Any thread
  void Stop();

 private:
  // One epoll pass; returns events processed. timeout_ms -1 = park forever
  int WaitForEvents(int timeout_ms);

  TaskQueue task_queue_;
  std::mutex mutex_;  // guards watchers_ and graveyard_
  std::map<int, Task> watchers_;
  // Retired watchers, destroyed at the top of the next Run iteration (the
  // loop thread is the only cleaner, and no callback is executing there)
  std::vector<Task> graveyard_;
  WaitableEvent wake_event_{WaitableEvent::ResetPolicy::kAutomatic};
  int epoll_fd_ = -1;
  std::atomic<bool> stop_{false};
};

}  // namespace cts::base

#endif  // CTS_BASE_TASK_TASK_LOOP_H_
