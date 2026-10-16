/**
 * @file console.cc
 * @author lipingan (lipingan.dev@outlook.com)
 * @brief Line-based stdin console; commands run on the main loop.
 */
#include "app/console.h"

#include <fcntl.h>
#include <unistd.h>

#include <iostream>
#include <string>
#include <vector>

#include <spdlog/spdlog.h>

#include "app/app_config.h"
#include "base/shutdown.h"
#include "md/md_dispatcher.h"
#include "md/md_gateway.h"

namespace cts::app {

Console::Console(base::TaskLoop& loop,
                 md::MdGateway& md_gateway,
                 md::MdDispatcher const& md_dispatcher)
    : loop_(loop), md_gateway_(md_gateway), md_dispatcher_(md_dispatcher) {}

void Console::Start() {
  if (!isatty(STDIN_FILENO)) {
    SPDLOG_INFO("console: stdin is not a TTY, console disabled");
    return;
  }
  // Nonblocking: OnReadable drains until EAGAIN
  fcntl(STDIN_FILENO, F_SETFL, fcntl(STDIN_FILENO, F_GETFL) | O_NONBLOCK);
  std::cout << "console ready; 'help' for commands\n> " << std::flush;
  loop_.WatchFd(STDIN_FILENO, [this] { OnReadable(); });
}

void Console::OnReadable() {
  char buf[256];
  for (;;) {
    ssize_t const n = read(STDIN_FILENO, buf, sizeof(buf));
    if (n > 0) {
      pending_.append(buf, static_cast<std::size_t>(n));
      for (;;) {
        auto const nl = pending_.find('\n');
        if (nl == std::string::npos) {
          break;
        }
        Execute(std::string_view(pending_).substr(0, nl));
        pending_.erase(0, nl + 1);
      }
    } else if (n == 0) {
      loop_.UnwatchFd(STDIN_FILENO);  // EOF: stdin closed
      return;
    } else {
      return;  // EAGAIN: drained
    }
  }
}

void Console::Execute(std::string_view line) {
  auto const sp = line.find(' ');
  std::string_view const cmd = line.substr(0, sp);
  std::string_view const args =
      sp == std::string_view::npos ? "" : line.substr(sp + 1);

  if (cmd == "help") {
    std::cout << "status                 connection, login, drop counters\n"
                 "subscribe <id...>      subscribe instruments\n"
                 "unsubscribe <id...>    unsubscribe instruments\n"
                 "log-level <level>      trace|debug|info|warn|error\n"
                 "quit                   graceful shutdown\n";
  } else if (cmd == "status") {
    auto const& cfg = GetAppConfig();
    std::cout << "front:          " << cfg.md_front << "\n"
              << "logged_in:      " << md_dispatcher_.logged_in() << "\n"
              << "dropped_events: " << md_dispatcher_.dropped_events() << "\n"
              << "instruments:";
    for (auto const& id : cfg.instruments) {
      std::cout << ' ' << id;
    }
    std::cout << "\n";
  } else if (cmd == "subscribe" || cmd == "unsubscribe") {
    std::vector<std::string> ids;
    std::string_view rest = args;
    while (!rest.empty()) {
      auto const p = rest.find(' ');
      ids.emplace_back(rest.substr(0, p));
      rest = p == std::string_view::npos ? "" : rest.substr(p + 1);
    }
    int const rc = cmd == "subscribe" ? md_gateway_.Subscribe(ids)
                                      : md_gateway_.UnSubscribe(ids);
    std::cout << (rc == 0 ? "accepted\n"
                          : "rejected (empty or not connected)\n");
  } else if (cmd == "log-level") {
    spdlog::level::level_enum const lv =
        args == "trace"   ? spdlog::level::trace
        : args == "debug" ? spdlog::level::debug
        : args == "warn"  ? spdlog::level::warn
        : args == "error" ? spdlog::level::err
                          : spdlog::level::info;
    spdlog::set_level(lv);
    std::cout << "log-level set\n";
  } else if (cmd == "quit") {
    std::cout << "shutting down\n";
    base::RequestShutdown();
    return;  // no prompt: we are going down
  } else {
    std::cout << "unknown command: " << cmd << " ('help' for list)\n";
  }
  std::cout << "> " << std::flush;
}

}  // namespace cts::app
