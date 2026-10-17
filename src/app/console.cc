/**
 * @file console.cc
 * @author lipingan (lipingan.dev@outlook.com)
 * @brief Line-based stdin console; commands run on the main loop.
 */
#include "app/console.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <charconv>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

#include <spdlog/spdlog.h>

#include "app/app_config.h"
#include "base/shutdown.h"
#include "base/string_utils.h"
#include "md/md_dispatcher.h"
#include "md/md_gateway.h"
#include "oms/checked_order_sink.h"
#include "oms/order_manager.h"
#include "strategy/sma_cross_strategy.h"
#include "td/td_gateway.h"

namespace cts::app {
namespace {

// Splits on spaces; empty input yields an empty list
std::vector<std::string_view> SplitWords(std::string_view line) {
  std::vector<std::string_view> words;
  std::string_view rest = line;
  while (!rest.empty()) {
    auto const p = rest.find(' ');
    words.push_back(rest.substr(0, p));
    rest = p == std::string_view::npos ? "" : rest.substr(p + 1);
  }
  return words;
}

td::Offset ParseOffset(std::string_view word, bool& ok) {
  ok = true;
  if (word.empty() || word == "open") {
    return td::Offset::kOpen;
  }
  if (word == "close") {
    return td::Offset::kClose;
  }
  if (word == "close-today") {
    return td::Offset::kCloseToday;
  }
  if (word == "close-yesterday") {
    return td::Offset::kCloseYesterday;
  }
  ok = false;
  return td::Offset::kOpen;
}

char const* StatusName(td::OrderStatus s) {
  switch (s) {
    case td::OrderStatus::kSubmitted:
      return "submitted";
    case td::OrderStatus::kPartTraded:
      return "part-traded";
    case td::OrderStatus::kAllTraded:
      return "all-traded";
    case td::OrderStatus::kCanceled:
      return "canceled";
    case td::OrderStatus::kRejected:
      return "rejected";
    case td::OrderStatus::kUnknown:
      return "unknown";
  }
  return "?";
}

}  // namespace

Console::Console(base::TaskLoop& loop,
                 md::MdGateway& md_gateway,
                 md::MdDispatcher const& md_dispatcher,
                 td::TdGateway& td_gateway,
                 oms::OrderManager& order_manager,
                 oms::CheckedOrderSink& risk_sink,
                 strategy::SmaCrossStrategy* strategy)
    : loop_(loop),
      md_gateway_(md_gateway),
      md_dispatcher_(md_dispatcher),
      td_gateway_(td_gateway),
      order_manager_(order_manager),
      risk_sink_(risk_sink),
      strategy_(strategy) {}

void Console::Start() {
  // stdin modes:
  //   TTY   -- interactive REPL
  //   FIFO  -- scripted: `printf 'positions\nquit\n' | cts_app`; EOF = done,
  //            request a graceful shutdown (the script's implicit quit)
  //   other -- /dev/null, regular file: daemon mode, console disabled
  struct stat st {};
  if (fstat(STDIN_FILENO, &st) != 0 ||
      (!S_ISFIFO(st.st_mode) && !(S_ISCHR(st.st_mode) && isatty(STDIN_FILENO)))) {
    SPDLOG_INFO("console: stdin is neither TTY nor pipe, console disabled");
    return;
  }
  eof_shutdown_ = S_ISFIFO(st.st_mode);
  // Nonblocking: OnReadable drains until EAGAIN
  fcntl(STDIN_FILENO, F_SETFL, fcntl(STDIN_FILENO, F_GETFL) | O_NONBLOCK);
  std::cout << "console ready; 'help' for commands\n>>> " << std::flush;
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
      if (eof_shutdown_) {
        base::RequestShutdown();  // scripted pipe drained: implicit quit
      }
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
                 "order <buy|sell> <id> <price> <vol> [offset]\n"
                 "                       submit a limit order (offset: open "
                 "(default)|close|close-today|close-yesterday)\n"
                 "cancel <order_ref>     cancel a working order\n"
                 "orders                 order journal (OMS)\n"
                 "positions              position ledger (OMS)\n"
                 "reconcile              query broker positions into the OMS\n"
                 "account                latest account snapshot\n"
                 "rates <id...>          margin/commission rates (queries "
                 "missing ones)\n"
                 "kill                   halt orders, stop strategy, cancel all\n"
                 "resume                 clear the kill switch\n"
                 "roll <old> <new> <close_px> <open_px>\n"
                 "                       close old position, open same on new\n"
                 "strategy [start|stop]  strategy status/control\n"
                 "log-level <level>      trace|debug|info|warn|error\n"
                 "quit                   graceful shutdown\n";
  } else if (cmd == "order") {
    auto const words = SplitWords(args);
    if (words.size() < 4 || (words[0] != "buy" && words[0] != "sell")) {
      std::cout << "usage: order buy|sell <id> <price> <vol> [offset]\n";
    } else {
      bool offset_ok = false;
      td::Offset const offset =
          ParseOffset(words.size() > 4 ? words[4] : "", offset_ok);
      auto const master = td_gateway_.instruments();
      auto const* inst = master != nullptr ? master->Find(words[1]) : nullptr;
      if (!offset_ok) {
        std::cout << "unknown offset\n";
      } else if (inst == nullptr) {
        std::cout << "unknown instrument (master not loaded?)\n";
      } else {
        td::OrderRequest req{};
        req.direction =
            words[0] == "buy" ? td::Direction::kBuy : td::Direction::kSell;
        req.offset = offset;
        double price = 0.0;
        int volume = 0;
        auto const p = std::from_chars(
            words[2].data(), words[2].data() + words[2].size(), price);
        auto const v = std::from_chars(
            words[3].data(), words[3].data() + words[3].size(), volume);
        if (p.ec != std::errc{} || v.ec != std::errc{} || volume <= 0) {
          std::cout << "bad price/volume\n";
        } else {
          req.limit_price = price;
          req.volume = volume;
          base::CopyToFixedArray(req.instrument_id, inst->instrument_id);
          base::CopyToFixedArray(req.exchange_id, inst->exchange_id);
          std::string const ref = risk_sink_.InsertOrder(req);
          std::cout << (ref.empty() ? "rejected locally (td not ready)\n"
                                    : "accepted, ref=" + ref + "\n");
        }
      }
    }
  } else if (cmd == "cancel") {
    if (args.empty()) {
      std::cout << "usage: cancel <order_ref>\n";
    } else {
      int const rc = risk_sink_.CancelOrder(args);
      std::cout << (rc == 0 ? "cancel accepted\n"
                            : "cancel rejected (unknown ref or not ready)\n");
    }
  } else if (cmd == "orders") {
    auto const orders = order_manager_.orders();
    std::cout << orders.size() << " orders\n";
    for (auto const& e : orders) {
      auto const& r = e.report;
      std::cout << "  " << r.order_ref << " " << r.instrument_id << " "
                << (r.direction == td::Direction::kBuy ? "buy" : "sell")
                << " " << r.volume_traded << "/" << r.volume_total_original
                << " @" << r.limit_price << " " << StatusName(r.status)
                << "\n";
    }
  } else if (cmd == "positions") {
    auto const positions = order_manager_.positions();
    std::cout << positions.size() << " positions (trading day "
                << order_manager_.trading_day() << ")\n";
    for (auto const& p : positions) {
      std::cout << "  " << p.instrument_id << " "
                << (p.side == td::Direction::kBuy ? "long" : "short")
                << " today=" << p.today_volume << " yd=" << p.yd_volume
                << " avg=" << p.avg_price << " realized=" << p.realized_pnl
                << "\n";
    }
  } else if (cmd == "reconcile") {
    int const rc = td_gateway_.QueryPositions();
    // Spaced ~1.3s for the CTP query throttle (~1 query/sec)
    loop_.PostDelayedTask([this] { td_gateway_.QueryAccount(); },
                          std::chrono::milliseconds{1300});
    if (rc == 0) {
      std::cout << "position+account queries sent; answers arrive async\n";
    } else {
      // rc: -1 = not ready locally; negative = CTP rejected (throttle etc.)
      std::cout << "rejected, rc=" << rc << "\n";
    }
  } else if (cmd == "account") {
    auto const acc = order_manager_.account();
    if (!acc) {
      std::cout << "no account snapshot yet (reconcile first)\n";
    } else {
      std::cout << std::fixed << std::setprecision(2)
                << "balance:   " << acc->balance
                << "\navailable: " << acc->available
                << "\nmargin:    " << acc->margin
                << "\nfrozen:    " << acc->frozen_margin
                << "\nclose_pnl: " << acc->close_profit
                << "\npos_pnl:   " << acc->position_profit << "\n";
    }
  } else if (cmd == "rates") {
    auto const words = SplitWords(args);
    if (words.empty() || words[0].empty()) {
      std::cout << "usage: rates <id...>\n";
    } else {
      int slot = 0;
      for (auto const w : words) {
        std::string const id{w};
        auto const margin = td_gateway_.rates().FindMargin(id);
        auto const commission = td_gateway_.rates().FindCommission(id);
        std::cout << "  " << id << " margin:";
        if (margin.has_value()) {
          std::cout << " long=" << margin->long_ratio
                    << " short=" << margin->short_ratio;
        } else {
          std::cout << " -";
        }
        std::cout << " commission:";
        if (commission.has_value()) {
          std::cout << " open=" << commission->open_by_money << "/"
                    << commission->open_by_volume
                    << " close=" << commission->close_by_money << "/"
                    << commission->close_by_volume
                    << " close_today=" << commission->close_today_by_money
                    << "/" << commission->close_today_by_volume;
        } else {
          std::cout << " -";
        }
        std::cout << "\n";
        // Query whatever is missing, throttle-spaced
        if (!margin.has_value()) {
          loop_.PostDelayedTask(
              [this, id] { td_gateway_.QueryMarginRate(id); },
              std::chrono::milliseconds{1300 * slot++});
        }
        if (!commission.has_value()) {
          loop_.PostDelayedTask(
              [this, id] { td_gateway_.QueryCommissionRate(id); },
              std::chrono::milliseconds{1300 * slot++});
        }
      }
      if (slot > 0) {
        std::cout << "queries sent for missing entries, rerun to see them\n";
      }
    }
  } else if (cmd == "kill") {
    risk_sink_.Halt();
    if (strategy_ != nullptr) {
      strategy_->Stop();
    }
    int canceled = 0;
    for (auto const& e : order_manager_.orders()) {
      if (e.report.status == td::OrderStatus::kSubmitted ||
          e.report.status == td::OrderStatus::kPartTraded) {
        risk_sink_.CancelOrder(e.report.order_ref);
        ++canceled;
      }
    }
    std::cout << "kill switch engaged, " << canceled
              << " working orders canceled\n";
  } else if (cmd == "resume") {
    risk_sink_.Resume();
    std::cout << "kill switch cleared\n";
  } else if (cmd == "roll") {
    auto const words = SplitWords(args);
    if (words.size() != 4) {
      std::cout << "usage: roll <old> <new> <close_price> <open_price>\n";
    } else {
      double close_price = 0.0;
      double open_price = 0.0;
      auto const c = std::from_chars(
          words[2].data(), words[2].data() + words[2].size(), close_price);
      auto const o = std::from_chars(
          words[3].data(), words[3].data() + words[3].size(), open_price);
      auto const master = td_gateway_.instruments();
      auto const* old_inst = master != nullptr ? master->Find(words[0]) : nullptr;
      auto const* new_inst = master != nullptr ? master->Find(words[1]) : nullptr;
      if (c.ec != std::errc{} || o.ec != std::errc{}) {
        std::cout << "bad prices\n";
      } else if (old_inst == nullptr || new_inst == nullptr) {
        std::cout << "unknown instrument\n";
      } else {
        int legs = 0;
        for (auto const& pos : order_manager_.positions()) {
          if (pos.instrument_id != words[0] || pos.total() == 0) {
            continue;
          }
          // Close leg(s): SHFE/INE split today/yd, others plain close
          bool const split = old_inst->exchange_id == "SHFE" ||
                             old_inst->exchange_id == "INE";
          td::OrderRequest close{};
          close.direction = pos.side == td::Direction::kBuy
                                ? td::Direction::kSell
                                : td::Direction::kBuy;
          close.limit_price = close_price;
          base::CopyToFixedArray(close.instrument_id, old_inst->instrument_id);
          base::CopyToFixedArray(close.exchange_id, old_inst->exchange_id);
          if (split && pos.today_volume > 0) {
            close.offset = td::Offset::kCloseToday;
            close.volume = pos.today_volume;
            risk_sink_.InsertOrder(close);
            ++legs;
          }
          if (pos.yd_volume > 0) {
            close.offset =
                split ? td::Offset::kCloseYesterday : td::Offset::kClose;
            close.volume = pos.yd_volume;
            risk_sink_.InsertOrder(close);
            ++legs;
          }
          // Open leg on the new contract, same side and volume
          td::OrderRequest open{};
          open.direction = pos.side;
          open.offset = td::Offset::kOpen;
          open.limit_price = open_price;
          open.volume = pos.total();
          base::CopyToFixedArray(open.instrument_id, new_inst->instrument_id);
          base::CopyToFixedArray(open.exchange_id, new_inst->exchange_id);
          risk_sink_.InsertOrder(open);
          ++legs;
        }
        std::cout << (legs > 0 ? "roll submitted, " + std::to_string(legs) +
                                     " legs\n"
                               : "no position on " + std::string(words[0]) +
                                     "\n");
      }
    }
  } else if (cmd == "strategy") {
    if (strategy_ == nullptr) {
      std::cout << "no strategy configured (--strategy-instrument)\n";
    } else if (args == "start") {
      strategy_->Start();
      std::cout << "strategy started\n";
    } else if (args == "stop") {
      strategy_->Stop();
      std::cout << "strategy stopped\n";
    } else {
      auto const s = strategy_->status();
      std::cout << "strategy:       " << strategy_->name() << "\n"
                << "running:        " << strategy_->running() << "\n"
                << "position:       " << s.position << "\n"
                << "order_in_flight:" << (s.order_in_flight ? " yes" : " no")
                << (s.working_ref.empty() ? "" : " (" + s.working_ref + ")")
                << "\n"
                << "fast_sma:       " << s.fast_sma << "\n"
                << "slow_sma:       " << s.slow_sma << "\n";
    }
  } else if (cmd == "status") {
    auto const& cfg = GetAppConfig();
    auto const master = td_gateway_.instruments();
    std::cout << "front:          " << cfg.md_front << "\n"
              << "logged_in:      " << md_dispatcher_.logged_in() << "\n"
              << "td_ready:       " << td_gateway_.ready() << "\n"
              << "instruments:    "
              << (master != nullptr ? std::to_string(master->size()) : "-")
              << "\n"
              << "dropped_events: " << md_dispatcher_.dropped_events() << "\n"
              << "subscribed:";
    for (auto const& id : cfg.instruments) {
      std::cout << ' ' << id;
    }
    std::cout << "\n";
  } else if (cmd == "subscribe" || cmd == "unsubscribe") {
    std::vector<std::string> ids;
    for (auto const w : SplitWords(args)) {
      ids.emplace_back(w);
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
  std::cout << ">>> " << std::flush;
}

}  // namespace cts::app
