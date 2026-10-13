/**
 * @file main.cc
 * @author lipingan (lipingan.dev@outlook.com)
 * @brief main function entry point.
 */

#include <chrono>
#include <csignal>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

#include "app/cli.h"
#include "base/thread.h"
#include "base/version.h"
#include "md/console_md_listener.h"
#include "md/md_dispatcher.h"
#include "md/md_gateway.h"

#include <spdlog/async.h>
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

// Wait for the login result (arrives async via dispatcher queue); returns
// success
bool WaitMdLogin(cts::md::ConsoleMdListener const& listener,
                 std::chrono::seconds timeout) {
  auto const deadline = std::chrono::steady_clock::now() + timeout;
  while (!listener.logged_in.load() &&
         std::chrono::steady_clock::now() < deadline) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  return listener.logged_in.load();
}

// Shutdown signals: the handler only sets a flag (sig_atomic_t is
// async-signal-safe); the main loop polls it. No masking, no sigwait -- also
// effective during startup.
sig_atomic_t volatile g_shutdown_signal = 0;

void HandleShutdownSignal(int sig) {
  g_shutdown_signal = sig;
}

void InstallSignalHandlers() {
  std::signal(SIGINT, HandleShutdownSignal);
  std::signal(SIGTERM, HandleShutdownSignal);
}

// Main pipeline: connect md gateway -> subscribe -> wait for shutdown ->
// graceful stop
int RunTrading(cts::app::AppConfig const& cfg) {
  SPDLOG_INFO("ctp-trading-system v{} starting", cts::base::Version());

  cts::md::ConsoleMdListener listener;
  cts::md::MdDispatcher dispatcher;
  dispatcher.AddListener(listener);

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

  if (!WaitMdLogin(listener, std::chrono::seconds{30})) {
    SPDLOG_ERROR("md: login timeout, exiting");
    // Must run the shutdown sequence: otherwise ~Thread() joins the spinning
    // Run() forever
    md_gateway.Disconnect();
    dispatcher.RequestStop();
    return 1;  // md_dispatch_thread is joined at destruction
  }

  if (!cfg.instruments.empty()) {
    if (md_gateway.Subscribe(cfg.instruments) == 0) {
      SPDLOG_INFO("md: subscribe request accepted");
    }
  }

  SPDLOG_INFO("running; Ctrl-C or SIGTERM to exit...");
  while (g_shutdown_signal == 0) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  SPDLOG_INFO("signal {} received, shutting down", g_shutdown_signal);

  // disconnect gateway first (no new events), then request stop, then join
  // (drains queue)
  md_gateway.Disconnect();
  dispatcher.RequestStop();
  md_dispatch_thread.Stop();
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  InstallSignalHandlers();

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
