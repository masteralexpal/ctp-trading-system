/**
 * @file md_gateway_test.cc
 * @author lipingan (lipingan.dev@outlook.com)
 * @brief MdGateway/MdSpiAdapter tests via the MdApiOps seam (no real CTP).
 */
#include "md/md_gateway.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <cstring>

#include <gtest/gtest.h>

#include "md/detail/md_api_ops.h"

namespace cts::md {
namespace {

// Records every call; tests drive the Spi callbacks by hand.
// calls lives outside: Disconnect deletes the fake, but the log must survive
class FakeMdApiOps final : public detail::MdApiOps {
 public:
  explicit FakeMdApiOps(std::vector<std::string>& calls) : calls(calls) {}

  std::vector<std::string>& calls;
  CThostFtdcMdSpi* spi = nullptr;
  std::string front;
  CThostFtdcReqUserLoginField login_req{};
  std::vector<std::vector<std::string>> subscribe_calls;
  std::vector<std::vector<std::string>> unsubscribe_calls;

  void RegisterSpi(CThostFtdcMdSpi* s) override {
    calls.emplace_back("RegisterSpi");
    spi = s;
  }
  void RegisterFront(char* f) override {
    calls.emplace_back("RegisterFront");
    front = f;
  }
  void Init() override { calls.emplace_back("Init"); }
  void Release() override { calls.emplace_back("Release"); }
  int ReqUserLogin(CThostFtdcReqUserLoginField* req, int /*id*/) override {
    login_req = *req;
    return 0;
  }
  int SubscribeMarketData(std::span<char*> ids) override {
    subscribe_calls.emplace_back(ids.begin(), ids.end());
    return 0;
  }
  int UnSubscribeMarketData(std::span<char*> ids) override {
    unsubscribe_calls.emplace_back(ids.begin(), ids.end());
    return 0;
  }
};

struct RecordingDelegate : MdDelegate {
  std::vector<std::string> events;

  void OnMdConnected() override { events.emplace_back("connected"); }
  void OnMdDisconnected(int reason) override {
    events.emplace_back("disconnected:" + std::to_string(reason));
  }
  void OnMdLogin(bool success, std::string_view msg) override {
    events.emplace_back(std::string("login:") + (success ? "ok:" : "fail:") +
                        std::string(msg));
  }
  void OnMdTick(Tick const& tick) override {
    events.emplace_back(std::string("tick:") + tick.instrument_id);
  }
  void OnMdError(int error_id, std::string_view msg) override {
    events.emplace_back("error:" + std::to_string(error_id) + ":" +
                        std::string(msg));
  }
};

struct MdGatewayTest : testing::Test {
  // Declaration order is load-bearing: ~MdGateway -> Disconnect() drives the
  // fake, which logs into `calls` -- so `calls` must be destroyed last
  std::vector<std::string> calls;
  MdGateway gateway;
  RecordingDelegate delegate;
  FakeMdApiOps* fake = nullptr;  // owned by gateway once connected

  void Connect() {
    auto ops = std::make_unique<FakeMdApiOps>(calls);
    fake = ops.get();
    gateway.Connect(
        {
            .front = "tcp://example:12345",
            .broker_id = "9999",
            .investor_id = "276491",
            .password = "secret",
            .flow_dir = "unused-with-fake",
        },
        delegate, std::move(ops));
  }

