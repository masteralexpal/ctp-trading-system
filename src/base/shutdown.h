/**
 * @file shutdown.h
 * @author lipingan (lipingan.dev@outlook.com)
 * @brief Process-wide shutdown coordination (signal-safe flag + futex park).
 */
#pragma once

#ifndef CTS_BASE_SHUTDOWN_H_
#define CTS_BASE_SHUTDOWN_H_

#include <atomic>
#include <cstdint>

namespace cts::base {

// Installs SIGINT/SIGTERM handlers. First signal: sets the flag and wakes
// WaitForShutdown(). Second signal: _Exit(128 + sig) immediately -- the
// graceful path never holds the user hostage.
void InstallShutdownHandlers();

// Sets the flag and wakes waiters. Callable from signal handlers and threads
bool ShutdownRequested();

// Parks until ShutdownRequested (futex wait; zero CPU, wakes instantly)
void WaitForShutdown();

// Registers a futex word the signal handler will kick in addition to the
// flag (the main TaskLoop parks on it). One active registration; Run/Stop of
// the loop own the register/unregister pairing
void RegisterShutdownWakeWord(std::atomic<std::uint32_t>* word);
void UnregisterShutdownWakeWord(std::atomic<std::uint32_t>* word);

}  // namespace cts::base

#endif  // CTS_BASE_SHUTDOWN_H_
