/**
 * @file op_log.cc
 * @author lipingan (lipingan.dev@outlook.com)
 * @brief OpLog: append-only binary journal of accepted OMS ops.
 */
#include "oms/op_log.h"

#include <unistd.h>

#include <algorithm>
#include <array>
#include <cstring>

#include <spdlog/spdlog.h>

namespace cts::oms {
namespace {

constexpr std::array<char, 9> kHeader{'C', 'T', 'S', 'O', 'P',
                                      'L', 'O', 'G', 1};  // version byte
constexpr std::uint8_t kOrder = 1;
constexpr std::uint8_t kTrade = 2;
constexpr std::uint8_t kIntent = 3;

#pragma pack(push, 1)
struct IntentRecord {
  char order_ref[13];
  td::OrderRequest req;
};
#pragma pack(pop)
static_assert(std::is_trivially_copyable_v<IntentRecord>);

// Bytes up to which the file parses cleanly (header + whole records)
std::uint64_t ValidPrefix(FILE* f) {
  std::fseek(f, 0, SEEK_END);
  auto const file_size = static_cast<std::uint64_t>(std::ftell(f));
  std::fseek(f, 0, SEEK_SET);
  std::array<char, kHeader.size()> header{};
  if (file_size < kHeader.size() ||
      std::fread(header.data(), 1, header.size(), f) != header.size() ||
      header != kHeader) {
    return 0;  // no valid header: the whole file is unusable
  }
  std::uint64_t offset = kHeader.size();
  while (offset + 5 <= file_size) {
    std::uint8_t rec_head[5];  // kind byte + little-endian u32 length
    std::uint32_t len;
    if (std::fread(rec_head, 1, 5, f) != 5) {
      break;  // torn tail
    }
    std::memcpy(&len, rec_head + 1, sizeof(len));
    if (len == 0 || len > 4096 || offset + 5 + len > file_size) {
      break;  // corrupt or torn record
    }
    offset += 5 + len;
    std::fseek(f, static_cast<long>(offset), SEEK_SET);
  }
  return offset;
}

}  // namespace

OpLog::~OpLog() {
  if (file_ != nullptr) {
    std::fclose(file_);
  }
}

bool OpLog::Open(std::filesystem::path const& path) {
  path_ = path;
  file_ = std::fopen(path.c_str(), "r+b");
  if (file_ == nullptr) {
    file_ = std::fopen(path.c_str(), "w+b");
    if (file_ == nullptr) {
      SPDLOG_WARN("oms: op log open failed: {}", path.string());
      return false;
    }
    std::fwrite(kHeader.data(), 1, kHeader.size(), file_);
    std::fflush(file_);
    size_ = kHeader.size();
    return true;
  }
  std::uint64_t const valid = ValidPrefix(file_);
  if (valid < kHeader.size()) {
    SPDLOG_WARN("oms: op log has no valid header, starting over: {}",
                path.string());
    std::freopen(path.c_str(), "w+b", file_);
    std::fwrite(kHeader.data(), 1, kHeader.size(), file_);
    std::fflush(file_);
    size_ = kHeader.size();
    return true;
  }
  // ftruncate via truncate(): drop a torn tail so appends stay contiguous
  ::truncate(path.c_str(), static_cast<off_t>(valid));
  size_ = valid;
  std::fseek(file_, 0, SEEK_END);
  return true;
}

void OpLog::Append(std::uint8_t kind, void const* payload, std::uint32_t len) {
  if (file_ == nullptr) {
    return;  // logging disabled
  }
  std::fwrite(&kind, 1, 1, file_);
  std::fwrite(&len, sizeof(len), 1, file_);
  std::fwrite(payload, 1, len, file_);
  std::fflush(file_);  // kill -9 must not lose accepted ops to stdio buffers
  size_ += 5 + len;
}

void OpLog::AppendOrder(td::OrderReport const& report) {
  Append(kOrder, &report, sizeof(report));
}

void OpLog::AppendTrade(td::TradeReport const& trade) {
  Append(kTrade, &trade, sizeof(trade));
}

void OpLog::AppendIntent(std::string_view order_ref,
                         td::OrderRequest const& req) {
  IntentRecord rec{};
  std::memcpy(rec.order_ref, order_ref.data(),
              std::min(order_ref.size(), sizeof(rec.order_ref) - 1));
  rec.req = req;
  Append(kIntent, &rec, sizeof(rec));
}

void OpLog::Reset() {
  if (file_ == nullptr) {
    return;
  }
  std::freopen(path_.c_str(), "w+b", file_);
  std::fwrite(kHeader.data(), 1, kHeader.size(), file_);
  std::fflush(file_);
  size_ = kHeader.size();
}

bool OpLog::ForEach(
    std::function<void(std::uint64_t, td::OrderReport const&)> on_order,
    std::function<void(std::uint64_t, td::TradeReport const&)> on_trade,
    std::function<void(std::uint64_t, std::string_view, td::OrderRequest const&)>
        on_intent) const {
  FILE* f = std::fopen(path_.c_str(), "rb");
  if (f == nullptr) {
    return false;
  }
  std::uint64_t const valid = ValidPrefix(f);
  std::fseek(f, static_cast<long>(kHeader.size()), SEEK_SET);
  std::uint64_t offset = kHeader.size();
  bool ok = true;
  while (offset < valid) {
    std::uint8_t kind;
    std::uint32_t len;
    if (std::fread(&kind, 1, 1, f) != 1 ||
        std::fread(&len, sizeof(len), 1, f) != 1) {
      ok = false;
      break;
    }
    std::uint64_t const record_offset = offset;
    offset += 5 + len;
    if (kind == kOrder && len == sizeof(td::OrderReport)) {
      td::OrderReport r;
      ok &= std::fread(&r, sizeof(r), 1, f) == 1;
      on_order(record_offset, r);
    } else if (kind == kTrade && len == sizeof(td::TradeReport)) {
      td::TradeReport t;
      ok &= std::fread(&t, sizeof(t), 1, f) == 1;
      on_trade(record_offset, t);
    } else if (kind == kIntent && len == sizeof(IntentRecord)) {
      IntentRecord rec;
      ok &= std::fread(&rec, sizeof(rec), 1, f) == 1;
      on_intent(record_offset, rec.order_ref, rec.req);
    } else {
      ok = false;  // unknown kind: never skip-guess inside the valid prefix
      break;
    }
    if (!ok) {
      break;
    }
  }
  std::fclose(f);
  return ok;
}

}  // namespace cts::oms
