/**
 * @file tick_ctp.cc
 * @author lipingan (lipingan.dev@outlook.com)
 * @brief Converter from the CTP depth market data struct to md::Tick.
 */
#include "md/detail/tick_ctp.h"

#include <cfloat>
#include <cstring>

#include "base/string_utils.h"
#include "base/time_utils.h"

namespace cts::md::detail {
namespace {

// CTP pads invalid doubles with DBL_MAX; fold those to 0 so the poison never
// leaves the boundary (raw archives keep the original struct untouched)
double Sanitize(double v) {
  return v >= DBL_MAX ? 0.0 : v;
}

}  // namespace

Tick TickFromCtp(CThostFtdcDepthMarketDataField const& f) {
  Tick tick{};
  base::CopyToFixedArray(tick.instrument_id, f.InstrumentID);
  base::CopyToFixedArray(tick.exchange_id, f.ExchangeID);
  base::CopyToFixedArray(tick.trading_day, f.TradingDay);
  // ActionDay is the actual calendar date; TradingDay belongs to the next
  // trading day during the night session
  tick.epoch_ms =
      base::LocalDateTimeToEpochMs(f.ActionDay, f.UpdateTime, f.UpdateMillisec);

  tick.last_price = Sanitize(f.LastPrice);
  tick.open_price = Sanitize(f.OpenPrice);
  tick.highest_price = Sanitize(f.HighestPrice);
  tick.lowest_price = Sanitize(f.LowestPrice);
  tick.pre_close_price = Sanitize(f.PreClosePrice);
  tick.volume = f.Volume;
  tick.turnover = Sanitize(f.Turnover);
  tick.open_interest = f.OpenInterest;

  double const bid_prices[] = {Sanitize(f.BidPrice1), Sanitize(f.BidPrice2),
                               Sanitize(f.BidPrice3), Sanitize(f.BidPrice4),
                               Sanitize(f.BidPrice5)};
  double const ask_prices[] = {Sanitize(f.AskPrice1), Sanitize(f.AskPrice2),
                               Sanitize(f.AskPrice3), Sanitize(f.AskPrice4),
                               Sanitize(f.AskPrice5)};
  std::int32_t const bid_volumes[] = {f.BidVolume1, f.BidVolume2, f.BidVolume3,
                                      f.BidVolume4, f.BidVolume5};
  std::int32_t const ask_volumes[] = {f.AskVolume1, f.AskVolume2, f.AskVolume3,
                                      f.AskVolume4, f.AskVolume5};
  std::memcpy(tick.bid_price, bid_prices, sizeof(bid_prices));
  std::memcpy(tick.ask_price, ask_prices, sizeof(ask_prices));
  std::memcpy(tick.bid_volume, bid_volumes, sizeof(bid_volumes));
  std::memcpy(tick.ask_volume, ask_volumes, sizeof(ask_volumes));

  tick.upper_limit_price = Sanitize(f.UpperLimitPrice);
  tick.lower_limit_price = Sanitize(f.LowerLimitPrice);
  tick.pre_settlement_price = Sanitize(f.PreSettlementPrice);
  tick.settlement_price = Sanitize(f.SettlementPrice);
  return tick;
}

}  // namespace cts::md::detail
