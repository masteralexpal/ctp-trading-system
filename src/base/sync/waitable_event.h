/**
 * @file waitable_event.h
 * @author lipingan (lipingan.dev@outlook.com)
 * @brief WaitableEvent (Chromium-aligned interface), eventfd-backed.
 */
#pragma once

#ifndef CTS_BASE_SYNC_WAITABLE_EVENT_H_
#define CTS_BASE_SYNC_WAITABLE_EVENT_H_

#include <chrono>

namespace cts::base {

// A signalable event (Chromium WaitableEvent semantics, eventfd-backed).
// Signal() is async-signal-safe; fd() is the epoll/poll ticket so the event
// can wait alongside sockets in one poll set
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

#endif  // CTS_BASE_SYNC_WAITABLE_EVENT_H_
