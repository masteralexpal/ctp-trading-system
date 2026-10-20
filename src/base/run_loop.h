/**
 * @file run_loop.h
 * @author lipingan (lipingan.dev@outlook.com)
 * @brief RunLoop: the conceptual threading model's runtime identity.
 */
#pragma once

#ifndef CTS_BASE_RUN_LOOP_H_
#define CTS_BASE_RUN_LOOP_H_

#include <string_view>

namespace cts::base {

// The conceptual threading model: every worker thread runs exactly one
// RunLoop. The base carries RUNTIME IDENTITY and LIFECYCLE only -- posting
// mechanics (closures, timers, POD events) live in the concrete subclasses.
// A RunLoop* can identify, wake and stop a loop, but never post to it:
// Current() is for identity/affiliation checks and self-reference, NOT a
// global postbox. Cross-thread posting requires the concrete handle handed
// out by the assembler, keeping every coupling explicit.
//
// Backends (identical lifecycle, different payloads and overflow policies):
//   base::TaskLoop      -- closures + timers, multi-producer (control plane)
//   base::SpscLoop<E>   -- POD events, single producer, drop-newest backpressure
//
// Model laws:
//   1. A RunLoop spawns nothing; the assembler places Run() on a thread.
//   2. One RunLoop per thread (asserted by the Run scope).
//   3. RequestStop is idempotent, any-thread; Run returns only after the
//      queue is drained.
class RunLoop {
 public:
  virtual ~RunLoop() = default;

  RunLoop(RunLoop const&) = delete;
  RunLoop& operator=(RunLoop const&) = delete;

  virtual std::string_view name() const = 0;

  // NVI: installs the TLS scope (one loop per thread), then runs the
  // subclass pump. Blocks until RequestStop and drained.
  void Run();

  // Idempotent, any thread (including the loop's own)
  virtual void RequestStop() = 0;
  // Wakes the parked loop without requesting stop
  virtual void Wakeup() = 0;

  // The loop running on the calling thread; nullptr outside any RunLoop
  // (e.g. on CTP callback threads)
  static RunLoop* Current();
  bool BelongsToCurrentThread() const { return Current() == this; }

 protected:
  RunLoop() = default;
  virtual void RunImpl() = 0;

 private:
  struct Scope;  // RAII TLS install/uninstall, defined in the .cc
};

}  // namespace cts::base

#endif  // CTS_BASE_RUN_LOOP_H_
