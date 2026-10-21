/**
 * @file waitable_event.cc
 * @author lipingan (lipingan.dev@outlook.com)
 * @brief WaitableEvent (Chromium-aligned interface), eventfd-backed.
 */
#include "base/sync/waitable_event.h"

#include <poll.h>
#include <sys/eventfd.h>
#include <unistd.h>

#include <cstdint>
#include <utility>

namespace cts::base {
namespace {

// Readable check; timeout_ms -1 = forever. EINTR retries
bool PollReadable(int fd, int timeout_ms) {
  pollfd pfd{fd, POLLIN, 0};
  int rc;
  do {
    rc = poll(&pfd, 1, timeout_ms);
  } while (rc < 0);
  return rc > 0 && (pfd.revents & POLLIN) != 0;
}

// Non-blocking consume of one unit; false = nothing to consume
bool TryConsume(int fd) {
  std::uint64_t value;
  return read(fd, &value, sizeof(value)) == sizeof(value);
}

}  // namespace

WaitableEvent::WaitableEvent(ResetPolicy policy, InitialState state) {
  policy_ = policy;
  fd_ = eventfd(state == InitialState::kSignaled ? 1 : 0,
                EFD_CLOEXEC | EFD_NONBLOCK |
                    (policy == ResetPolicy::kAutomatic ? EFD_SEMAPHORE : 0));
}

WaitableEvent::~WaitableEvent() {
  if (fd_ >= 0) {
    close(fd_);
  }
}

WaitableEvent::WaitableEvent(WaitableEvent&& other) noexcept
    : fd_(std::exchange(other.fd_, -1)), policy_(other.policy_) {}

WaitableEvent& WaitableEvent::operator=(WaitableEvent&& other) noexcept {
  if (this != &other) {
    if (fd_ >= 0) {
      close(fd_);
    }
    fd_ = std::exchange(other.fd_, -1);
    policy_ = other.policy_;
  }
  return *this;
}

void WaitableEvent::Signal() {
  std::uint64_t const one = 1;
  // EAGAIN (counter saturated) means the fd is already readable, which is
  // enough to wake any waiter -- safe to ignore (Chromium's ScheduleWork
  // documents the same idiom)
  static_cast<void>(write(fd_, &one, sizeof(one)));
}

void WaitableEvent::Reset() {
  std::uint64_t tmp;
  while (read(fd_, &tmp, sizeof(tmp)) > 0) {  // nonblocking; EAGAIN ends it
  }
}

bool WaitableEvent::IsSignaled() const {
  return PollReadable(fd_, 0);
}

void WaitableEvent::Wait() {
  if (policy_ == ResetPolicy::kAutomatic) {
    // Poll-then-read (not a plain blocking read): if a competing waiter
    // consumed the unit first, EAGAIN loops us back to the poll
    while (!PollReadable(fd_, -1) || !TryConsume(fd_)) {
    }
    return;
  }
  // Manual: poll is level-triggered on counter > 0 and non-destructive,
  // so every waiter proceeds until Reset()
  while (!PollReadable(fd_, -1)) {
  }
}

bool WaitableEvent::TimedWait(std::chrono::milliseconds timeout) {
  if (!PollReadable(fd_, static_cast<int>(timeout.count()))) {
    return false;
  }
  if (policy_ == ResetPolicy::kAutomatic) {
    return TryConsume(fd_);  // false = a competing waiter won the race
  }
  return true;
}

}  // namespace cts::base
