/**
 * @file console_td_client.cc
 * @author lipingan (lipingan.dev@outlook.com)
 * @brief Debug client: forwards trading events to the log.
 */
#include "td/console_td_client.h"

#include <spdlog/spdlog.h>

namespace cts::td {
namespace {

char const* DirectionName(Direction d) {
  return d == Direction::kBuy ? "buy" : "sell";
}

char const* OffsetName(Offset o) {
  switch (o) {
    case Offset::kOpen:
      return "open";
    case Offset::kClose:
      return "close";
    case Offset::kCloseToday:
      return "close-today";
    case Offset::kCloseYesterday:
      return "close-yesterday";
  }
  return "?";
}

char const* StatusName(OrderStatus s) {
  switch (s) {
    case OrderStatus::kSubmitted:
      return "submitted";
    case OrderStatus::kPartTraded:
      return "part-traded";
    case OrderStatus::kAllTraded:
      return "all-traded";
    case OrderStatus::kCanceled:
      return "canceled";
    case OrderStatus::kRejected:
      return "rejected";
    case OrderStatus::kUnknown:
      return "unknown";
  }
  return "?";
}

}  // namespace

void ConsoleTdClient::OnTdConnected() {
  SPDLOG_INFO("td: connected");
}

void ConsoleTdClient::OnTdDisconnected(int reason) {
  SPDLOG_WARN("td: disconnected reason={}", reason);
}

void ConsoleTdClient::OnTdReady(bool success, std::string_view msg) {
  if (success) {
    SPDLOG_INFO("td: ready (auth + login + settlement confirmed)");
  } else {
    SPDLOG_ERROR("td: startup chain failed: {}", msg);
  }
}

void ConsoleTdClient::OnTdOrder(OrderReport const& order) {
  SPDLOG_INFO("order: {} {} {} {} @{} ref={} traded={}/{} status={}",
              order.instrument_id, DirectionName(order.direction),
              OffsetName(order.offset), order.volume_total_original,
              order.limit_price, order.order_ref, order.volume_traded,
              order.volume_total_original, StatusName(order.status));
}

void ConsoleTdClient::OnTdTrade(TradeReport const& trade) {
  SPDLOG_INFO("trade: {} {} {} {} @{} ref={} id={}", trade.instrument_id,
              DirectionName(trade.direction), OffsetName(trade.offset),
              trade.volume, trade.price, trade.order_ref, trade.trade_id);
}

void ConsoleTdClient::OnTdPositions(
    std::shared_ptr<std::vector<PositionRow> const> rows) {
  for (auto const& row : *rows) {
    SPDLOG_INFO("position: {} {} today={} yd={}", row.instrument_id,
                DirectionName(row.direction), row.today_volume,
                row.yd_volume);
  }
  SPDLOG_INFO("positions snapshot: {} rows", rows->size());
}

void ConsoleTdClient::OnTdError(int error_id, std::string_view msg) {
  SPDLOG_ERROR("td: error {} {}", error_id, msg);
}

}  // namespace cts::td
