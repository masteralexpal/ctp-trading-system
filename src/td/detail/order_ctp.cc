/**
 * @file order_ctp.cc
 * @author lipingan (lipingan.dev@outlook.com)
 * @brief Boundary translation between td types and CTP trader structs.
 */
#include "td/detail/order_ctp.h"

#include "base/string_utils.h"
#include "base/time_utils.h"

namespace cts::td::detail {
namespace {

TThostFtdcDirectionType DirectionToCtp(Direction d) {
  return d == Direction::kBuy ? THOST_FTDC_D_Buy : THOST_FTDC_D_Sell;
}

Direction DirectionFromCtp(TThostFtdcDirectionType d) {
  return d == THOST_FTDC_D_Buy ? Direction::kBuy : Direction::kSell;
}

TThostFtdcOffsetFlagType OffsetToCtp(Offset o) {
  switch (o) {
    case Offset::kOpen:
      return THOST_FTDC_OF_Open;
    case Offset::kClose:
      return THOST_FTDC_OF_Close;
    case Offset::kCloseToday:
      return THOST_FTDC_OF_CloseToday;
    case Offset::kCloseYesterday:
      return THOST_FTDC_OF_CloseYesterday;
  }
  return THOST_FTDC_OF_Open;
}

Offset OffsetFromCtp(TThostFtdcOffsetFlagType o) {
  switch (o) {
    case THOST_FTDC_OF_Close:
      return Offset::kClose;
    case THOST_FTDC_OF_CloseToday:
      return Offset::kCloseToday;
    case THOST_FTDC_OF_CloseYesterday:
      return Offset::kCloseYesterday;
    default:
      return Offset::kOpen;
  }
}

OrderStatus StatusFromCtp(TThostFtdcOrderStatusType s) {
  switch (s) {
    case THOST_FTDC_OST_AllTraded:
      return OrderStatus::kAllTraded;
    case THOST_FTDC_OST_PartTradedQueueing:
      return OrderStatus::kPartTraded;
    case THOST_FTDC_OST_Canceled:
      return OrderStatus::kCanceled;
    case THOST_FTDC_OST_NoTradeQueueing:
    case THOST_FTDC_OST_PartTradedNotQueueing:
    case THOST_FTDC_OST_NoTradeNotQueueing:
      return OrderStatus::kSubmitted;
    default:
      return OrderStatus::kUnknown;
  }
}

}  // namespace

CThostFtdcInputOrderField InputOrderToCtp(OrderRequest const& req,
                                          std::string_view broker_id,
                                          std::string_view investor_id,
                                          std::string_view order_ref) {
  CThostFtdcInputOrderField f{};
  base::CopyToFixedArray(f.BrokerID, std::string(broker_id));
  base::CopyToFixedArray(f.InvestorID, std::string(investor_id));
  base::CopyToFixedArray(f.UserID, std::string(investor_id));  // CTP: same
  base::CopyToFixedArray(f.OrderRef, std::string(order_ref));
  base::CopyToFixedArray(f.InstrumentID, req.instrument_id);
  base::CopyToFixedArray(f.ExchangeID, req.exchange_id);
  f.OrderPriceType = THOST_FTDC_OPT_LimitPrice;
  f.Direction = DirectionToCtp(req.direction);
  f.CombOffsetFlag[0] = OffsetToCtp(req.offset);
  f.CombHedgeFlag[0] = THOST_FTDC_HF_Speculation;
  f.LimitPrice = req.limit_price;
  f.VolumeTotalOriginal = req.volume;
  f.TimeCondition = THOST_FTDC_TC_GFD;
  f.VolumeCondition = THOST_FTDC_VC_AV;
  f.MinVolume = 1;
  f.ContingentCondition = THOST_FTDC_CC_Immediately;
  f.ForceCloseReason = THOST_FTDC_FCC_NotForceClose;
  f.IsAutoSuspend = 0;
  f.UserForceClose = 0;
  return f;
}

OrderReport OrderReportFromCtp(CThostFtdcOrderField const& f) {
  OrderReport r{};
  base::CopyToFixedArray(r.instrument_id, f.InstrumentID);
  base::CopyToFixedArray(r.exchange_id, f.ExchangeID);
  base::CopyToFixedArray(r.order_ref, f.OrderRef);
  base::CopyToFixedArray(r.order_sys_id, f.OrderSysID);
  r.direction = DirectionFromCtp(f.Direction);
  r.offset = OffsetFromCtp(f.CombOffsetFlag[0]);
  r.limit_price = f.LimitPrice;
  r.volume_total_original = f.VolumeTotalOriginal;
  r.volume_traded = f.VolumeTraded;
  r.volume_total = f.VolumeTotal;
  r.status = StatusFromCtp(f.OrderStatus);
  r.front_id = f.FrontID;
  r.session_id = f.SessionID;
  return r;
}

OrderReport RejectedOrderFromCtp(CThostFtdcInputOrderField const& f) {
  OrderReport r{};
  base::CopyToFixedArray(r.instrument_id, f.InstrumentID);
  base::CopyToFixedArray(r.exchange_id, f.ExchangeID);
  base::CopyToFixedArray(r.order_ref, f.OrderRef);
  r.direction = DirectionFromCtp(f.Direction);
  r.offset = OffsetFromCtp(f.CombOffsetFlag[0]);
  r.limit_price = f.LimitPrice;
  r.volume_total_original = f.VolumeTotalOriginal;
  r.volume_traded = 0;
  r.volume_total = f.VolumeTotalOriginal;
  r.status = OrderStatus::kRejected;
  // front/session are unknown for a rejected insert; the cancel key is moot
  return r;
}

TradeReport TradeReportFromCtp(CThostFtdcTradeField const& f) {
  TradeReport r{};
  base::CopyToFixedArray(r.instrument_id, f.InstrumentID);
  base::CopyToFixedArray(r.exchange_id, f.ExchangeID);
  base::CopyToFixedArray(r.order_ref, f.OrderRef);
  base::CopyToFixedArray(r.trade_id, f.TradeID);
  r.direction = DirectionFromCtp(f.Direction);
  r.offset = OffsetFromCtp(f.OffsetFlag);
  r.price = f.Price;
  r.volume = f.Volume;
  r.epoch_ms = base::LocalDateTimeToEpochMs(f.TradeDate, f.TradeTime, 0);
  return r;
}

}  // namespace cts::td::detail
