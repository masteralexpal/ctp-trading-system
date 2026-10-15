/**
 * @file task_loop.cc
 * @author lipingan (lipingan.dev@outlook.com)
 * @brief Task-centric event loop (Chromium MessageLoop spirit, minimal).
 */
#include "base/task/task_loop.h"

#include "base/detail/futex.h"
#include "base/shutdown.h"

namespace cts::base {

void TaskLoop::PostTask(Task task) {
  {
    std::lock_guard lock(mutex_);
    incoming_.push_back(std::move(task));
  }
  wake_.fetch_add(1, std::memory_order_release);
  detail::FutexWakeOne(wake_);
}

void TaskLoop::PostDelayedTask(Task task, std::chrono::milliseconds delay) {
  {
    std::lock_guard lock(mutex_);
    delayed_.push({std::chrono::steady_clock::now() + delay, next_seq_++,
                   std::move(task)});
  }
  wake_.fetch_add(1, std::memory_order_release);
  detail::FutexWakeOne(wake_);
}

void TaskLoop::Stop() {
  stop_.store(true, std::memory_order_release);
  wake_.fetch_add(1, std::memory_order_release);
  detail::FutexWakeOne(wake_);
}

void TaskLoop::Run() {
  RegisterShutdownWakeWord(&wake_);
  std::vector<Task> work;
  for (;;) {
    auto const now = std::chrono::steady_clock::now();
    {
      std::lock_guard lock(mutex_);
      while (!delayed_.empty() && delayed_.top().due <= now) {
        // priority_queue::top() is const; the pop below discards the node
        work.push_back(
            std::move(const_cast<DelayedTask&>(delayed_.top()).task));
        delayed_.pop();
      }
      for (auto& task : incoming_) {
        work.push_back(std::move(task));
      }
      incoming_.clear();
    }
    for (auto& task : work) {
      task();
    }
    work.clear();

    if (stop_.load(std::memory_order_acquire) || ShutdownRequested()) {
      // Final drain of plain tasks; delayed tasks are dropped
      {
        std::lock_guard lock(mutex_);
        for (auto& task : incoming_) {
          task();
        }
        incoming_.clear();
      }
      UnregisterShutdownWakeWord(&wake_);
      return;
    }

    // Park. Posters bump wake_ after unlocking, so the protocol is: confirm
    // empty under the mutex, sample wake_ last, then wait on that sample --
    // any post after the sample either changes the word (EAGAIN) or wakes us
    std::optional<std::chrono::milliseconds> timeout;
    std::uint32_t sample;
    {
      std::lock_guard lock(mutex_);
      if (!incoming_.empty()) {
        continue;
      }
      if (!delayed_.empty() && delayed_.top().due <= now) {
        continue;
      }
      if (!delayed_.empty()) {
        timeout = std::chrono::duration_cast<std::chrono::milliseconds>(
                      delayed_.top().due - now) +
                  std::chrono::milliseconds{1};
      }
      sample = wake_.load(std::memory_order_acquire);
    }
    detail::FutexWait(wake_, sample, timeout);
  }
}

}  // namespace cts::base
