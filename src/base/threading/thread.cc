/**
 * @file thread.cc
 * @author lipingan (lipingan.dev@outlook.com)
 * @brief Named dedicated thread handle (name visible in gdb/top).
 */
#include "base/threading/thread.h"

namespace cts::base {

Thread::~Thread() {
  Stop();
}

void Thread::Stop() {
  if (thread_.joinable()) {
    thread_.join();
  }
}

bool Thread::IsRunning() const noexcept {
  return running_.load(std::memory_order_acquire);
}

std::string const& Thread::name() const noexcept {
  return name_;
}

}  // namespace cts::base
