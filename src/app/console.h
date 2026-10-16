/**
 * @file console.h
 * @author lipingan (lipingan.dev@outlook.com)
 * @brief Line-based stdin console; commands run on the main loop.
 */
#pragma once

#ifndef CTS_APP_CONSOLE_H_
#define CTS_APP_CONSOLE_H_

#include <string>
#include <string_view>

#include "base/task/task_loop.h"

namespace cts::md {
class MdDispatcher;
class MdGateway;
}  // namespace cts::md

namespace cts::app {

// Dev console: stdin is watched by the main TaskLoop directly (fd in the
// epoll set; terminal canonical mode makes it readable per line), so commands
// execute on the main thread where all the assembled state lives. Transport
// (stdin) and command handling are deliberately separated; a TCP admin
// socket is a later transport for the same command set.
//
// Degrades silently: not started when stdin is not a TTY (daemon mode).
class Console {
 public:
  Console(base::TaskLoop& loop,
          md::MdGateway& md_gateway,
          md::MdDispatcher const& md_dispatcher);
  ~Console() = default;

  Console(Console const&) = delete;
  Console& operator=(Console const&) = delete;

  // Watches stdin on the loop; no-op when stdin is not a TTY
  void Start();

 private:
  void OnReadable();                    // loop thread
  void Execute(std::string_view line);  // loop thread

  base::TaskLoop& loop_;
  md::MdGateway& md_gateway_;
  md::MdDispatcher const& md_dispatcher_;
  std::string pending_;  // partial line between reads
};

}  // namespace cts::app

#endif  // CTS_APP_CONSOLE_H_
