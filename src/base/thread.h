/**
 * @file thread.h
 * @author lipingan (lipingan.dev@outlook.com)
 * @brief Named dedicated thread handle (name visible in gdb/top).
 */
#pragma once

#ifndef CTS_BASE_THREAD_H_
#define CTS_BASE_THREAD_H_

#include <pthread.h>

#include <atomic>
#include <string>
#include <thread>
#include <utility>

namespace cts::base {

class Thread {
 public:
  explicit Thread(std::string name) : name_(std::move(name)) {}
  ~Thread() { Stop(); }

  Thread(Thread const&) = delete;
  Thread& operator=(Thread const&) = delete;

  template <typename Callable>
  void Start(Callable&& callable) {
    if (thread_.joinable()) {
      return;
    }
    running_.store(true, std::memory_order_release);
    thread_ =
        std::thread([this, fn = std::forward<Callable>(callable)]() mutable {
          // Linux thread names are capped at 16 bytes incl NUL; truncate
          pthread_setname_np(pthread_self(), name_.substr(0, 15).c_str());
          fn();
          running_.store(false, std::memory_order_release);
        });
  }

  void Stop() {
    if (thread_.joinable()) {
      thread_.join();
    }
  }

  bool IsRunning() const noexcept {
    return running_.load(std::memory_order_acquire);
  }
  std::string const& name() const noexcept { return name_; }

 private:
  std::string name_;
  std::thread thread_;
  std::atomic<bool> running_{false};
};

}  // namespace cts::base

#endif  // CTS_BASE_THREAD_H_