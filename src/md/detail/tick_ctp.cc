/**
 * @file tick_ctp.cc
 * @author lipingan (lipingan.dev@outlook.com)
 * @brief Converter from the CTP depth market data struct to md::Tick.
 */
#include "md/detail/tick_ctp.h"

#include <cstring>

#include "base/string_utils.h"
#include "base/time_utils.h"

namespace cts::md::detail {

Tick TickFromCtp(CThostFtdcDepthMarketDataField const& f) {
  Tick tick{};
  base::CopyToFixedArray(tick.instrument_id, f.InstrumentID);
  base::CopyToFixedArray(tick.exchange_id, f.ExchangeID);
  base::CopyToFixedArray(tick.trading_day, f.TradingDay);
  // ActionDay is the actual calendar date; TradingDay belongs to the next
  // trading day during the night session
  tick.epoch_ms =
      base::LocalDateTimeToEpochMs(f.ActionDay, f.UpdateTime, f.UpdateMillisec);

  tick.last_price = f.LastPrice;
  tick.volume = f.Volume;
  tick.turnover = f.Turnover;
  tick.open_interest = f.OpenInterest;

  double const bid_prices[] = {f.BidPrice1, f.BidPrice2, f.BidPrice3,
                               f.BidPrice4, f.BidPrice5};
  double const ask_prices[] = {f.AskPrice1, f.AskPrice2, f.AskPrice3,
                               f.AskPrice4, f.AskPrice5};
  std::int32_t const bid_volumes[] = {f.BidVolume1, f.BidVolume2, f.BidVolume3,
                                      f.BidVolume4, f.BidVolume5};
  std::int32_t const ask_volumes[] = {f.AskVolume1, f.AskVolume2, f.AskVolume3,
                                      f.AskVolume4, f.AskVolume5};
  std::memcpy(tick.bid_price, bid_prices, sizeof(bid_prices));
  std::memcpy(tick.ask_price, ask_prices, sizeof(ask_prices));
  std::memcpy(tick.bid_volume, bid_volumes, sizeof(bid_volumes));
  std::memcpy(tick.ask_volume, ask_volumes, sizeof(ask_volumes));

  tick.upper_limit_price = f.UpperLimitPrice;
  tick.lower_limit_price = f.LowerLimitPrice;
  tick.pre_settlement_price = f.PreSettlementPrice;
  tick.settlement_price = f.SettlementPrice;
  return tick;
}

}  // namespace cts::md::detail
