/**
 * @file order_ctp.h
 * @author lipingan (lipingan.dev@outlook.com)
 * @brief Boundary translation between td types and CTP trader structs.
 */
#pragma once

#ifndef CTS_TD_DETAIL_ORDER_CTP_H_
#define CTS_TD_DETAIL_ORDER_CTP_H_

// Boundary header: td internal + tests only; exposes CTP types by design.

#include <string_view>

#include "ThostFtdcTraderApi.h"
#include "td/order.h"

namespace cts::td::detail {

// Request -> vendor struct: limit price, GFD, speculation, immediate.
// order_ref is the zero-padded decimal reference allocated by the adapter
CThostFtdcInputOrderField InputOrderToCtp(OrderRequest const& req,
                                          std::string_view broker_id,
                                          std::string_view investor_id,
                                          std::string_view order_ref);

// Vendor reports -> normalized
OrderReport OrderReportFromCtp(CThostFtdcOrderField const& f);
// Insert rejections come back as the request echo (OnRspOrderInsert /
// OnErrRtnOrderInsert), not as an OrderField
OrderReport RejectedOrderFromCtp(CThostFtdcInputOrderField const& f);
TradeReport TradeReportFromCtp(CThostFtdcTradeField const& f);

// One CTP row carries one bucket only (PositionDate); false = skipped (Net)
bool PositionRowFromCtp(CThostFtdcInvestorPositionField const& f,
                        PositionRow& row);
AccountReport AccountReportFromCtp(CThostFtdcTradingAccountField const& f);

}  // namespace cts::td::detail

#endif  // CTS_TD_DETAIL_ORDER_CTP_H_
