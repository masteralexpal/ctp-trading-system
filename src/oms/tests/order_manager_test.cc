/**
 * @file order_manager_test.cc
 * @author lipingan (lipingan.dev@outlook.com)
 * @brief Unit tests for oms::OrderManager.
 */
#include "oms/order_manager.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include "base/time_utils.h"
#include "td/order_sink.h"

namespace cts::oms {
namespace {

class FakeCancelSink final : public td::OrderSink {
 public:
  std::vector<std::string> canceled;
  std::string InsertOrder(td::OrderRequest const&) override { return {}; }
  int CancelOrder(std::string_view ref) override {
    canceled.emplace_back(ref);
    return 0;
  }
};

td::OrderReport MakeOrder(std::string_view ref,
                          std::string_view instrument,
                          td::OrderStatus status,
                          int total,
                          int traded) {
  td::OrderReport r{};
  std::memcpy(r.order_ref, ref.data(), ref.size());
  std::memcpy(r.instrument_id, instrument.data(), instrument.size());
  std::memcpy(r.exchange_id, "SHFE", 5);
  r.direction = td::Direction::kBuy;
  r.offset = td::Offset::kOpen;
  r.limit_price = 3500;
  r.volume_total_original = total;
  r.volume_traded = traded;
  r.volume_total = total - traded;
  r.status = status;
  return r;
}

td::TradeReport MakeTrade(std::string_view instrument,
                          td::Direction direction,
                          td::Offset offset,
                          double price,
                          int volume,
                          std::string_view trading_day,
                          std::string_view ref = "1",
                          std::string_view trade_id = "") {
  td::TradeReport t{};
  std::memcpy(t.instrument_id, instrument.data(), instrument.size());
  std::memcpy(t.exchange_id, "SHFE", 5);
  std::memcpy(t.order_ref, ref.data(), ref.size());
  // Auto-generate a unique id per call (dedup would collapse equal ids);
  // pass an explicit id to test replay
  if (trade_id.empty()) {
    static int next = 0;
    std::snprintf(t.trade_id, sizeof(t.trade_id), "auto%d", next++);
  } else {
    std::memcpy(t.trade_id, trade_id.data(), trade_id.size());
  }
  std::memcpy(t.trading_day, trading_day.data(), trading_day.size());
  t.direction = direction;
  t.offset = offset;
  t.price = price;
  t.volume = volume;
  return t;
}

td::PositionRow MakeRow(std::string_view instrument,
                        td::Direction side,
                        int yd,
                        int today) {
  td::PositionRow r{};
  std::memcpy(r.instrument_id, instrument.data(), instrument.size());
  std::memcpy(r.exchange_id, "SHFE", 5);
  r.direction = side;
  r.yd_volume = yd;
  r.today_volume = today;
  return r;
}

Position const* FindPosition(std::vector<Position> const& positions,
                             std::string_view instrument,
                             td::Direction side) {
  for (auto const& p : positions) {
    if (p.instrument_id == instrument && p.side == side) {
      return &p;
    }
  }
  return nullptr;
}

TEST(OrderManagerTest, JournalKeepsLatestReportPerRef) {
  OrderManager oms;
  oms.OnTdOrder(MakeOrder("1", "rb2701", td::OrderStatus::kSubmitted, 2, 0));
  oms.OnTdOrder(MakeOrder("1", "rb2701", td::OrderStatus::kPartTraded, 2, 1));
  oms.OnTdOrder(MakeOrder("2", "rb2701", td::OrderStatus::kSubmitted, 1, 0));

  auto const orders = oms.orders();
  ASSERT_EQ(orders.size(), 2);
  for (auto const& e : orders) {
    if (std::string_view(e.report.order_ref) == "1") {
      EXPECT_EQ(e.report.status, td::OrderStatus::kPartTraded);
      EXPECT_EQ(e.report.volume_traded, 1);
    }
  }
}

TEST(OrderManagerTest, TerminalOrderAbsorbsStaleReports) {
  OrderManager oms;
  oms.OnTdOrder(MakeOrder("1", "rb2701", td::OrderStatus::kSubmitted, 2, 0));
  oms.OnTdOrder(MakeOrder("1", "rb2701", td::OrderStatus::kCanceled, 2, 0));
  // Stale replay arriving after the terminal must not resurrect the order
  oms.OnTdOrder(MakeOrder("1", "rb2701", td::OrderStatus::kSubmitted, 2, 0));

  auto const orders = oms.orders();
  ASSERT_EQ(orders.size(), 1);
  EXPECT_EQ(orders[0].report.status, td::OrderStatus::kCanceled);
}

td::OrderRequest MakeRequest(std::string_view instrument,
                             td::Direction direction,
                             td::Offset offset,
                             double price,
                             int volume) {
  td::OrderRequest req{};
  std::memcpy(req.instrument_id, instrument.data(), instrument.size());
  std::memcpy(req.exchange_id, "SHFE", 5);
  req.direction = direction;
  req.offset = offset;
  req.limit_price = price;
  req.volume = volume;
  return req;
}

TEST(OrderManagerTest, IntentFinalizesOnFirstReport) {
  OrderManager oms;
  oms.RegisterIntent(
      MakeRequest("rb2701", td::Direction::kBuy, td::Offset::kOpen, 3500, 2),
      "7");
  EXPECT_TRUE(oms.HasIntent("7"));
  oms.OnTdOrder(MakeOrder("7", "rb2701", td::OrderStatus::kSubmitted, 2, 0));
  EXPECT_FALSE(oms.HasIntent("7"));
}

TEST(OrderManagerTest, RejectReportFinalizesTheIntent) {
  OrderManager oms;
  oms.RegisterIntent(
      MakeRequest("rb2701", td::Direction::kBuy, td::Offset::kOpen, 3500, 2),
      "7");
  oms.OnTdOrder(MakeOrder("7", "rb2701", td::OrderStatus::kRejected, 2, 0));
  EXPECT_FALSE(oms.HasIntent("7"));
}

TEST(OrderManagerTest, IntentRegistrationLosesToARacingReport) {
  OrderManager oms;
  // The first report arrived before the send returned (real CTP race)
  oms.OnTdOrder(MakeOrder("7", "rb2701", td::OrderStatus::kSubmitted, 2, 0));
  oms.RegisterIntent(
      MakeRequest("rb2701", td::Direction::kBuy, td::Offset::kOpen, 3500, 2),
      "7");
  EXPECT_FALSE(oms.HasIntent("7"));
}

TEST(OrderManagerTest, InFlightExposureHandsOffFromIntentToJournal) {
  OrderManager oms;
  oms.RegisterIntent(
      MakeRequest("rb2701", td::Direction::kBuy, td::Offset::kOpen, 3500, 2),
      "7");
  auto e = oms.InFlightOpenExposure("rb2701");
  EXPECT_EQ(e.net_lots, 2);
  EXPECT_DOUBLE_EQ(e.notional, 7000.0);

  // Confirm: the journal takes over the SAME remaining volume (no double
  // count during the intent -> journal handoff)
  oms.OnTdOrder(MakeOrder("7", "rb2701", td::OrderStatus::kSubmitted, 2, 0));
  e = oms.InFlightOpenExposure("rb2701");
  EXPECT_EQ(e.net_lots, 2);
  EXPECT_DOUBLE_EQ(e.notional, 7000.0);

  oms.OnTdOrder(MakeOrder("7", "rb2701", td::OrderStatus::kPartTraded, 2, 1));
  e = oms.InFlightOpenExposure("rb2701");
  EXPECT_EQ(e.net_lots, 1);  // only the remaining lot still counts
  EXPECT_DOUBLE_EQ(e.notional, 3500.0);

  oms.RegisterIntent(
      MakeRequest("rb2701", td::Direction::kSell, td::Offset::kOpen, 3600, 1),
      "8");
  e = oms.InFlightOpenExposure("rb2701");
  EXPECT_EQ(e.net_lots, 0);
  EXPECT_DOUBLE_EQ(e.notional, 3500.0 + 3600.0);

  // Terminal: the exposure releases
  oms.OnTdOrder(MakeOrder("7", "rb2701", td::OrderStatus::kCanceled, 2, 1));
  e = oms.InFlightOpenExposure("rb2701");
  EXPECT_EQ(e.net_lots, -1);
  EXPECT_DOUBLE_EQ(e.notional, 3600.0);
}

class RecordingListener final : public OmsListener {
 public:
  std::vector<std::string> events;  // e.g. "order:1:4", "trade:T1"

