/**
 * @file tick_ctp.h
 * @author lipingan (lipingan.dev@outlook.com)
 * @brief Converter from the CTP depth market data struct to md::Tick.
 */
#pragma once

#ifndef CTS_MD_DETAIL_TICK_CTP_H_
#define CTS_MD_DETAIL_TICK_CTP_H_

// Boundary header: only the md module's own .cc files (and its tests) may
// include this; nothing else in the system sees CTP types.

#include "ThostFtdcUserApiStruct.h"
#include "md/tick.h"

namespace cts::md::detail {

// Pure translation, never fails: malformed source timestamps fold to
// epoch_ms == 0, overlong strings are truncated
Tick TickFromCtp(CThostFtdcDepthMarketDataField const& field);

}  // namespace cts::md::detail

#endif  // CTS_MD_DETAIL_TICK_CTP_H_
