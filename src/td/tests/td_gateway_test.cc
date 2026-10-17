/**
 * @file td_gateway_test.cc
 * @author lipingan (lipingan.dev@outlook.com)
 * @brief TdGateway/TdSpiAdapter tests via the TdApiOps seam (no real CTP).
 */
#include "td/td_gateway.h"

#include <cstring>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "td/detail/td_api_ops.h"

namespace cts::td {
namespace {

// Records every call; tests drive the Spi callbacks by hand.
// calls lives outside: Disconnect deletes the fake, but the log must survive
class FakeTdApiOps final : public detail::TdApiOps {
 public:
  explicit FakeTdApiOps(std::vector<std::string>& calls) : calls(calls) {}

  std::vector<std::string>& calls;
  CThostFtdcTraderSpi* spi = nullptr;
  std::string front;
  int private_topic = -1;
  int public_topic = -1;
  CThostFtdcReqAuthenticateField auth_req{};
  CThostFtdcReqUserLoginField login_req{};
  CThostFtdcSettlementInfoConfirmField confirm_req{};
  std::vector<CThostFtdcInputOrderField> insert_reqs;
  std::vector<CThostFtdcInputOrderActionField> action_reqs;
  int qry_instrument_calls = 0;
  int qry_position_calls = 0;
  CThostFtdcQryInvestorPositionField qry_position_req{};
  int qry_account_calls = 0;

  void RegisterSpi(CThostFtdcTraderSpi* s) override {
    calls.emplace_back("RegisterSpi");
    spi = s;
  }
  void RegisterFront(char* f) override {
    calls.emplace_back("RegisterFront");
    front = f;
  }
  void SubscribePrivateTopic(int t) override { private_topic = t; }
  void SubscribePublicTopic(int t) override { public_topic = t; }
  void Init() override { calls.emplace_back("Init"); }
  void Release() override { calls.emplace_back("Release"); }
  int ReqAuthenticate(CThostFtdcReqAuthenticateField* req,
                      int /*id*/) override {
    auth_req = *req;
    return 0;
  }
  int ReqUserLogin(CThostFtdcReqUserLoginField* req, int /*id*/) override {
    login_req = *req;
    return 0;
  }
  int ReqSettlementInfoConfirm(CThostFtdcSettlementInfoConfirmField* req,
                               int /*id*/) override {
    confirm_req = *req;
    return 0;
  }
  int ReqOrderInsert(CThostFtdcInputOrderField* req, int /*id*/) override {
    insert_reqs.push_back(*req);
    return 0;
  }
  int ReqOrderAction(CThostFtdcInputOrderActionField* req,
                     int /*id*/) override {
    action_reqs.push_back(*req);
    return 0;
  }
  int ReqQryInstrument(CThostFtdcQryInstrumentField* /*req*/,
                       int /*id*/) override {
    ++qry_instrument_calls;
    return 0;
  }
  int ReqQryInvestorPosition(CThostFtdcQryInvestorPositionField* req,
                             int /*id*/) override {
    ++qry_position_calls;
    qry_position_req = *req;
    return 0;
  }
  int ReqQryTradingAccount(CThostFtdcQryTradingAccountField* /*req*/,
                           int /*id*/) override {
    ++qry_account_calls;
    return 0;
  }
  int ReqQryInstrumentMarginRate(
      CThostFtdcQryInstrumentMarginRateField* req, int /*id*/) override {
    ++qry_margin_calls;
    qry_margin_req = *req;
    return 0;
  }
  int ReqQryInstrumentCommissionRate(
      CThostFtdcQryInstrumentCommissionRateField* req, int /*id*/) override {
    ++qry_commission_calls;
    qry_commission_req = *req;
    return 0;
  }
  int qry_margin_calls = 0;
  int qry_commission_calls = 0;
  CThostFtdcQryInstrumentMarginRateField qry_margin_req{};
  CThostFtdcQryInstrumentCommissionRateField qry_commission_req{};
};

struct RecordingDelegate : TdDelegate {
  std::vector<std::string> events;

