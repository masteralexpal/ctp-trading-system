/**
 * @file main.cc
 * @author lipingan (lipingan.dev@outlook.com)
 * @brief main function entry point.
 */

#include <atomic>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <memory>
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
#include "oms/checked_order_sink.h"
#include "oms/order_manager.h"
#include "strategy/sma_cross_strategy.h"
#include "td/console_td_client.h"
#include "td/td_dispatcher.h"
#include "td/td_gateway.h"

#include <spdlog/async.h>
#include <spdlog/sinks/daily_file_sink.h>
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
  // Logs go to a daily-rolled file only: the terminal belongs to the REPL.
  // Rotation is by CALENDAR day (local clock, 00:00) -- the TradingDay-bucket
  // rule applies to the tick journal, not to diagnostics
  std::filesystem::create_directories(cfg.log_dir);
  spdlog::init_thread_pool(8192, 1);
  auto logger = spdlog::create_async_nb<spdlog::sinks::daily_file_sink_mt>(
      "cts", cfg.log_dir + "/cts.log", 0, 0);
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

// Same shape for the td startup chain (auth + login + settlement confirm)
bool WaitTdReady(cts::td::TdDispatcher const& dispatcher,
                 std::chrono::seconds timeout) {
  auto const deadline = std::chrono::steady_clock::now() + timeout;
  while (!dispatcher.ready() && !cts::base::ShutdownRequested() &&
         std::chrono::steady_clock::now() < deadline) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  return dispatcher.ready();
}

// Re-fires reconciliation queries after every (re)connect, spaced to respect
// the CTP query throttle (~1 query/sec). Startup is special: OnTdReady fires
// while the main loop is still blocked on the instrument master, so delayed
// tasks posted there all collide the instant the loop starts and the throttle
// eats one. Begin() is called by the main thread once the instrument query is
// done (the throttle window is quiet then); reconnects re-fire via OnTdReady
class Reconciler final : public cts::td::TdClient {
 public:
  Reconciler(cts::base::TaskLoop& loop, cts::td::TdGateway& gateway)
      : loop_(loop), gateway_(gateway) {}

  // Main thread, after the instrument master load
  void Begin() {
    begun_.store(true, std::memory_order_release);
    Fire();
  }

  void OnTdConnected() override {}
  void OnTdDisconnected(int /*reason*/) override {}
  void OnTdReady(bool success, std::string_view /*msg*/) override {
    if (success && begun_.load(std::memory_order_acquire)) {
      Fire();
    }
  }
  void OnTdOrder(cts::td::OrderReport const& /*order*/) override {}
  void OnTdTrade(cts::td::TradeReport const& /*trade*/) override {}
  void OnTdPositions(
      std::shared_ptr<std::vector<cts::td::PositionRow> const>) override {}
  void OnTdAccount(cts::td::AccountReport const&) override {}
  void OnTdError(int, std::string_view) override {}

 private:
  // Fires both queries, spaced for the throttle; any thread (via the loop)
  void Fire() {
    loop_.PostDelayedTask([this] { gateway_.QueryPositions(); },
                          std::chrono::milliseconds{0});
    loop_.PostDelayedTask([this] { gateway_.QueryAccount(); },
                          std::chrono::milliseconds{1300});
  }

  cts::base::TaskLoop& loop_;
  cts::td::TdGateway& gateway_;
  std::atomic<bool> begun_{false};
};

