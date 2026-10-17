/**
 * @file shutdown.h
 * @author lipingan (lipingan.dev@outlook.com)
 * @brief Process-wide shutdown coordination (signal-safe flag + futex park).
 */
#pragma once

#ifndef CTS_BASE_SHUTDOWN_H_
#define CTS_BASE_SHUTDOWN_H_

namespace cts::base {

class WaitableEvent;

// Installs SIGINT/SIGTERM handlers. First signal: sets the flag and wakes
// WaitForShutdown(). Second signal: _Exit(128 + sig) immediately -- the
// graceful path never holds the user hostage.
void InstallShutdownHandlers();

// Programmatic shutdown request (e.g. a "quit" console command); same effect
// as the first SIGINT
void RequestShutdown();

// True once shutdown has been requested (signal or RequestShutdown)
bool ShutdownRequested();

// Parks until ShutdownRequested (futex wait; zero CPU, wakes instantly)
void WaitForShutdown();

// Registers an event the signal handler will Signal() in addition to the
// flag (each TaskLoop parks on its own via epoll). Fixed small capacity
// (4 slots, fixed array = signal-safe); Run/Stop of a loop own the
// register/unregister pairing
void RegisterShutdownWakeEvent(WaitableEvent* event);
void UnregisterShutdownWakeEvent(WaitableEvent* event);

}  // namespace cts::base

#endif  // CTS_BASE_SHUTDOWN_H_
