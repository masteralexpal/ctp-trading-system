/**
 * @file task_loop.cc
 * @author lipingan (lipingan.dev@outlook.com)
 * @brief Task-centric event loop (Chromium MessagePump spirit, minimal).
 */
#include "base/task/task_loop.h"

#include <sys/epoll.h>
#include <unistd.h>

#include "base/shutdown.h"

namespace cts::base {

TaskLoop::TaskLoop() {
  epoll_fd_ = epoll_create1(EPOLL_CLOEXEC);
  epoll_event ev{};
  ev.events = EPOLLIN;
  ev.data.fd = wake_event_.fd();
  epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, wake_event_.fd(), &ev);
}

TaskLoop::~TaskLoop() {
  if (epoll_fd_ >= 0) {
    close(epoll_fd_);
  }
}

void TaskLoop::PostTask(Task task) {
  task_queue_.Push(std::move(task));
  wake_event_.Signal();
}

void TaskLoop::PostDelayedTask(Task task, std::chrono::milliseconds delay) {
  task_queue_.PushDelayed(std::move(task), delay);
  wake_event_.Signal();
}

void TaskLoop::WatchFd(int fd, Task cb) {
  {
    std::lock_guard lock(mutex_);
    watchers_.emplace(fd, std::move(cb));
    epoll_event ev{};
    ev.events = EPOLLIN;
    ev.data.fd = fd;
    epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, fd, &ev);
  }
  // A newly watched fd may already be readable while Run() parks with an
  // infinite timeout
  wake_event_.Signal();
}

void TaskLoop::UnwatchFd(int fd) {
  std::lock_guard lock(mutex_);
  auto node = watchers_.extract(fd);
  if (!node.empty()) {
    // Retire, not destroy: the loop thread may be executing this very
    // callback right now. The graveyard is cleared at a point where no
    // callback can be running
    graveyard_.push_back(std::move(node.mapped()));
  }
  epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, nullptr);
}

void TaskLoop::Stop() {
  stop_.store(true, std::memory_order_release);
  wake_event_.Signal();
}

void TaskLoop::Run() {
  RegisterShutdownWakeEvent(&wake_event_);
  std::vector<Task> work;
  for (;;) {
    // No callback is executing here: safe to destroy retired watchers
    {
      std::lock_guard lock(mutex_);
      graveyard_.clear();
    }
    task_queue_.TakeWork(work);
    for (auto& task : work) {
      task();
    }
    work.clear();

    if (stop_.load(std::memory_order_acquire) || ShutdownRequested()) {
      // Final drain of plain tasks; delayed tasks are dropped
      task_queue_.TakeWork(work);
      for (auto& task : work) {
        task();
      }
      UnregisterShutdownWakeEvent(&wake_event_);
      return;
    }

    // Zero-timeout IO sweep before parking (Chromium's Run rhythm): ready IO
    // is handled while we are awake; if any fired, loop around and re-take
    // work instead of sleeping
    if (WaitForEvents(0) > 0) {
      continue;
    }

    int timeout_ms = -1;
    if (auto const to_next = task_queue_.TimeToNextTask()) {
      timeout_ms = static_cast<int>(to_next->count());
    }

    // Park: the wake event is level-triggered, so any Signal() before this
    // point keeps it readable and epoll_wait returns immediately -- no lost
    // wakeup by construction
    WaitForEvents(timeout_ms);
  }
}

int TaskLoop::WaitForEvents(int timeout_ms) {
  epoll_event events[16];
  int const n = epoll_wait(epoll_fd_, events, 16, timeout_ms);
  for (int i = 0; i < n; ++i) {
    int const fd = events[i].data.fd;
    if (fd == wake_event_.fd()) {
      wake_event_.Reset();  // consume; the queue is re-read at loop top
      continue;
    }
    Task* cb = nullptr;
    {
      std::lock_guard lock(mutex_);
      auto const it = watchers_.find(fd);
      if (it != watchers_.end()) {
        cb = &it->second;
      }
    }
    // Raw pointer, no refcount: an UnwatchFd racing or nesting here moves the
    // task to the graveyard, keeping the pointee alive until this call ends
    if (cb != nullptr && *cb) {
      (*cb)();
    }
  }
  return n;
}

}  // namespace cts::base
