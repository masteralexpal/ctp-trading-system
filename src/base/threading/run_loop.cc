/**
 * @file run_loop.cc
 * @author lipingan (lipingan.dev@outlook.com)
 * @brief RunLoop: the conceptual threading model's runtime identity.
 */
#include "base/threading/run_loop.h"

#include <cassert>

namespace cts::base {
namespace {

thread_local RunLoop* t_current = nullptr;

}  // namespace

struct RunLoop::Scope {
  explicit Scope(RunLoop* self) : prev_(t_current) {
    // Model law 2: one RunLoop per thread
    assert(prev_ == nullptr);
    t_current = self;
  }
  ~Scope() { t_current = prev_; }
  RunLoop* prev_;
};

void RunLoop::Run() {
  Scope const scope(this);
  RunImpl();
}

RunLoop* RunLoop::Current() {
  return t_current;
}

}  // namespace cts::base
