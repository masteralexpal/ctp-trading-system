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

#include "base/threading/task_loop.h"

namespace cts::md {
class MdDispatcher;
class MdGateway;
}  // namespace cts::md

namespace cts::td {
class TdGateway;
}

namespace cts::oms {
class CheckedOrderSink;
class OrderManager;
}

namespace cts::strategy {
class SmaCrossStrategy;
}

namespace cts::app {

// Dev console: stdin is watched by the main TaskLoop directly (fd in the
// epoll set; terminal canonical mode makes it readable per line), so commands
// execute on the main thread where all the assembled state lives. Transport
// (stdin) and command handling are deliberately separated; a TCP admin
// socket is a later transport for the same command set.
//
// Degrades by stdin type (see Start): TTY = interactive, FIFO = scripted
// (EOF shuts the process down), anything else = daemon, console off.
class Console {
 public:
  Console(base::TaskLoop& loop,
          md::MdGateway& md_gateway,
          md::MdDispatcher const& md_dispatcher,
          td::TdGateway& td_gateway,
          oms::OrderManager& order_manager,
          oms::CheckedOrderSink& risk_sink,
          strategy::SmaCrossStrategy* strategy);  // nullptr = no strategy
  ~Console() = default;

  Console(Console const&) = delete;
  Console& operator=(Console const&) = delete;

  // Watches stdin on the loop; no-op when stdin is neither TTY nor pipe
  void Start();

 private:
  void OnReadable();                    // loop thread
  void Execute(std::string_view line);  // loop thread

  base::TaskLoop& loop_;
  md::MdGateway& md_gateway_;
  md::MdDispatcher const& md_dispatcher_;
  td::TdGateway& td_gateway_;
  oms::OrderManager& order_manager_;
  oms::CheckedOrderSink& risk_sink_;  // all order flow goes through risk
  strategy::SmaCrossStrategy* strategy_;
  std::string pending_;  // partial line between reads
  bool eof_shutdown_{false};  // FIFO mode: EOF requests graceful shutdown
};

}  // namespace cts::app

#endif  // CTS_APP_CONSOLE_H_
