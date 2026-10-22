/**
 * @file order_manager_fuzz.cc
 * @author lipingan (lipingan.dev@outlook.com)
 * @brief Coverage-guided property fuzzing for the OMS fold (libFuzzer).
 *
 * Engine: libFuzzer mutates a byte stream. Oracle: the laws of
 * docs/op-driven-transaction.md ch.5, checked differentially -- the SAME
 * OrderManager type folds the raw stream (duplicates included, as RESUME
 * replay produces) and a deduplicated reference stream; both must agree.
 * Journal law: an order that ever went terminal must stay terminal.
 *
 * A law violation abort()s so libFuzzer saves + minimizes the input.
 * Set CTS_FUZZ_TRACE=1 to print the decoded op stream (replay aid).
 * Build: -DCTS_ENABLE_FUZZERS=ON, target oms_fuzz. Never in the default build.
 */
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>
#include <string>
#include <unordered_map>

#include "oms/order_manager.h"

namespace {

// Minimal byte reader: exhausted input reads as zeros (no exceptions)
struct ByteReader {
  std::uint8_t const* cur;
  std::size_t left;

  std::uint8_t U8() {
    if (left == 0) {
      return 0;
    }
    --left;
    return *cur++;
  }
  std::uint32_t U32() {
    std::uint32_t v = 0;
    for (int i = 0; i < 4; ++i) {
      v = (v << 8) | U8();
    }
    return v;
  }
};

bool g_trace = false;

void CheckLaw(bool ok, char const* law) {
  if (!ok) {
    std::fprintf(stderr, "LAW VIOLATION: %s\n", law);
    std::abort();  // libFuzzer catches this and saves the reproducer
  }
}

char const* kDays[] = {"20261016", "20261017", "20261019"};
char const* kInstruments[] = {"rb2701", "au2612"};

cts::td::TradeReport DecodeTrade(ByteReader& br) {
  cts::td::TradeReport t{};
  std::strncpy(t.instrument_id, kInstruments[br.U8() % 2],
               sizeof(t.instrument_id) - 1);
  std::strncpy(t.exchange_id, "SHFE", sizeof(t.exchange_id) - 1);
  std::snprintf(t.order_ref, sizeof(t.order_ref), "%012d", br.U8() % 4);
  // Small id pool: duplicates are the common case (replay seam)
  std::snprintf(t.trade_id, sizeof(t.trade_id), "T%04d", br.U8() % 16);
  std::strncpy(t.trading_day, kDays[br.U8() % 3], sizeof(t.trading_day) - 1);
  t.direction =
      br.U8() % 2 ? cts::td::Direction::kSell : cts::td::Direction::kBuy;
  cts::td::Offset const kOffsets[] = {
      cts::td::Offset::kOpen, cts::td::Offset::kOpen,  // opens dominate
      cts::td::Offset::kClose, cts::td::Offset::kCloseToday,
      cts::td::Offset::kCloseYesterday};
  t.offset = kOffsets[br.U8() % 5];
  t.price = 3000.0 + static_cast<double>(br.U32() % 2000);
  t.volume = 1 + br.U8() % 8;
  return t;
}

cts::td::OrderReport DecodeOrder(ByteReader& br) {
  cts::td::OrderReport r{};
  std::strncpy(r.instrument_id, kInstruments[br.U8() % 2],
               sizeof(r.instrument_id) - 1);
  std::strncpy(r.exchange_id, "SHFE", sizeof(r.exchange_id) - 1);
  std::snprintf(r.order_ref, sizeof(r.order_ref), "%012d", br.U8() % 4);
  cts::td::OrderStatus const kStatuses[] = {
      cts::td::OrderStatus::kSubmitted, cts::td::OrderStatus::kPartTraded,
      cts::td::OrderStatus::kAllTraded, cts::td::OrderStatus::kCanceled,
      cts::td::OrderStatus::kRejected,  cts::td::OrderStatus::kUnknown};
  r.status = kStatuses[br.U8() % 6];
  r.limit_price = 3500;
  r.volume_total_original = 2;
  r.volume_traded = br.U8() % 3;
  r.volume_total = 2 - r.volume_traded;
  return r;
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(std::uint8_t const* data,
                                      std::size_t size) {
  g_trace = std::getenv("CTS_FUZZ_TRACE") != nullptr;
  ByteReader br{data, size};
  cts::oms::OrderManager raw;    // folds the stream as delivered (dups incl.)
  cts::oms::OrderManager dedup;  // folds unique trades only (the law)
  std::set<std::string> seen_trades;
  std::unordered_map<std::string, bool> terminal_seen;

  for (int ops = 0; ops < 256 && br.left > 0; ++ops) {
    if (br.U8() % 4 != 3) {  // 3/4 trades, 1/4 order reports
      auto const t = DecodeTrade(br);
      if (g_trace) {
        std::fprintf(stderr, "trade %s %s x%d @%.0f day %s\n", t.trade_id,
                     t.instrument_id, t.volume, t.price, t.trading_day);
      }
      raw.OnTdTrade(t);
      if (seen_trades.insert(std::string(t.trade_id) + t.trading_day).second) {
        dedup.OnTdTrade(t);
      }
    } else {
      auto const r = DecodeOrder(br);
      if (g_trace) {
        std::fprintf(stderr, "order %s status %d\n", r.order_ref,
                     static_cast<int>(r.status));
      }
      raw.OnTdOrder(r);
      dedup.OnTdOrder(r);
      if (cts::td::IsTerminal(r.status)) {
        terminal_seen[r.order_ref] = true;
      }
    }
  }

  // Law 1 (idempotency): replayed trades must not change the ledger
  auto a = raw.positions();
  auto b = dedup.positions();
  auto by_key = [](cts::oms::Position const& x, cts::oms::Position const& y) {
    return x.instrument_id == y.instrument_id
               ? x.side < y.side
               : x.instrument_id < y.instrument_id;
  };
  std::sort(a.begin(), a.end(), by_key);
  std::sort(b.begin(), b.end(), by_key);
  CheckLaw(a.size() == b.size(), "ledger size diverges under replay");
  for (std::size_t i = 0; i < a.size(); ++i) {
    CheckLaw(a[i].instrument_id == b[i].instrument_id && a[i].side == b[i].side,
             "ledger key diverges under replay");
    CheckLaw(a[i].today_volume == b[i].today_volume &&
                 a[i].yd_volume == b[i].yd_volume,
             "position volume diverges under replay");
    CheckLaw(std::fabs(a[i].avg_price - b[i].avg_price) < 1e-6,
             "avg price diverges under replay");
    CheckLaw(std::fabs(a[i].realized_pnl - b[i].realized_pnl) < 1e-6,
             "realized pnl diverges under replay");
  }

  // Law 2 (terminal absorption): a terminal order must stay terminal
  for (auto const& entry : raw.orders()) {
    if (terminal_seen.count(entry.report.order_ref) != 0) {
      CheckLaw(cts::td::IsTerminal(entry.report.status),
               "terminal order resurrected by a stale report");
    }
  }
  return 0;
}