// Main pipeline: md + td gateways -> instrument master -> strategy ->
// park on the main loop -> graceful stop.
// Shutdown: first Ctrl-C sets the shutdown flag (futex wake, no polling);
// a second Ctrl-C _Exit()s immediately
int RunTrading(cts::app::AppConfig const& cfg) {
  SPDLOG_INFO("ctp-trading-system v{} starting", cts::base::Version());

  // Objects first, connects later: the Reconciler wires the loop and the td
  // gateway before either starts moving
  cts::base::TaskLoop loop;
  cts::md::MdGateway md_gateway;
  cts::td::TdGateway td_gateway;
  Reconciler reconciler{loop, td_gateway};

  // --- md side ---
  cts::md::ConsoleMdClient client;
  cts::md::MdDispatcher dispatcher;
  dispatcher.AddClient(client);

  cts::base::Thread md_dispatch_thread{"md-dispatch"};
  md_dispatch_thread.Start([&dispatcher] { dispatcher.Run(); });

  // --- td side ---
  cts::td::ConsoleTdClient td_client;
  cts::oms::OrderManager order_manager;  // order journal + position ledger
  // Restart recovery: restore ledger/journal from the last snapshot before
  // any event arrives; the broker reconcile stays authoritative on volumes
  std::filesystem::create_directories(cfg.state_dir);
  order_manager.SetSnapshotPath(cfg.state_dir + "/oms.txt");
  order_manager.LoadSnapshot();
  cts::td::TdDispatcher td_dispatcher;
  td_dispatcher.AddClient(td_client);
  td_dispatcher.AddClient(order_manager);
  td_dispatcher.AddClient(reconciler);

  cts::base::Thread td_dispatch_thread{"td-dispatch"};
  td_dispatch_thread.Start([&td_dispatcher] { td_dispatcher.Run(); });

  // Every worker loop must be RequestStop'ed before scope exit (~Thread
  // joins; a parked Run() would hang forever)
  auto const stop_all = [&] {
    td_dispatcher.RequestStop();
    dispatcher.RequestStop();
  };

  // flow dirs = configured root + role subdir (trailing slash fixed by
  // gateways)
  md_gateway.Connect(
      {
          .front = cfg.md_front,
          .broker_id = cfg.broker_id,
          .investor_id = cfg.username,
          .password = cfg.password,
          .flow_dir = cfg.flow_dir + "/md",
      },
      dispatcher);

  td_gateway.Connect(
      {
          .front = cfg.trader_front,
          .broker_id = cfg.broker_id,
          .investor_id = cfg.username,
          .password = cfg.password,
          .app_id = cfg.app_id,
          .auth_code = cfg.auth_code,
          .flow_dir = cfg.flow_dir + "/td",
      },
      td_dispatcher);

  if (!WaitMdLogin(dispatcher, std::chrono::seconds{30})) {
    bool const aborted = cts::base::ShutdownRequested();
    SPDLOG_ERROR("md: {}", aborted ? "shutdown during login wait"
                                   : "login timeout, exiting");
    td_gateway.Disconnect();
    md_gateway.Disconnect();
    stop_all();
    return aborted ? 0 : 1;
  }

  if (!WaitTdReady(td_dispatcher, std::chrono::seconds{30})) {
    bool const aborted = cts::base::ShutdownRequested();
    SPDLOG_ERROR("td: {}", aborted ? "shutdown during startup chain"
                                   : "startup chain timeout, exiting");
    td_gateway.Disconnect();
    md_gateway.Disconnect();
    stop_all();
    return aborted ? 0 : 1;
  }

  // Whole-market instrument snapshot (exchange ids for order routing)
  td_gateway.QueryInstruments();
  {
    auto const deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds{60};
    while (td_gateway.instruments() == nullptr &&
           !cts::base::ShutdownRequested() &&
           std::chrono::steady_clock::now() < deadline) {
      std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
  }
  auto const master = td_gateway.instruments();
  if (master == nullptr) {
    SPDLOG_WARN("td: instrument query timed out; order routing unavailable");
  } else {
    SPDLOG_INFO("td: instrument master loaded, {} instruments", master->size());
    order_manager.SetInstrumentMaster(master);
  }

  // The instrument query stream is over; the throttle window is quiet now.
  // Kick position/account reconciliation from here (not from OnTdReady --
  // delayed tasks posted there would all collide the moment the loop starts)
  reconciler.Begin();

  // Pull margin/commission rates for the subscribed instruments, queued
  // behind the reconcile pair (0/1300 ms) at the same throttle spacing.
  // The risk gate's margin check reads these
  {
    int slot = 2;
    for (auto const& id : cfg.instruments) {
      if (master != nullptr && master->Find(id) == nullptr) {
        continue;  // not a listed instrument; do not waste a query on it
      }
      loop.PostDelayedTask(
          [&td_gateway, id] { td_gateway.QueryMarginRate(id); },
          std::chrono::milliseconds{1300 * slot++});
      loop.PostDelayedTask(
          [&td_gateway, id] { td_gateway.QueryCommissionRate(id); },
          std::chrono::milliseconds{1300 * slot++});
    }
  }

  // Pre-trade risk gate: all order flow (console + strategy) goes through it.
  // Position/account reconciliation is the Reconciler's job (throttle-spaced)
  cts::oms::CheckedOrderSink risk_sink{td_gateway, order_manager, {}};
  if (master != nullptr) {
    risk_sink.SetInstrumentMaster(master);
  }
  risk_sink.SetRateTable(&td_gateway.rates());

  if (!cfg.instruments.empty()) {
    if (md_gateway.Subscribe(cfg.instruments) == 0) {
      SPDLOG_INFO("md: subscribe request accepted");
    }
  }

  // Optional demo strategy (needs the master for exchange routing)
  std::unique_ptr<cts::strategy::SmaCrossStrategy> strategy;
  if (!cfg.strategy_instrument.empty()) {
    auto const* inst =
        master != nullptr ? master->Find(cfg.strategy_instrument) : nullptr;
    if (inst == nullptr) {
      SPDLOG_ERROR("strategy: {} not in instrument master, disabled",
                   cfg.strategy_instrument);
    } else {
      strategy = std::make_unique<cts::strategy::SmaCrossStrategy>(
          cts::strategy::SmaCrossConfig{
              .instrument_id = inst->instrument_id,
              .exchange_id = inst->exchange_id,
              .fast = cfg.strategy_fast,
              .slow = cfg.strategy_slow,
              .volume = cfg.strategy_volume,
          },
          risk_sink);
      dispatcher.AddClient(*strategy);
      td_dispatcher.AddClient(*strategy);
      strategy->Start();
      SPDLOG_INFO("strategy {} started (fast={} slow={} volume={})",
                  strategy->name(), cfg.strategy_fast, cfg.strategy_slow,
                  cfg.strategy_volume);
    }
  }

  SPDLOG_INFO("running; Ctrl-C to stop (twice to force-kill)");

  // The main thread is a task loop: periodic housekeeping lives here.
  // Shutdown (first Ctrl-C) wakes and stops it via the shutdown wake word
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
  cts::app::Console console{loop,       md_gateway, dispatcher,
                            td_gateway, order_manager, risk_sink,
                            strategy.get()};
  console.Start();

  loop.Run();
  SPDLOG_INFO("shutdown requested, draining");

  // Stop order flow first, then gateways (no new events), then dispatchers
  if (strategy != nullptr) {
    strategy->Stop();
  }
  order_manager.SaveSnapshot();  // explicit final save before the drain
  td_gateway.Disconnect();
  md_gateway.Disconnect();
  stop_all();
  td_dispatch_thread.Stop();
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

  // Terminal lifeline: all logs now go to the file; say where, and warn that
  // the console prompt only appears after the instrument master load
  std::cout << "cts_app " << cts::base::Version()
            << "; logs: " << cfg.log_dir << "/cts_YYYY-MM-DD.log\n"
            << "loading instruments, console activates shortly..."
            << std::endl;

  if (cfg.dry_run) {
    SPDLOG_INFO("dry-run mode, exiting.");
    spdlog::shutdown();
    return 0;
  }

  int const rc = RunTrading(cfg);
  spdlog::shutdown();  // drain the async log queue before exit
  return rc;
}
