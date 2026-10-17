/**
 * @file instrument_ctp.h
 * @author lipingan (lipingan.dev@outlook.com)
 * @brief Converter from CTP instrument struct to domain Instrument.
 */
#pragma once

#ifndef CTS_INSTRUMENT_INSTRUMENT_CTP_H_
#define CTS_INSTRUMENT_INSTRUMENT_CTP_H_

// Converter from CTP structs to domain Instrument.
// Only CTP-linking modules (td gateway, probe tools) should include this
// header; the instrument module itself stays CTP-free.

#include "ThostFtdcUserApiStruct.h"
#include "instrument/instrument.h"

namespace cts::instrument {

inline Instrument InstrumentFromCtp(CThostFtdcInstrumentField const& f) {
  Instrument inst;
  inst.instrument_id = f.InstrumentID;
  inst.product_id = f.ProductID;
  inst.exchange_id = f.ExchangeID;
  inst.open_date = f.OpenDate;
  inst.expire_date = f.ExpireDate;
  inst.price_tick = f.PriceTick;
  inst.volume_multiple = f.VolumeMultiple;
  inst.max_limit_order_volume = f.MaxLimitOrderVolume;
  switch (f.ProductClass) {
    case THOST_FTDC_PC_Futures:
      inst.product_class = ProductClass::kFutures;
      break;
    case THOST_FTDC_PC_Options:
      inst.product_class = ProductClass::kOption;
      break;
    default:
      inst.product_class = ProductClass::kOther;
      break;
  }
  return inst;
}

}  // namespace cts::instrument

#endif  // CTS_INSTRUMENT_INSTRUMENT_CTP_H_