  void OnTdConnected() override { events.emplace_back("connected"); }
  void OnTdDisconnected(int reason) override {
    events.emplace_back("disconnected:" + std::to_string(reason));
  }
  void OnTdReady(bool success, std::string_view msg) override {
    events.emplace_back(std::string("ready:") + (success ? "ok:" : "fail:") +
                        std::string(msg));
  }
  void OnTdOrder(OrderReport const& order) override {
    events.emplace_back(std::string("order:") + order.order_ref + ":" +
                        std::to_string(static_cast<int>(order.status)));
  }
  void OnTdTrade(TradeReport const& trade) override {
    events.emplace_back(std::string("trade:") + trade.instrument_id + ":" +
                        std::to_string(trade.volume));
  }
  void OnTdAccount(AccountReport const& account) override {
    events.emplace_back("account:" + std::to_string(account.balance));
  }
  void OnTdPositions(
      std::shared_ptr<std::vector<PositionRow> const> rows) override {
    for (auto const& row : *rows) {
      events.emplace_back(std::string("position:") + row.instrument_id +
                          ":" +
                          std::to_string(static_cast<int>(row.direction)) +
                          ":" + std::to_string(row.yd_volume) + ":" +
                          std::to_string(row.today_volume));
    }
  }
  void OnTdError(int error_id, std::string_view msg) override {
    events.emplace_back("error:" + std::to_string(error_id) + ":" +
                        std::string(msg));
  }
};

struct TdGatewayTest : testing::Test {
  // Declaration order is load-bearing: ~TdGateway -> Disconnect() drives the
  // fake, which logs into `calls` -- so `calls` must be destroyed last
  std::vector<std::string> calls;
  TdGateway gateway;
  RecordingDelegate delegate;
  FakeTdApiOps* fake = nullptr;  // owned by gateway once connected

  void Connect() {
    auto ops = std::make_unique<FakeTdApiOps>(calls);
    fake = ops.get();
    gateway.Connect(
        {
            .front = "tcp://example:12346",
            .broker_id = "9999",
            .investor_id = "276491",
            .password = "secret",
            .app_id = "simnow_client_test",
            .auth_code = "0000000000000000",
            .flow_dir = "unused-with-fake",
        },
        delegate, std::move(ops));
  }

  // Drives the full startup chain to ready (auth -> login -> confirm)
  void ReadyUp(int max_order_ref = 0) {
    fake->spi->OnFrontConnected();
    fake->spi->OnRspAuthenticate(nullptr, nullptr, 1, true);
    CThostFtdcRspUserLoginField rsp{};
    rsp.FrontID = 7;
    rsp.SessionID = 99;
    std::snprintf(rsp.MaxOrderRef, sizeof(rsp.MaxOrderRef), "%d",
                  max_order_ref);
    fake->spi->OnRspUserLogin(&rsp, nullptr, 2, true);
    fake->spi->OnRspSettlementInfoConfirm(nullptr, nullptr, 3, true);
  }

