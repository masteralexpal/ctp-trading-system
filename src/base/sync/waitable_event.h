/**
 * @file waitable_event.h
 * @author lipingan (lipingan.dev@outlook.com)
 * @brief WaitableEvent (Chromium-aligned interface), eventfd-backed.
 */
#pragma once

#ifndef CTS_BASE_WAITABLE_EVENT_H_
#define CTS_BASE_WAITABLE_EVENT_H_

#include <chrono>

namespace cts::base {

// A signalable event, Chromium base::WaitableEvent semantics, Linux-only
// (eventfd). Signal() is async-signal-safe (a bare write(2)).
//
// fd() is the epoll/poll ticket: the event can wait alongside sockets in one
// poll set (the future admin-socket loop).
class WaitableEvent {
 public:
  enum class ResetPolicy {
    kManual,     // stays signaled until Reset(); all waiters proceed
    kAutomatic,  // each Signal() releases exactly one waiter
  };
  enum class InitialState { kNotSignaled, kSignaled };

  explicit WaitableEvent(ResetPolicy policy,
                         InitialState state = InitialState::kNotSignaled);
  ~WaitableEvent();

  WaitableEvent(WaitableEvent const&) = delete;
  WaitableEvent& operator=(WaitableEvent const&) = delete;
  WaitableEvent(WaitableEvent&&) noexcept;
  WaitableEvent& operator=(WaitableEvent&&) noexcept;

  void Signal();
  void Reset();
  bool IsSignaled() const;

  void Wait();
  // Returns false on timeout
  bool TimedWait(std::chrono::milliseconds timeout);

  int fd() const { return fd_; }
  ResetPolicy policy() const { return policy_; }

 private:
  int fd_;
  ResetPolicy policy_;
};

}  // namespace cts::base

#endif  // CTS_BASE_WAITABLE_EVENT_H_
