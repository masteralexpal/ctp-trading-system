/**
 * @file main.cc
 * @author lipingan (lipingan.dev@outlook.com)
 * @brief main function entry point.
 */

#include <chrono>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

#include "app/cli.h"
#include "app/console.h"
#include "base/shutdown.h"
#include "base/task/task_loop.h"
#include "base/thread.h"
#include "base/version.h"
#include "md/console_md_client.h"
#include "md/md_dispatcher.h"
#include "md/md_gateway.h"

#include <spdlog/async.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

namespace {

spdlog::level::level_enum ParseLogLevel(std::string_view name) {
  if (name == "trace") {
    return spdlog::level::trace;
  }
  if (name == "debug") {
    return spdlog::level::debug;
  }
  if (name == "warn") {
    return spdlog::level::warn;
  }
  if (name == "error") {
    return spdlog::level::err;
  }
  return spdlog::level::info;
}

void InitLogging(cts::app::AppConfig const& cfg) {
  spdlog::init_thread_pool(8192, 1);
  auto logger =
      spdlog::create_async_nb<spdlog::sinks::stdout_color_sink_mt>("cts");
  spdlog::set_default_logger(std::move(logger));
  spdlog::set_level(ParseLogLevel(cfg.log_level));
  spdlog::flush_on(spdlog::level::err);
}

void DumpConfig(cts::app::AppConfig const& cfg) {
  SPDLOG_INFO("log-level:    {}", cfg.log_level);
  SPDLOG_INFO("md-front:     {}",
              cfg.md_front.empty() ? "(unset)" : cfg.md_front);
  SPDLOG_INFO("trader-front: {}",
              cfg.trader_front.empty() ? "(unset)" : cfg.trader_front);
  SPDLOG_INFO("dry-run:      {}", cfg.dry_run);
  SPDLOG_INFO("flow-dir:     {}", cfg.flow_dir);
  for (auto const& instrument : cfg.instruments) {
    SPDLOG_INFO("instrument:   {}", instrument);
  }
}

// Wait for the login result (arrives async via the dispatcher); returns
// success. A shutdown request aborts the wait immediately
bool WaitMdLogin(cts::md::MdDispatcher const& dispatcher,
                 std::chrono::seconds timeout) {
  auto const deadline = std::chrono::steady_clock::now() + timeout;
  while (!dispatcher.logged_in() && !cts::base::ShutdownRequested() &&
         std::chrono::steady_clock::now() < deadline) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  return dispatcher.logged_in();
}

// Main pipeline: connect md gateway -> subscribe -> park -> graceful stop.
// Shutdown: first Ctrl-C sets the shutdown flag (futex wake, no polling);
// a second Ctrl-C _Exit()s immediately
int RunTrading(cts::app::AppConfig const& cfg) {
  SPDLOG_INFO("ctp-trading-system v{} starting", cts::base::Version());

  cts::md::ConsoleMdClient client;
  cts::md::MdDispatcher dispatcher;
  dispatcher.AddClient(client);

  cts::base::Thread md_dispatch_thread{"md-dispatch"};
  md_dispatch_thread.Start([&dispatcher] { dispatcher.Run(); });

  // md flow dir = configured root + role subdir (trailing slash fixed by
  // gateway)
  std::string const md_flow_dir = cfg.flow_dir + "/md";

  cts::md::MdGateway md_gateway;
  md_gateway.Connect(
      {
          .front = cfg.md_front,
          .broker_id = cfg.broker_id,
          .investor_id = cfg.username,
          .password = cfg.password,
          .flow_dir = md_flow_dir,
      },
      dispatcher);

  if (!WaitMdLogin(dispatcher, std::chrono::seconds{30})) {
    bool const aborted = cts::base::ShutdownRequested();
    if (aborted) {
      SPDLOG_INFO("md: shutdown requested during login wait");
    } else {
      SPDLOG_ERROR("md: login timeout, exiting");
    }
    // Must run the shutdown sequence: otherwise ~Thread() joins the spinning
    // Run() forever
    md_gateway.Disconnect();
    dispatcher.RequestStop();
    return aborted ? 0 : 1;  // md_dispatch_thread is joined at destruction
  }

  if (!cfg.instruments.empty()) {
    if (md_gateway.Subscribe(cfg.instruments) == 0) {
      SPDLOG_INFO("md: subscribe request accepted");
    }
  }

  SPDLOG_INFO("running; Ctrl-C to stop (twice to force-kill)");

  // The main thread is a task loop: periodic housekeeping lives here.
  // Shutdown (first Ctrl-C) wakes and stops it via the shutdown wake word
  cts::base::TaskLoop loop;
  struct HealthTick {
    cts::base::TaskLoop* loop;
    cts::md::MdDispatcher const* dispatcher;
    void operator()() const {
      SPDLOG_INFO("md: dropped_events={}", dispatcher->dropped_events());
      loop->PostDelayedTask(*this, std::chrono::seconds{60});
    }
  };
  loop.PostDelayedTask(HealthTick{&loop, &dispatcher},
                       std::chrono::seconds{60});

  // Dev console: stdin lines become tasks on this loop; no-op without a TTY
  cts::app::Console console{loop, md_gateway, dispatcher};
  console.Start();

  loop.Run();
  SPDLOG_INFO("shutdown requested, draining");

  // disconnect gateway first (no new events), then request stop, then join
  // (drains queue)
  md_gateway.Disconnect();
  dispatcher.RequestStop();
  md_dispatch_thread.Stop();
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  cts::base::InstallShutdownHandlers();

  auto [config, exit_code] = cts::app::ParseArgs(argc, argv);
  if (!config) {
    return exit_code;
  }
  cts::app::InstallAppConfig(std::move(*config));

  auto const& cfg = cts::app::GetAppConfig();
  InitLogging(cfg);
  DumpConfig(cfg);

  if (cfg.dry_run) {
    SPDLOG_INFO("dry-run mode, exiting.");
    spdlog::shutdown();
    return 0;
  }

  int const rc = RunTrading(cfg);
  spdlog::shutdown();  // drain the async log queue before exit
  return rc;
}
