/**
 * @file dump_main.cc
 * @author lipingan (lipingan.dev@outlook.com)
 * @brief cts_dump: raw tick journal -> CSV.
 */

// First step of the ETL path: one CSV row per record, values straight off the
// wire struct (translation stays out -- a translator bug must never poison
// analytics either). Columns: provenance (seq, arrival_ms) first, then the
// timestamp identity (trading_day/action_day/update_time), then the book.

#include <cstdio>
#include <string>

#include "recorder/raw_tick_file.h"

#include <CLI/CLI.hpp>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

namespace {

void WriteHeader(FILE* out) {
  std::fputs("seq,arrival_ms,trading_day,action_day,instrument,exchange,"
             "update_time,update_ms,last_price,bid1,bid_vol1,ask1,ask_vol1,"
             "volume,turnover,open_interest\n",
             out);
}

void WriteRow(FILE* out, cts::recorder::RawTickRecord const& r) {
  auto const& t = r.tick;
  std::fprintf(out, "%llu,%lld,%s,%s,%s,%s,%s,%d,%.4f,%.4f,%d,%.4f,%d,%d,%.2f,"
                    "%.1f\n",
               static_cast<unsigned long long>(r.seq),
               static_cast<long long>(r.arrival_ms), t.TradingDay,
               t.ActionDay, t.InstrumentID, t.ExchangeID, t.UpdateTime,
               t.UpdateMillisec, t.LastPrice, t.BidPrice1, t.BidVolume1,
               t.AskPrice1, t.AskVolume1, t.Volume, t.Turnover,
               t.OpenInterest);
}

}  // namespace

int main(int argc, char** argv) {
  std::string file;
  std::string out_path;  // empty = stdout

  CLI::App app{"cts_dump: dump a CTSRAWT raw tick journal as CSV"};
  app.add_option("--file", file, "raw tick journal (ticks_YYYYMMDD.raw)")
      ->required();
  app.add_option("--out", out_path, "output CSV path (default: stdout)");

  try {
    app.parse(argc, argv);
  } catch (CLI::ParseError const& e) {
    return app.exit(e);
  }

  spdlog::set_default_logger(spdlog::stdout_color_mt("cts_dump"));

  cts::recorder::RawTickFileReader reader;
  if (!reader.Open(file)) {
    SPDLOG_ERROR("failed to open {}", file);
    return 1;
  }

  FILE* out = stdout;
  if (!out_path.empty()) {
    out = std::fopen(out_path.c_str(), "w");
    if (out == nullptr) {
      SPDLOG_ERROR("failed to open {} for writing", out_path);
      return 1;
    }
  }

  WriteHeader(out);
  cts::recorder::RawTickRecord record;
  while (reader.Next(record)) {
    WriteRow(out, record);
  }

  if (out != stdout) {
    std::fclose(out);
  }
  SPDLOG_INFO("dumped {} records (source={})", reader.read_count(),
              reader.source());
  return 0;
}