  OrderRequest MakeRequest(char const* id = "rb2701") {
    OrderRequest req{};
    std::strncpy(req.instrument_id, id, sizeof(req.instrument_id) - 1);
    std::strncpy(req.exchange_id, "SHFE", sizeof(req.exchange_id) - 1);
    req.direction = Direction::kBuy;
    req.offset = Offset::kOpen;
    req.limit_price = 3500.0;
    req.volume = 1;
    return req;
  }
};

TEST_F(TdGatewayTest, ConnectDrivesApiInOrder) {
  Connect();
  EXPECT_EQ(calls,
            (std::vector<std::string>{"RegisterSpi", "RegisterFront", "Init"}));
  EXPECT_EQ(fake->front, "tcp://example:12346");
  EXPECT_EQ(fake->private_topic, 1);  // THOST_TERT_RESUME
  EXPECT_EQ(fake->public_topic, 1);
}

TEST_F(TdGatewayTest, StartupChainRunsToReady) {
  Connect();
  ReadyUp();
  EXPECT_TRUE(gateway.ready());
  EXPECT_EQ(delegate.events,
            (std::vector<std::string>{"connected", "ready:ok:"}));
  EXPECT_STREQ(fake->auth_req.AppID, "simnow_client_test");
  EXPECT_STREQ(fake->login_req.Password, "secret");
  EXPECT_STREQ(fake->confirm_req.InvestorID, "276491");
}

TEST_F(TdGatewayTest, AuthFailureAbortsChain) {
  Connect();
  fake->spi->OnFrontConnected();
  CThostFtdcRspInfoField info{};
  info.ErrorID = 63;
  std::strcpy(info.ErrorMsg, "\xB4\xED\xCE\xF3");  // "错误" in GBK
  fake->spi->OnRspAuthenticate(nullptr, &info, 1, true);
  EXPECT_FALSE(gateway.ready());
  EXPECT_EQ(delegate.events,
            (std::vector<std::string>{"connected", "ready:fail:错误"}));
}

TEST_F(TdGatewayTest, OrderRefContinuesFromMaxOrderRef) {
  Connect();
  ReadyUp(/*max_order_ref=*/42);
  std::string const ref = gateway.InsertOrder(MakeRequest());
  EXPECT_EQ(ref, "000000000043");
  ASSERT_EQ(fake->insert_reqs.size(), 1u);
  EXPECT_STREQ(fake->insert_reqs[0].OrderRef, "000000000043");
}

TEST_F(TdGatewayTest, InsertOrderFillsCtpFields) {
  Connect();
  ReadyUp();
  gateway.InsertOrder(MakeRequest());
  ASSERT_EQ(fake->insert_reqs.size(), 1u);
  auto const& f = fake->insert_reqs[0];
  EXPECT_STREQ(f.BrokerID, "9999");
  EXPECT_STREQ(f.InstrumentID, "rb2701");
  EXPECT_STREQ(f.ExchangeID, "SHFE");
  EXPECT_EQ(f.Direction, THOST_FTDC_D_Buy);
  EXPECT_EQ(f.CombOffsetFlag[0], THOST_FTDC_OF_Open);
  EXPECT_EQ(f.OrderPriceType, THOST_FTDC_OPT_LimitPrice);
  EXPECT_EQ(f.TimeCondition, THOST_FTDC_TC_GFD);
  EXPECT_DOUBLE_EQ(f.LimitPrice, 3500.0);
  EXPECT_EQ(f.VolumeTotalOriginal, 1);
}

TEST_F(TdGatewayTest, InsertOrderBeforeReadyIsRejectedLocally) {
  Connect();
  fake->spi->OnFrontConnected();  // connected but the chain is unfinished
  EXPECT_EQ(gateway.InsertOrder(MakeRequest()), "");
  EXPECT_TRUE(fake->insert_reqs.empty());
}

TEST_F(TdGatewayTest, RtnOrderIsTranslatedAndForwarded) {
  Connect();
  ReadyUp();
  CThostFtdcOrderField f{};
  std::strcpy(f.InstrumentID, "rb2701");
  std::strcpy(f.OrderRef, "000000000001");
  std::strcpy(f.OrderSysID, "12345");
  f.Direction = THOST_FTDC_D_Sell;
  f.CombOffsetFlag[0] = THOST_FTDC_OF_CloseToday;
  f.OrderStatus = THOST_FTDC_OST_PartTradedQueueing;
  f.VolumeTotalOriginal = 3;
  f.VolumeTraded = 1;
  f.VolumeTotal = 2;
  f.FrontID = 7;
  f.SessionID = 99;
  fake->spi->OnRtnOrder(&f);
  // kPartTraded == 1 in the OrderStatus enum
  EXPECT_EQ(delegate.events.back(), "order:000000000001:1");
}

TEST_F(TdGatewayTest, RejectedInsertDropsTheCancelKey) {
  Connect();
  ReadyUp();
  std::string const ref = gateway.InsertOrder(MakeRequest());
  ASSERT_FALSE(ref.empty());

  CThostFtdcRspInfoField info{};
  info.ErrorID = 50;
  std::strcpy(info.ErrorMsg, "\xB4\xED\xCE\xF3");
  fake->spi->OnRspOrderInsert(&fake->insert_reqs[0], &info, 1, true);
  EXPECT_EQ(gateway.CancelOrder(ref), -1);  // key erased: no longer cancellable
}

TEST_F(TdGatewayTest, CancelOrderUsesTheStoredKey) {
  Connect();
  ReadyUp();
  std::string const ref = gateway.InsertOrder(MakeRequest());
  // The exchange accept refreshes front/session
  CThostFtdcOrderField report{};
  std::strcpy(report.OrderRef, ref.c_str());
  std::strcpy(report.InstrumentID, "rb2701");
  report.OrderStatus = THOST_FTDC_OST_NoTradeQueueing;
  report.FrontID = 8;
  report.SessionID = 100;
  fake->spi->OnRtnOrder(&report);

  EXPECT_EQ(gateway.CancelOrder(ref), 0);
  ASSERT_EQ(fake->action_reqs.size(), 1u);
  auto const& f = fake->action_reqs[0];
  EXPECT_STREQ(f.OrderRef, ref.c_str());
  EXPECT_EQ(f.FrontID, 8);
  EXPECT_EQ(f.SessionID, 100);
  EXPECT_EQ(f.ActionFlag, THOST_FTDC_AF_Delete);
  EXPECT_STREQ(f.InstrumentID, "rb2701");
}

TEST_F(TdGatewayTest, CancelOrderPrefersOrderSysIdWhenKnown) {
  Connect();
  ReadyUp();
  std::string const ref = gateway.InsertOrder(MakeRequest());
  CThostFtdcOrderField report{};
  std::strcpy(report.OrderRef, ref.c_str());
  std::strcpy(report.InstrumentID, "rb2701");
  std::strcpy(report.OrderSysID, "12345");
  report.OrderStatus = THOST_FTDC_OST_NoTradeQueueing;
  report.FrontID = 8;
  report.SessionID = 100;
  fake->spi->OnRtnOrder(&report);

  EXPECT_EQ(gateway.CancelOrder(ref), 0);
  ASSERT_EQ(fake->action_reqs.size(), 1u);
  auto const& f = fake->action_reqs[0];
  // The exchange-side sys id survives a reconnect; front/session do not
  EXPECT_STREQ(f.OrderSysID, "12345");
  EXPECT_EQ(f.FrontID, 0);
  EXPECT_EQ(f.SessionID, 0);
}

TEST_F(TdGatewayTest, TerminalOrdersAreNotCancellable) {
  Connect();
  ReadyUp();
  std::string const ref = gateway.InsertOrder(MakeRequest());
  CThostFtdcOrderField report{};
  std::strcpy(report.OrderRef, ref.c_str());
  report.OrderStatus = THOST_FTDC_OST_AllTraded;
  fake->spi->OnRtnOrder(&report);
  EXPECT_EQ(gateway.CancelOrder(ref), -1);
}

TEST_F(TdGatewayTest, QueryInstrumentsBuildsTheMaster) {
  Connect();
  ReadyUp();
  EXPECT_EQ(gateway.instruments(), nullptr);
  EXPECT_EQ(gateway.QueryInstruments(), 0);
  EXPECT_EQ(fake->qry_instrument_calls, 1);

  CThostFtdcInstrumentField inst{};
  std::strcpy(inst.InstrumentID, "rb2701");
  std::strcpy(inst.ExchangeID, "SHFE");
  std::strcpy(inst.ProductID, "rb");
  inst.PriceTick = 1.0;
  inst.VolumeMultiple = 10;
  inst.ProductClass = THOST_FTDC_PC_Futures;
  fake->spi->OnRspQryInstrument(&inst, nullptr, 1, false);
  EXPECT_EQ(gateway.instruments(), nullptr);  // published only at is_last
  std::strcpy(inst.InstrumentID, "cu2611");
  std::strcpy(inst.ProductID, "cu");
  fake->spi->OnRspQryInstrument(&inst, nullptr, 1, true);

  auto master = gateway.instruments();
  ASSERT_NE(master, nullptr);
  EXPECT_EQ(master->size(), 2u);
  auto const* rb = master->Find("rb2701");
  ASSERT_NE(rb, nullptr);
  EXPECT_EQ(rb->exchange_id, "SHFE");
  EXPECT_DOUBLE_EQ(rb->price_tick, 1.0);
}

TEST_F(TdGatewayTest, TradeIsTranslatedAtTheBoundary) {
  Connect();
  ReadyUp();
  CThostFtdcTradeField f{};
  std::strcpy(f.InstrumentID, "rb2701");
  std::strcpy(f.OrderRef, "000000000001");
  std::strcpy(f.TradeID, "9999");
  f.Direction = THOST_FTDC_D_Buy;
  f.OffsetFlag = THOST_FTDC_OF_Open;
  f.Price = 3501.0;
  f.Volume = 2;
  std::strcpy(f.TradeDate, "20261013");
  std::strcpy(f.TradeTime, "21:35:00");
  fake->spi->OnRtnTrade(&f);
  EXPECT_EQ(delegate.events.back(), "trade:rb2701:2");
}

TEST_F(TdGatewayTest, DisconnectIsIdempotentAndOrdered) {
  Connect();
  std::size_t const before = calls.size();
  gateway.Disconnect();  // deletes the fake; only `calls` survives
  ASSERT_EQ(calls.size(), before + 2);
  EXPECT_EQ(calls[before], "RegisterSpi");
  EXPECT_EQ(calls[before + 1], "Release");
  gateway.Disconnect();  // no-op
  EXPECT_EQ(calls.size(), before + 2);
}

TEST_F(TdGatewayTest, QueryPositionsMergesPositionDateRows) {
  Connect();
  ReadyUp();
  EXPECT_EQ(gateway.QueryPositions(), 0);
  EXPECT_EQ(fake->qry_position_calls, 1);
  EXPECT_STREQ(fake->qry_position_req.InvestorID, "276491");

  // CTP splits one position into a History row and a Today row (and fires a
  // trailing nullptr row at is_last)
  CThostFtdcInvestorPositionField row{};
  std::strncpy(row.InstrumentID, "rb2701", sizeof(row.InstrumentID) - 1);
  std::strncpy(row.ExchangeID, "SHFE", sizeof(row.ExchangeID) - 1);
  row.PosiDirection = THOST_FTDC_PD_Long;
  row.PositionDate = THOST_FTDC_PSD_History;
  row.YdPosition = 2;
  row.Position = 2;
  fake->spi->OnRspQryInvestorPosition(&row, nullptr, 1, false);
  row.PositionDate = THOST_FTDC_PSD_Today;
  row.YdPosition = 0;
  row.Position = 1;
  fake->spi->OnRspQryInvestorPosition(&row, nullptr, 1, false);
  fake->spi->OnRspQryInvestorPosition(nullptr, nullptr, 1, true);

  ASSERT_EQ(delegate.events.size(), 3);  // connected, ready, one merged row
  EXPECT_EQ(delegate.events[2],
            "position:rb2701:" +
                std::to_string(static_cast<int>(Direction::kBuy)) + ":2:1");
}

TEST_F(TdGatewayTest, QueryAccountReturnsSnapshot) {
  Connect();
  ReadyUp();
  EXPECT_EQ(gateway.QueryAccount(), 0);
  EXPECT_EQ(fake->qry_account_calls, 1);

  CThostFtdcTradingAccountField acc{};
  acc.Balance = 1000000.0;
  acc.Available = 980000.0;
  fake->spi->OnRspQryTradingAccount(&acc, nullptr, 1, true);

  ASSERT_FALSE(delegate.events.empty());
  EXPECT_EQ(delegate.events.back(), "account:1000000.000000");
}

TEST_F(TdGatewayTest, QueryPositionsRejectedBeforeReady) {
  Connect();
  EXPECT_EQ(gateway.QueryPositions(), -1);
  EXPECT_EQ(fake->qry_position_calls, 0);
}

TEST_F(TdGatewayTest, MarginRateLandsInRateTable) {
  Connect();
  ReadyUp();
  EXPECT_EQ(gateway.QueryMarginRate("rb2701"), 0);
  EXPECT_EQ(fake->qry_margin_calls, 1);
  EXPECT_STREQ(fake->qry_margin_req.InstrumentID, "rb2701");

  CThostFtdcInstrumentMarginRateField rate{};
  std::strcpy(rate.InstrumentID, "rb2701");
  rate.LongMarginRatioByMoney = 0.12;
  rate.ShortMarginRatioByMoney = 0.14;
  fake->spi->OnRspQryInstrumentMarginRate(&rate, nullptr, 1, true);

  auto const m = gateway.rates().FindMargin("rb2701");
  ASSERT_TRUE(m.has_value());
  EXPECT_DOUBLE_EQ(m->long_ratio, 0.12);
  EXPECT_DOUBLE_EQ(m->short_ratio, 0.14);
  EXPECT_STREQ(m->instrument_id, "rb2701");
}

TEST_F(TdGatewayTest, CommissionRateLandsInRateTable) {
  Connect();
  ReadyUp();
  EXPECT_EQ(gateway.QueryCommissionRate("rb2701"), 0);
  EXPECT_EQ(fake->qry_commission_calls, 1);

  CThostFtdcInstrumentCommissionRateField rate{};
  std::strcpy(rate.InstrumentID, "rb2701");
  rate.OpenRatioByMoney = 0.0001;
  rate.CloseTodayRatioByVolume = 3.0;
  fake->spi->OnRspQryInstrumentCommissionRate(&rate, nullptr, 1, true);

  auto const c = gateway.rates().FindCommission("rb2701");
  ASSERT_TRUE(c.has_value());
  EXPECT_DOUBLE_EQ(c->open_by_money, 0.0001);
  EXPECT_DOUBLE_EQ(c->close_today_by_volume, 3.0);
}

}  // namespace
}  // namespace cts::td
