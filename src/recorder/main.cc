/**
 * @file main.cc
 * @author lipingan (lipingan.dev@outlook.com)
 * @brief cts_recorder: standalone raw tick recorder process.
 */

// Standalone recorder: same CTP account, second concurrent login. The md feed
// is a broadcast, so this process sees bytes identical to the trading
// process; keeping it separate means a recording fault can never take the
// trading process down (and vice versa).

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "base/shutdown.h"
#include "base/threading/thread.h"
#include "base/version.h"
#include "md/md_delegate.h"
#include "md/md_gateway.h"
#include "recorder/raw_tick_recorder.h"

#include <CLI/CLI.hpp>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

namespace {

struct RecorderConfig {
  std::string front;
  std::string broker_id;
  std::string investor_id;
  std::string password;
  std::string flow_dir;
  std::string dir;  // output dir of per-day raw tick files
  std::string source{"simnow-724"};  // feed tag written into the file header
  std::string log_level{"info"};
  std::vector<std::string> instruments;
};

// Lifecycle logging + login flag; ticks are dropped -- the raw tap already
// recorded them, the normalized Tick is unused in this process
class LoggingDelegate final : public cts::md::MdDelegate {
 public:
  bool logged_in() const {
    return logged_in_.load(std::memory_order_acquire);
  }

  void OnMdConnected() override { SPDLOG_INFO("md: front connected"); }
  void OnMdDisconnected(int reason) override {
    logged_in_.store(false, std::memory_order_release);
    SPDLOG_WARN("md: front disconnected, reason={}", reason);
  }
  void OnMdLogin(bool success, std::string_view msg) override {
    logged_in_.store(success, std::memory_order_release);
    if (success) {
      SPDLOG_INFO("md: login ok");
    } else {
      SPDLOG_ERROR("md: login failed: {}", msg);
    }
  }
  void OnMdError(int error_id, std::string_view msg) override {
    SPDLOG_ERROR("md: error {}: {}", error_id, msg);
  }

 private:
  std::atomic<bool> logged_in_{false};
};

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

bool WaitMdLogin(LoggingDelegate const& delegate, std::chrono::seconds t) {
  auto const deadline = std::chrono::steady_clock::now() + t;
  while (!delegate.logged_in() && !cts::base::ShutdownRequested() &&
         std::chrono::steady_clock::now() < deadline) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  return delegate.logged_in();
}

}  // namespace

int main(int argc, char** argv) {
  cts::base::InstallShutdownHandlers();

  RecorderConfig cfg;
  char const* home = std::getenv("HOME");
  std::string const home_dir = home != nullptr ? home : ".";
  cfg.flow_dir = home_dir + "/.cts/flow/recorder";
  cfg.dir = home_dir + "/.cts/records";

  CLI::App app{"cts_recorder: standalone raw CTP tick recorder"};
  app.set_config("--config", home_dir + "/.cts/recorder.ini",
                 "INI config path");
  app.set_version_flag("--version", std::string(cts::base::Version()));
  app.add_option("--md-front", cfg.front, "market data front address")
      ->required();
  app.add_option("--broker-id", cfg.broker_id, "broker id (SimNow: 9999)")
      ->required();
  app.add_option("--username", cfg.investor_id, "CTP investor id")->required();
  // password belongs in the INI: CLI args are visible in ps
  app.add_option("--password", cfg.password, "CTP password")->required();
  app.add_option("--flow-dir", cfg.flow_dir, "dir of CTP flow files");
  app.add_option("--dir", cfg.dir, "output dir of raw tick files");
  app.add_option("--source", cfg.source,
                 "feed tag in the file header (<= 15 chars)");
  app.add_option("--log-level", cfg.log_level, "log level")
      ->check(CLI::IsMember({"trace", "debug", "info", "warn", "error"}))
      ->capture_default_str();
  app.add_option("instruments", cfg.instruments,
                 "instruments to subscribe (multi)")
      ->required();

  try {
    app.parse(argc, argv);
  } catch (CLI::ParseError const& e) {
    return app.exit(e);
  }

  spdlog::set_default_logger(spdlog::stdout_color_mt("cts_recorder"));
  spdlog::set_level(ParseLogLevel(cfg.log_level));

  SPDLOG_INFO("cts_recorder v{} starting", cts::base::Version());
  SPDLOG_INFO("dir: {} source: {} instruments: {}", cfg.dir, cfg.source,
              cfg.instruments.size());

  cts::recorder::RawTickRecorder recorder{cfg.dir, cfg.source};
  cts::base::Thread writer_thread{"raw-recorder"};
  writer_thread.Start([&recorder] { recorder.Run(); });

  LoggingDelegate delegate;
  cts::md::MdGateway gateway;
  gateway.SetRawTickSink(&recorder);
  gateway.Connect(
      {
          .front = cfg.front,
          .broker_id = cfg.broker_id,
          .investor_id = cfg.investor_id,
          .password = cfg.password,
          .flow_dir = cfg.flow_dir,
      },
      delegate);

  int rc = 0;
  if (!WaitMdLogin(delegate, std::chrono::seconds{30})) {
    bool const aborted = cts::base::ShutdownRequested();
    SPDLOG_ERROR("md: {}",
                 aborted ? "shutdown during login wait" : "login timeout");
    rc = aborted ? 0 : 1;
  } else if (gateway.Subscribe(cfg.instruments) != 0) {
    SPDLOG_ERROR("md: subscribe request rejected");
    rc = 1;
  } else {
    SPDLOG_INFO("recording; Ctrl-C to stop (twice to force-kill)");
    cts::base::WaitForShutdown();
  }

  // No new ticks after Disconnect; then stop and drain the writer
  gateway.Disconnect();
  recorder.RequestStop();
  writer_thread.Stop();

  SPDLOG_INFO("recorded={} dropped={} write_errors={}", recorder.recorded(),
              recorder.dropped(), recorder.write_errors());
  spdlog::shutdown();
  return rc;
}
