/**
 * @file raw_tick_sink.h
 * @author lipingan (lipingan.dev@outlook.com)
 * @brief Raw tick tap at the gateway boundary (pre-translation).
 */
#pragma once

#ifndef CTS_MD_RAW_TICK_SINK_H_
#define CTS_MD_RAW_TICK_SINK_H_

// Boundary interface: exposes the verbatim CTP wire struct by design (same
// tradeoff as instrument/instrument_ctp.h). Only recorder-style consumers
// implement this; everything else eats the normalized md::Tick.

#include "ThostFtdcUserApiStruct.h"

namespace cts::md {

class RawTickSink {
 public:
  virtual ~RawTickSink() = default;

  // Fired on the CTP callback thread with the verbatim wire struct, BEFORE
  // normalization. Must be fast: box-and-enqueue only
  virtual void OnRawTick(CThostFtdcDepthMarketDataField const& tick) = 0;
};

}  // namespace cts::md

#endif  // CTS_MD_RAW_TICK_SINK_H_