  void LoginOk() {
    fake->spi->OnFrontConnected();
    CThostFtdcRspUserLoginField rsp{};
    fake->spi->OnRspUserLogin(&rsp, nullptr, 1, true);
  }
};

TEST_F(MdGatewayTest, ConnectDrivesApiInOrder) {
  Connect();
  EXPECT_EQ(calls,
            (std::vector<std::string>{"RegisterSpi", "RegisterFront", "Init"}));
  EXPECT_EQ(fake->front, "tcp://example:12345");
}

TEST_F(MdGatewayTest, FrontConnectedTriggersLoginWithConfigFields) {
  Connect();
  fake->spi->OnFrontConnected();
  EXPECT_STREQ(fake->login_req.BrokerID, "9999");
  EXPECT_STREQ(fake->login_req.UserID, "276491");
  EXPECT_STREQ(fake->login_req.Password, "secret");
  EXPECT_EQ(delegate.events, std::vector<std::string>{"connected"});
}

TEST_F(MdGatewayTest, LoginSuccessAndFailureAreForwarded) {
  Connect();
  LoginOk();
  CThostFtdcRspInfoField info{};
  info.ErrorID = 10;
  // GBK bytes for "错误"
  std::strcpy(info.ErrorMsg, "\xB4\xED\xCE\xF3");
  fake->spi->OnRspUserLogin(nullptr, &info, 2, true);
  EXPECT_EQ(delegate.events, (std::vector<std::string>{
                                 "connected", "login:ok:", "login:fail:错误"}));
}

TEST_F(MdGatewayTest, SubscriptionsAreRestoredAfterRelogin) {
  Connect();
  gateway.Subscribe({"rb2701", "cu2611"});
  ASSERT_EQ(fake->subscribe_calls.size(), 1u);
  LoginOk();
  // The resubscribe round goes through the set: sorted, same contents
  ASSERT_EQ(fake->subscribe_calls.size(), 2u);
  EXPECT_EQ(fake->subscribe_calls[0],
            (std::vector<std::string>{"rb2701", "cu2611"}));
  EXPECT_EQ(fake->subscribe_calls[1],
            (std::vector<std::string>{"cu2611", "rb2701"}));
}

TEST_F(MdGatewayTest, SubscribeDeduplicates) {
  Connect();
  gateway.Subscribe({"rb2701", "rb2701", "cu2611"});
  gateway.Subscribe({"rb2701"});
  ASSERT_EQ(fake->subscribe_calls.size(), 1u);
  EXPECT_EQ(fake->subscribe_calls[0],
            (std::vector<std::string>{"rb2701", "cu2611"}));
}

TEST_F(MdGatewayTest, UnSubscribeOnlyRemovesKnown) {
  Connect();
  gateway.Subscribe({"rb2701", "cu2611"});
  gateway.UnSubscribe({"rb2701", "unknown"});
  ASSERT_EQ(fake->unsubscribe_calls.size(), 1u);
  EXPECT_EQ(fake->unsubscribe_calls[0], std::vector<std::string>{"rb2701"});
  LoginOk();
  // Only cu2611 survives the reconnect
  ASSERT_EQ(fake->subscribe_calls.size(), 2u);
  EXPECT_EQ(fake->subscribe_calls[1], std::vector<std::string>{"cu2611"});
}

TEST_F(MdGatewayTest, TickIsTranslatedAtTheBoundary) {
  Connect();
  CThostFtdcDepthMarketDataField field{};
  std::strcpy(field.InstrumentID, "rb2701");
  std::strcpy(field.ActionDay, "20261013");
  std::strcpy(field.UpdateTime, "21:35:00");
  fake->spi->OnRtnDepthMarketData(&field);
  EXPECT_EQ(delegate.events, std::vector<std::string>{"tick:rb2701"});
}

TEST_F(MdGatewayTest, ErrorsAreForwardedWithUtf8Message) {
  Connect();
  CThostFtdcRspInfoField info{};
  info.ErrorID = 7;
  std::strcpy(info.ErrorMsg, "\xB4\xED\xCE\xF3");  // "错误" in GBK
  fake->spi->OnRspError(&info, 1, true);
  EXPECT_EQ(delegate.events, std::vector<std::string>{"error:7:错误"});
}

TEST_F(MdGatewayTest, DisconnectIsIdempotentAndOrdered) {
  Connect();
  std::size_t const before = calls.size();
  gateway.Disconnect();  // deletes the fake; only `calls` survives
  ASSERT_EQ(calls.size(), before + 2);
  EXPECT_EQ(calls[before], "RegisterSpi");
  EXPECT_EQ(calls[before + 1], "Release");
  gateway.Disconnect();  // no-op
  EXPECT_EQ(calls.size(), before + 2);
}

TEST_F(MdGatewayTest, SubscribeBeforeConnectFails) {
  EXPECT_EQ(gateway.Subscribe({"rb2701"}), -1);
  EXPECT_EQ(gateway.UnSubscribe({"rb2701"}), -1);
}

}  // namespace
}  // namespace cts::md
