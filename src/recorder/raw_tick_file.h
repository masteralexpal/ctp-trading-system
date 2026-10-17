/**
 * @file raw_tick_file.h
 * @author lipingan (lipingan.dev@outlook.com)
 * @brief Raw tick journal: header + append-only RawTickRecord array.
 */
#pragma once

#ifndef CTS_RECORDER_RAW_TICK_FILE_H_
#define CTS_RECORDER_RAW_TICK_FILE_H_

// Boundary header: the record embeds the verbatim CTP wire struct, so
// including this header exposes the CTP PODs (same tradeoff as
// instrument/instrument_ctp.h). Only the recorder app and replay/ETL tools
// should include it.

#include <cstdint>
#include <filesystem>
#include <string_view>
#include <type_traits>

#include "ThostFtdcUserApiStruct.h"

namespace cts::recorder {

// On-disk journal of VERBATIM wire structs (little-endian x86-64 only): the
// translation to md::Tick happens at read/ETL time, so a translator bug never
// poisons the archive. No fsync -- a crash may tear the tail; readers treat a
// short trailing record as clean EOF, writers repair it on open.
//
// seq is assigned by the producer per process run (starts at 1, monotonic
// across same-day rotations): a gap in seq is a dropped record, a DECREASE
// marks a process restart boundary (same-day restart appends to the file).
inline constexpr char kRawTickFileMagic[8] = {'C', 'T', 'S', 'R',
                                              'A', 'W', 'T', '\0'};
inline constexpr std::uint32_t kRawTickFileVersion = 1;

struct RawTickFileHeader {
  char magic[8];
  std::uint32_t version;
  std::uint32_t record_size;      // sizeof(RawTickRecord) at write time
  std::int64_t created_epoch_ms;  // wall clock at creation; 0 when appending
  char source[16];                // feed tag, e.g. "simnow-724" / "prod"
};

struct RawTickRecord {
  std::uint64_t seq;        // producer-side sequence; gap = dropped record
  std::int64_t arrival_ms;  // local receive time (system_clock)
  CThostFtdcDepthMarketDataField tick;  // verbatim off the wire
};

static_assert(std::is_trivially_copyable_v<RawTickFileHeader>);
static_assert(std::is_trivially_copyable_v<RawTickRecord>);

// Append-only writer. A same-day restart appends to the existing file after
// validating its header (and repairing a torn tail). Not thread-safe; errors
// by return value (errno preserved).
class RawTickFileWriter {
 public:
  RawTickFileWriter() = default;
  ~RawTickFileWriter() { Close(); }

  RawTickFileWriter(RawTickFileWriter const&) = delete;
  RawTickFileWriter& operator=(RawTickFileWriter const&) = delete;

  // Creates parent dirs; an empty file gets a header (with source), an
  // existing one is validated and repaired. false on IO or format error
  bool Open(std::filesystem::path const& path, std::string_view source);
  bool Write(RawTickRecord const& record);
  void Close();  // idempotent

  bool is_open() const { return fd_ >= 0; }
  // seq of the last record found in the file at Open (0 when empty)
  std::uint64_t last_seq() const { return last_seq_; }
  std::uint64_t written() const { return written_; }

 private:
  int fd_{-1};
  std::uint64_t last_seq_{0};
  std::uint64_t written_{0};
};

class RawTickFileReader {
 public:
  RawTickFileReader() = default;
  ~RawTickFileReader() { Close(); }

  RawTickFileReader(RawTickFileReader const&) = delete;
  RawTickFileReader& operator=(RawTickFileReader const&) = delete;

  bool Open(std::filesystem::path const& path);  // validates the header
  bool Next(RawTickRecord& out);                 // false at EOF or torn tail
  void Close();

  bool is_open() const { return fd_ >= 0; }
  std::string_view source() const { return source_; }
  std::uint64_t read_count() const { return read_count_; }

 private:
  int fd_{-1};
  char source_[16]{};
  std::uint64_t read_count_{0};
};

}  // namespace cts::recorder

#endif  // CTS_RECORDER_RAW_TICK_FILE_H_
