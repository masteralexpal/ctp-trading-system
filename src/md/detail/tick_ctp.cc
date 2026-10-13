/**
 * @file tick_ctp.cc
 * @author lipingan (lipingan.dev@outlook.com)
 * @brief Converter from the CTP depth market data struct to md::Tick.
 */
#include "md/detail/tick_ctp.h"

#include <cstring>
#include <string_view>

#include "base/string_utils.h"
#include "base/time_utils.h"

namespace cts::md::detail {
namespace {

// Howard Hinnant's days_from_civil: proleptic Gregorian days since 1970-01-01
std::int64_t DaysFromCivil(std::int64_t year, unsigned month, unsigned day) {
  year -= month <= 2 ? 1 : 0;
  std::int64_t const era = (year >= 0 ? year : year - 399) / 400;
  unsigned const yoe = static_cast<unsigned>(year - era * 400);
  unsigned const doy = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
  unsigned const doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + static_cast<std::int64_t>(doe) - 719468;
}

int Parse2(char a, char b) {
  if (a < '0' || a > '9' || b < '0' || b > '9') {
    return -1;
  }
  return (a - '0') * 10 + (b - '0');
}

// Folds "YYYYMMDD" + "HH:MM:SS" + millisec into Unix epoch milliseconds.
// CTP timestamps are China local time; Asia/Shanghai is a fixed UTC+8 (no
// DST). Returns 0 on malformed input.
std::int64_t ToEpochMs(std::string_view day,
                       std::string_view time,
                       int millisec) {
  if (day.size() < 8 || time.size() < 8) {
    return 0;
  }
  int const year_hi = Parse2(day[0], day[1]);
  int const year_lo = Parse2(day[2], day[3]);
  int const month = Parse2(day[4], day[5]);
  int const dom = Parse2(day[6], day[7]);
  int const hour = Parse2(time[0], time[1]);
  int const minute = Parse2(time[3], time[4]);  // skip ':'
  int const second = Parse2(time[6], time[7]);
  if (year_hi < 0 || year_lo < 0 || month < 1 || month > 12 || dom < 1 ||
      dom > 31 || hour < 0 || hour > 23 || minute < 0 || minute > 59 ||
      second < 0 || second > 59 || millisec < 0 || millisec > 999) {
    return 0;
  }
  std::int64_t const days =
      DaysFromCivil(year_hi * 100 + year_lo, static_cast<unsigned>(month),
                    static_cast<unsigned>(dom));
  return (days * 86400 + hour * 3600 + minute * 60 + second -
          base::kUtcPlus8Seconds) *
             1000 +
         millisec;
}

}  // namespace

Tick TickFromCtp(CThostFtdcDepthMarketDataField const& f) {
  Tick tick{};
  base::CopyToFixedArray(tick.instrument_id, f.InstrumentID);
  base::CopyToFixedArray(tick.exchange_id, f.ExchangeID);
  base::CopyToFixedArray(tick.trading_day, f.TradingDay);
  // ActionDay is the actual calendar date; TradingDay belongs to the next
  // trading day during the night session
  tick.epoch_ms = ToEpochMs(f.ActionDay, f.UpdateTime, f.UpdateMillisec);

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
