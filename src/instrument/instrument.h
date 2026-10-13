/**
 * @file instrument.h
 * @author lipingan (lipingan.dev@outlook.com)
 * @brief Domain representation of an instrument (a curated subset of CTP
 * fields).
 */
#pragma once

#ifndef CTS_INSTRUMENT_INSTRUMENT_H_
#define CTS_INSTRUMENT_INSTRUMENT_H_

#include <string>

namespace cts::instrument {

enum class ProductClass {
  kFutures,
  kOption,
  kOther,
};

// Domain representation of one instrument (curated subset of
// CThostFtdcInstrumentField). Fields are admitted on demand; option fields
// (strike/side/underlying) come with options support.
struct Instrument {
  std::string instrument_id;  // rb2701
  std::string product_id;     // rb
  std::string exchange_id;    // SHFE
  ProductClass product_class{ProductClass::kFutures};
  std::string open_date;    // listing date
  std::string expire_date;  // expiry date (input to the roll engine)
  double price_tick{0.0};   // tick size (mandatory for order price rounding)
  int volume_multiple{0};   // volume multiple (mandatory for PnL)
};

}  // namespace cts::instrument

#endif  // CTS_INSTRUMENT_INSTRUMENT_H_