  void OnOrderUpdate(td::OrderReport const& report) override {
    events.push_back("order:" + std::string(report.order_ref) + ":" +
                     std::to_string(static_cast<int>(report.status)));
  }
  void OnTradeApplied(td::TradeReport const& trade) override {
    events.push_back("trade:" + std::string(trade.trade_id));
  }
  void OnPositionsReconciled(
      std::shared_ptr<std::vector<td::PositionRow> const>) override {
    events.push_back("positions");
  }
};

td::OrderReport MakeQueryOrder(std::string_view ref,
                               std::string_view instrument,
                               td::OrderStatus status,
                               int total,
                               int traded) {
  auto r = MakeOrder(ref, instrument, status, total, traded);
  r.source = td::ReportSource::kQuery;
  return r;
}

// Recovery window (not ready): the query answer is newer than any RESUME
// replay, so it overrides even a terminal journal entry (docs ch.6.1)
TEST(OrderManagerTest, RecoveryQueryOverridesJournalTerminal) {
  OrderManager oms;  // ready_ = false: the recovery window
  oms.OnTdOrder(MakeOrder("1", "rb2701", td::OrderStatus::kCanceled, 2, 0));
  oms.OnTdOrder(
      MakeQueryOrder("1", "rb2701", td::OrderStatus::kAllTraded, 2, 2));
  auto const orders = oms.orders();
  ASSERT_EQ(orders.size(), 1);
  EXPECT_EQ(orders[0].report.status, td::OrderStatus::kAllTraded);
}

// Once ready, the first terminal stands; a conflicting query row only warns
TEST(OrderManagerTest, ReadyTerminalStandsAgainstQuery) {
  OrderManager oms;
  oms.OnTdReady(true, "");
  oms.OnTdOrder(MakeOrder("1", "rb2701", td::OrderStatus::kCanceled, 2, 0));
  oms.OnTdOrder(
      MakeQueryOrder("1", "rb2701", td::OrderStatus::kAllTraded, 2, 2));
  auto const orders = oms.orders();
  ASSERT_EQ(orders.size(), 1);
  EXPECT_EQ(orders[0].report.status, td::OrderStatus::kCanceled);
}

TEST(OrderManagerTest, SnapshotDefersWhileOrdersInFlight) {
  OrderManager oms;
  RecordingListener listener;
  oms.AddListener(listener);
  oms.OnTdTrade(MakeTrade("rb2701", td::Direction::kBuy, td::Offset::kOpen,
                          3500, 2, "20261017"));
  oms.OnTdOrder(MakeOrder("1", "rb2701", td::OrderStatus::kSubmitted, 1, 0));

  // The snapshot may predate in-flight fills: deferred while not quiescent
  auto rows = std::make_shared<std::vector<td::PositionRow>>();
  oms.OnTdPositions(rows);  // broker says flat
  auto const* p = FindPosition(oms.positions(), "rb2701", td::Direction::kBuy);
  ASSERT_NE(p, nullptr);
  EXPECT_EQ(p->total(), 2);  // ledger untouched
  auto const positions_effects =
      std::count(listener.events.begin(), listener.events.end(), "positions");
  EXPECT_EQ(positions_effects, 0);  // but the positions effect is deferred

  // Quiesce: the deferred snapshot applies, effect fires then
  oms.OnTdOrder(MakeOrder("1", "rb2701", td::OrderStatus::kCanceled, 1, 0));
  p = FindPosition(oms.positions(), "rb2701", td::Direction::kBuy);
  EXPECT_TRUE(p == nullptr || p->total() == 0);
  EXPECT_EQ(listener.events.back(), "positions");
}

TEST(OrderManagerTest, AccountDefersWhileIntentPending) {
  OrderManager oms;
  oms.RegisterIntent(
      MakeRequest("rb2701", td::Direction::kBuy, td::Offset::kOpen, 3500, 1),
      "7");
  td::AccountReport account{};
  account.balance = 12345;
  oms.OnTdAccount(account);
  EXPECT_FALSE(oms.account().has_value());  // gated

  oms.OnTdOrder(MakeOrder("7", "rb2701", td::OrderStatus::kSubmitted, 1, 0));
  // A live journal order still blocks quiesce
  EXPECT_FALSE(oms.account().has_value());
  oms.OnTdOrder(MakeOrder("7", "rb2701", td::OrderStatus::kAllTraded, 1, 1));
  // Terminal announces a fill that has not arrived: still gated
  EXPECT_FALSE(oms.account().has_value());
  oms.OnTdTrade(MakeTrade("rb2701", td::Direction::kBuy, td::Offset::kOpen,
                          3500, 1, "20261017", "7", "T1"));
  ASSERT_TRUE(oms.account().has_value());  // quiesced: deferred account applied
  EXPECT_DOUBLE_EQ(oms.account()->balance, 12345);
}

struct TempState {
  explicit TempState(std::string_view name)
      : snapshot(std::filesystem::temp_directory_path() /
                 ("cts_oms_" + std::string(name) + ".txt")),
        log(std::filesystem::temp_directory_path() /
            ("cts_oms_" + std::string(name) + ".log")) {
    std::filesystem::remove(snapshot);
    std::filesystem::remove(log);
  }
  ~TempState() {
    std::filesystem::remove(snapshot);
    std::filesystem::remove(log);
  }
  std::filesystem::path const snapshot;
  std::filesystem::path const log;
};

// kill -9 seam: no shutdown snapshot, the op log alone rebuilds the state
TEST(OrderManagerTest, LogTailReplayRestoresUnsnapshottedOps) {
  TempState tmp("replay");
  {
    OrderManager a;
    a.SetSnapshotPath(tmp.snapshot);
    a.SetOpLogPath(tmp.log);
    a.OnTdOrder(MakeOrder("1", "rb2701", td::OrderStatus::kSubmitted, 2, 0));
    a.OnTdTrade(MakeTrade("rb2701", td::Direction::kBuy, td::Offset::kOpen,
                          3500, 2, "20261017", "1", "T1"));
    a.OnTdOrder(MakeOrder("1", "rb2701", td::OrderStatus::kAllTraded, 2, 2));
    // crash: dtor without SaveSnapshot; the log is the only record
  }
  OrderManager b;
  b.SetSnapshotPath(tmp.snapshot);
  b.SetOpLogPath(tmp.log);
  EXPECT_FALSE(b.LoadSnapshot());  // no snapshot file...
  auto const* p = FindPosition(b.positions(), "rb2701", td::Direction::kBuy);
  ASSERT_NE(p, nullptr);
  EXPECT_EQ(p->today_volume, 2);  // ...but the log replayed
  EXPECT_DOUBLE_EQ(p->avg_price, 3500);
  EXPECT_EQ(b.trading_day(), "20261017");
  ASSERT_EQ(b.orders().size(), 1u);
  EXPECT_EQ(b.orders()[0].report.status, td::OrderStatus::kAllTraded);

  // The RESUME seam after the restart: the same trade must not double-count
  b.OnTdTrade(MakeTrade("rb2701", td::Direction::kBuy, td::Offset::kOpen,
                        3500, 2, "20261017", "1", "T1"));
  p = FindPosition(b.positions(), "rb2701", td::Direction::kBuy);
  ASSERT_NE(p, nullptr);
  EXPECT_EQ(p->today_volume, 2);
}

// The convergence law: snapshot + log tail == full log replay == live state
TEST(OrderManagerTest, SnapshotPlusLogTailEqualsFullReplay) {
  TempState tmp("join");
  {
    OrderManager a;
    a.SetSnapshotPath(tmp.snapshot);
    a.SetOpLogPath(tmp.log);
    a.OnTdOrder(MakeOrder("1", "rb2701", td::OrderStatus::kSubmitted, 2, 0));
    a.OnTdTrade(MakeTrade("rb2701", td::Direction::kBuy, td::Offset::kOpen,
                          3500, 1, "20261017", "1", "T1"));
    a.SaveSnapshot();  // checkpoint: snapshot subsumes the log so far
    a.OnTdTrade(MakeTrade("rb2701", td::Direction::kBuy, td::Offset::kOpen,
                          3520, 1, "20261017", "1", "T2"));
    a.OnTdOrder(MakeOrder("1", "rb2701", td::OrderStatus::kAllTraded, 2, 2));
  }
  OrderManager b;  // snapshot + tail
  b.SetSnapshotPath(tmp.snapshot);
  b.SetOpLogPath(tmp.log);
  EXPECT_TRUE(b.LoadSnapshot());

  OrderManager c;  // full replay, no snapshot
  c.SetSnapshotPath(tmp.snapshot.parent_path() / "cts_oms_join_absent.txt");
  c.SetOpLogPath(tmp.log);
  EXPECT_FALSE(c.LoadSnapshot());

  for (auto* om : {&b, &c}) {
    auto const* p =
        FindPosition(om->positions(), "rb2701", td::Direction::kBuy);
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(p->today_volume, 2);
    EXPECT_DOUBLE_EQ(p->avg_price, 3510);  // (3500 + 3520) / 2, full fidelity
    EXPECT_EQ(om->trading_day(), "20261017");
    ASSERT_EQ(om->orders().size(), 1u);
    EXPECT_EQ(om->orders()[0].report.status, td::OrderStatus::kAllTraded);
  }
}

// Terminal verdict #4: an intent replayed from the log that the recovery
// query never claims evaporated in the disconnect -- discarded at ready
TEST(OrderManagerTest, EvaporatedIntentDiscardedAtReady) {
  TempState tmp("evaporate");
  {
    OrderManager a;
    a.SetSnapshotPath(tmp.snapshot);
    a.SetOpLogPath(tmp.log);
    a.RegisterIntent(
        MakeRequest("rb2701", td::Direction::kBuy, td::Offset::kOpen, 3500, 1),
        "000000000007");
  }
  OrderManager b;
  b.SetSnapshotPath(tmp.snapshot);
  b.SetOpLogPath(tmp.log);
  b.LoadSnapshot();
  EXPECT_TRUE(b.HasIntent("000000000007"));  // restored by the replay
  b.OnTdReady(true, "");  // the query answered; no row claimed the intent
  EXPECT_FALSE(b.HasIntent("000000000007"));
}

TEST(OrderManagerTest, StaleLiveOrderIsCanceledOnce) {
  OrderManager oms;
  FakeCancelSink sink;
  auto const t0 = base::NowEpochMs();
  oms.OnTdOrder(MakeOrder("1", "rb2701", td::OrderStatus::kSubmitted, 2, 0));

  oms.SweepStaleOrders(t0 + 400, sink, std::chrono::milliseconds{500});
  EXPECT_TRUE(sink.canceled.empty());  // fresh

  oms.SweepStaleOrders(t0 + 600, sink, std::chrono::milliseconds{500});
  EXPECT_EQ(sink.canceled, std::vector<std::string>{"1"});

  // One cancel per ref: later sweeps do not spam while the cancel is in flight
  oms.SweepStaleOrders(t0 + 5000, sink, std::chrono::milliseconds{500});
  EXPECT_EQ(sink.canceled.size(), 1u);

  // The terminal settles the ref
  oms.OnTdOrder(MakeOrder("1", "rb2701", td::OrderStatus::kCanceled, 2, 0));
  oms.SweepStaleOrders(t0 + 99999, sink, std::chrono::milliseconds{500});
  EXPECT_EQ(sink.canceled.size(), 1u);
}

TEST(OrderManagerTest, TerminalOrdersAreNeverSwept) {
  OrderManager oms;
  FakeCancelSink sink;
  auto const t0 = base::NowEpochMs();
  oms.OnTdOrder(MakeOrder("1", "rb2701", td::OrderStatus::kAllTraded, 2, 2));
  oms.SweepStaleOrders(t0 + 99999, sink, std::chrono::milliseconds{500});
  EXPECT_TRUE(sink.canceled.empty());
}

TEST(OrderManagerTest, EffectsPreserveArrivalOrder) {
  OrderManager oms;
  RecordingListener listener;
  oms.AddListener(listener);

  oms.OnTdOrder(MakeOrder("1", "rb2701", td::OrderStatus::kSubmitted, 2, 0));
  oms.OnTdTrade(MakeTrade("rb2701", td::Direction::kBuy, td::Offset::kOpen,
                          3500, 1, "20261017", "1", "T1"));
  oms.OnTdOrder(MakeOrder("1", "rb2701", td::OrderStatus::kPartTraded, 2, 1));
  oms.OnTdTrade(MakeTrade("rb2701", td::Direction::kBuy, td::Offset::kOpen,
                          3501, 1, "20261017", "1", "T2"));
  oms.OnTdOrder(MakeOrder("1", "rb2701", td::OrderStatus::kAllTraded, 2, 2));

  // kAllTraded = 4 in the OrderStatus enum... verify against the enum
  std::string const all_traded =
      std::to_string(static_cast<int>(td::OrderStatus::kAllTraded));
  EXPECT_EQ(listener.events,
            (std::vector<std::string>{
                "order:1:" +
                    std::to_string(static_cast<int>(td::OrderStatus::kSubmitted)),
                "trade:T1",
                "order:1:" + std::to_string(
                                 static_cast<int>(td::OrderStatus::kPartTraded)),
                "trade:T2",
                "order:1:" + all_traded}));
}

TEST(OrderManagerTest, AbsorbedAndReplayedEventsEmitNothing) {
  OrderManager oms;
  RecordingListener listener;
  oms.AddListener(listener);

  oms.OnTdOrder(MakeOrder("1", "rb2701", td::OrderStatus::kCanceled, 2, 0));
  oms.OnTdOrder(MakeOrder("1", "rb2701", td::OrderStatus::kSubmitted, 2, 0));
  auto const t = MakeTrade("rb2701", td::Direction::kBuy, td::Offset::kOpen,
                           3500, 1, "20261017", "2", "T9");
  oms.OnTdTrade(t);
  oms.OnTdTrade(t);  // replay

  ASSERT_EQ(listener.events.size(), 2u);
  EXPECT_EQ(listener.events[1], "trade:T9");
}

TEST(OrderManagerTest, TerminalEffectWaitsForTrailingFills) {
  OrderManager oms;
  RecordingListener listener;
  oms.AddListener(listener);

  // CTP order: the all-traded report arrives BEFORE the trade details
  oms.OnTdOrder(MakeOrder("1", "rb2701", td::OrderStatus::kAllTraded, 2, 2));
  EXPECT_TRUE(listener.events.empty());  // announced fills not applied yet

  oms.OnTdTrade(MakeTrade("rb2701", td::Direction::kBuy, td::Offset::kOpen,
                          3500, 1, "20261017", "1", "T1"));
  EXPECT_EQ(listener.events,
            (std::vector<std::string>{"trade:T1"}));  // one fill short

  oms.OnTdTrade(MakeTrade("rb2701", td::Direction::kBuy, td::Offset::kOpen,
                          3501, 1, "20261017", "1", "T2"));
  // Caught up: the terminal effect fires now, AFTER the fills
  ASSERT_EQ(listener.events.size(), 3u);
  EXPECT_EQ(listener.events[1], "trade:T2");
  EXPECT_EQ(listener.events[2],
            "order:1:" +
                std::to_string(static_cast<int>(td::OrderStatus::kAllTraded)));
}

TEST(OrderManagerTest, TerminalWithoutFillsFiresImmediately) {
  OrderManager oms;
  RecordingListener listener;
  oms.AddListener(listener);
  oms.OnTdOrder(MakeOrder("1", "rb2701", td::OrderStatus::kCanceled, 2, 0));
  ASSERT_EQ(listener.events.size(), 1u);
}

TEST(OrderManagerTest, OpenFillAddsTodayBucket) {
  OrderManager oms;
  oms.OnTdTrade(MakeTrade("rb2701", td::Direction::kBuy, td::Offset::kOpen,
                          3500, 2, "20261017"));
  oms.OnTdTrade(MakeTrade("rb2701", td::Direction::kBuy, td::Offset::kOpen,
                          3520, 1, "20261017"));

  auto const* p =
      FindPosition(oms.positions(), "rb2701", td::Direction::kBuy);
  ASSERT_NE(p, nullptr);
  EXPECT_EQ(p->today_volume, 3);
  EXPECT_EQ(p->yd_volume, 0);
  EXPECT_DOUBLE_EQ(p->avg_price, (3500 * 2 + 3520) / 3.0);
  EXPECT_EQ(oms.trading_day(), "20261017");
}

TEST(OrderManagerTest, ReplayedTradeDoesNotDoubleCount) {
  OrderManager oms;
  auto const t = MakeTrade("rb2701", td::Direction::kBuy, td::Offset::kOpen,
                           3500, 2, "20261017", "1", "T123");
  oms.OnTdTrade(t);
  oms.OnTdTrade(t);  // RESUME replay

  auto const* p =
      FindPosition(oms.positions(), "rb2701", td::Direction::kBuy);
  ASSERT_NE(p, nullptr);
  EXPECT_EQ(p->today_volume, 2);
}

TEST(OrderManagerTest, TradingDayRollFlipsBuckets) {
  OrderManager oms;
  oms.OnTdTrade(MakeTrade("rb2701", td::Direction::kBuy, td::Offset::kOpen,
                          3500, 2, "20261016"));
  // The night session of day T belongs to TradingDay T+1: the flip is
  // declared by the trade's TradingDay field
  oms.OnTdTrade(MakeTrade("rb2701", td::Direction::kBuy, td::Offset::kOpen,
                          3510, 1, "20261017"));

  auto const* p =
      FindPosition(oms.positions(), "rb2701", td::Direction::kBuy);
  ASSERT_NE(p, nullptr);
  EXPECT_EQ(p->yd_volume, 2);
  EXPECT_EQ(p->today_volume, 1);
}

TEST(OrderManagerTest, CloseTodayReducesTodayBucketOnly) {
  OrderManager oms;
  oms.OnTdTrade(MakeTrade("rb2701", td::Direction::kBuy, td::Offset::kOpen,
                          3500, 1, "20261016"));
  oms.OnTdTrade(MakeTrade("rb2701", td::Direction::kBuy, td::Offset::kOpen,
                          3500, 1, "20261017"));
  oms.OnTdTrade(MakeTrade("rb2701", td::Direction::kSell,
                          td::Offset::kCloseToday, 3600, 1, "20261017"));

  auto const* p =
      FindPosition(oms.positions(), "rb2701", td::Direction::kBuy);
  ASSERT_NE(p, nullptr);
  EXPECT_EQ(p->yd_volume, 1);
  EXPECT_EQ(p->today_volume, 0);
  EXPECT_DOUBLE_EQ(p->realized_pnl, 100.0);  // multiplier defaults to 1
}

TEST(OrderManagerTest, PlainCloseEatsYesterdayFirst) {
  OrderManager oms;
  oms.OnTdTrade(MakeTrade("rb2701", td::Direction::kBuy, td::Offset::kOpen,
                          3500, 1, "20261016"));
  oms.OnTdTrade(MakeTrade("rb2701", td::Direction::kBuy, td::Offset::kOpen,
                          3500, 1, "20261017"));
  oms.OnTdTrade(MakeTrade("rb2701", td::Direction::kSell, td::Offset::kClose,
                          3600, 1, "20261017"));

  auto const* p =
      FindPosition(oms.positions(), "rb2701", td::Direction::kBuy);
  ASSERT_NE(p, nullptr);
  EXPECT_EQ(p->yd_volume, 0);
  EXPECT_EQ(p->today_volume, 1);
}

TEST(OrderManagerTest, ShortSideRealizedPnl) {
  OrderManager oms;
  oms.OnTdTrade(MakeTrade("rb2701", td::Direction::kSell, td::Offset::kOpen,
                          3600, 1, "20261017"));
  oms.OnTdTrade(MakeTrade("rb2701", td::Direction::kBuy,
                          td::Offset::kCloseToday, 3500, 1, "20261017"));

  auto const* p =
      FindPosition(oms.positions(), "rb2701", td::Direction::kSell);
  // Flat positions with realized pnl stay visible
  ASSERT_NE(p, nullptr);
  EXPECT_EQ(p->total(), 0);
  EXPECT_DOUBLE_EQ(p->realized_pnl, 100.0);
}

// CTP quirk: OnRtnOrder(all-traded) arrives BEFORE OnRtnTrade; the ledger
// must only follow trades
TEST(OrderManagerTest, TerminalOrderReportBeforeTrade) {
  OrderManager oms;
  oms.OnTdOrder(MakeOrder("1", "rb2701", td::OrderStatus::kAllTraded, 1, 1));
  EXPECT_TRUE(oms.positions().empty());  // no trade yet, no position
  oms.OnTdTrade(MakeTrade("rb2701", td::Direction::kBuy, td::Offset::kOpen,
                          3500, 1, "20261017"));
  auto const* p =
      FindPosition(oms.positions(), "rb2701", td::Direction::kBuy);
  ASSERT_NE(p, nullptr);
  EXPECT_EQ(p->total(), 1);
}

TEST(OrderManagerTest, RealizedPnlUsesVolumeMultiple) {
  OrderManager oms;
  oms.SetInstrumentMaster(
      std::make_shared<instrument::InstrumentMaster>(
          instrument::InstrumentMaster::Build({[] {
            instrument::Instrument i;
            i.instrument_id = "rb2701";
            i.exchange_id = "SHFE";
            i.volume_multiple = 10;
            return i;
          }()})));
  oms.OnTdTrade(MakeTrade("rb2701", td::Direction::kBuy, td::Offset::kOpen,
                          3500, 1, "20261017"));
  oms.OnTdTrade(MakeTrade("rb2701", td::Direction::kSell,
                          td::Offset::kCloseToday, 3600, 1, "20261017"));

  auto const* p =
      FindPosition(oms.positions(), "rb2701", td::Direction::kBuy);
  ASSERT_NE(p, nullptr);
  EXPECT_DOUBLE_EQ(p->realized_pnl, 100.0 * 10);
}

TEST(OrderManagerTest, ReconcileAdoptsBrokerBuckets) {
  OrderManager oms;
  // Local tracking says: 2 long rb2701, 1 short au2512
  oms.OnTdTrade(MakeTrade("rb2701", td::Direction::kBuy, td::Offset::kOpen,
                          3500, 2, "20261017"));
  oms.OnTdTrade(MakeTrade("au2512", td::Direction::kSell, td::Offset::kOpen,
                          500, 1, "20261017"));

  // Broker disagrees on rb2701 (restart missed a fill), au2512 is flat there
  auto rows = std::make_shared<std::vector<td::PositionRow> const>(
      std::vector<td::PositionRow>{
          MakeRow("rb2701", td::Direction::kBuy, 0, 3)});
  oms.OnTdPositions(rows);

  auto const positions = oms.positions();
  auto const* rb = FindPosition(positions, "rb2701", td::Direction::kBuy);
  ASSERT_NE(rb, nullptr);
  EXPECT_EQ(rb->today_volume, 3);  // broker adopted
  EXPECT_EQ(FindPosition(positions, "au2512", td::Direction::kSell), nullptr)
      << "zeroed and flat-with-zero-pnl entries drop out of the snapshot";
}

TEST(OrderManagerTest, SnapshotRoundTrip) {
  auto const path = std::filesystem::temp_directory_path() /
                    "cts_oms_snapshot_test.txt";
  std::filesystem::remove(path);
  {
    OrderManager oms;
    oms.SetSnapshotPath(path);
    oms.OnTdOrder(
        MakeOrder("000000000001", "rb2701", td::OrderStatus::kAllTraded, 2, 2));
    oms.OnTdTrade(MakeTrade("rb2701", td::Direction::kBuy, td::Offset::kOpen,
                            3500, 2, "20261017", "000000000001"));
    oms.OnTdTrade(MakeTrade("rb2701", td::Direction::kSell,
                            td::Offset::kCloseToday, 3510, 1, "20261017",
                            "000000000002"));
    td::AccountReport acc{};
    acc.balance = 1000000;
    acc.available = 990000;
    oms.OnTdAccount(acc);
  }  // saved on every event; dtor without save must not lose state

  OrderManager restored;
  restored.SetSnapshotPath(path);
  ASSERT_TRUE(restored.LoadSnapshot());
  EXPECT_EQ(restored.trading_day(), "20261017");
  auto const positions = restored.positions();
  auto const* rb = FindPosition(positions, "rb2701", td::Direction::kBuy);
  ASSERT_NE(rb, nullptr);
  EXPECT_EQ(rb->today_volume, 1);
  EXPECT_DOUBLE_EQ(rb->avg_price, 3500);
  EXPECT_DOUBLE_EQ(rb->realized_pnl, 10);  // (3510-3500) * 1 * multiple 1
  EXPECT_EQ(restored.orders().size(), 1u);
  ASSERT_TRUE(restored.account().has_value());
  EXPECT_DOUBLE_EQ(restored.account()->balance, 1000000);
  std::filesystem::remove(path);
}

TEST(OrderManagerTest, SnapshotWithoutTradingDayRoundTrips) {
  auto const path = std::filesystem::temp_directory_path() /
                    "cts_oms_snapshot_noday_test.txt";
  std::filesystem::remove(path);
  {
    OrderManager oms;
    oms.SetSnapshotPath(path);
    // Reconcile-only state: no trade ever set the trading day
    auto rows = std::make_shared<std::vector<td::PositionRow>>();
    td::PositionRow row{};
    std::strncpy(row.instrument_id, "au2612", sizeof(row.instrument_id) - 1);
    row.direction = td::Direction::kBuy;
    row.yd_volume = 1;
    rows->push_back(row);
    oms.OnTdPositions(std::move(rows));
  }

  OrderManager restored;
  restored.SetSnapshotPath(path);
  ASSERT_TRUE(restored.LoadSnapshot());
  EXPECT_TRUE(restored.trading_day().empty());
  auto const* au =
      FindPosition(restored.positions(), "au2612", td::Direction::kBuy);
  ASSERT_NE(au, nullptr);
  EXPECT_EQ(au->yd_volume, 1);
  std::filesystem::remove(path);
}

TEST(OrderManagerTest, LoadSnapshotMissingFileIsFreshStart) {
  OrderManager oms;
  oms.SetSnapshotPath(std::filesystem::temp_directory_path() /
                      "cts_oms_no_such_file.txt");
  EXPECT_FALSE(oms.LoadSnapshot());
  EXPECT_TRUE(oms.positions().empty());
}

}  // namespace
}  // namespace cts::oms
