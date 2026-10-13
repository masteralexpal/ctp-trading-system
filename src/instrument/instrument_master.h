/**
 * @file instrument_master.h
 * @author lipingan (lipingan.dev@outlook.com)
 * @brief Instrument master: authoritative whole-market snapshot, built per
 * trading day, read-only afterwards.
 */
#pragma once

#ifndef CTS_INSTRUMENT_INSTRUMENT_MASTER_H_
#define CTS_INSTRUMENT_INSTRUMENT_MASTER_H_

#include <cstddef>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "instrument/instrument.h"

namespace cts::instrument {

// Instrument master: the authoritative whole-market snapshot.
// Lifetime = one trading day: built once from the td-channel query, read-only
// (lock-free) afterwards, wholesale-replaced next day. No per-item registry, no
// persistence.
class InstrumentMaster {
 public:
  // Build wholesale from domain objects (CTP conversion lives in
  // instrument_ctp.h)
  static InstrumentMaster Build(std::vector<Instrument> instruments);

  // nullptr when not found
  Instrument const* Find(std::string_view instrument_id) const;
  // Filter by product ("rb" -> rb2611/rb2701/..., options included)
  std::vector<Instrument const*> ByProduct(std::string_view product_id) const;

  std::size_t size() const noexcept { return by_id_.size(); }

 private:
  // Transparent hash: string_view lookup without a temporary string
  struct StringHash {
    using is_transparent = void;
    std::size_t operator()(std::string_view sv) const noexcept {
      return std::hash<std::string_view>{}(sv);
    }
  };

  // unordered_map is node-based: addresses are stable, so by_product_ may hold
  // pointers
  std::unordered_map<std::string, Instrument, StringHash, std::equal_to<>>
      by_id_;
  std::unordered_map<std::string,
                     std::vector<Instrument const*>,
                     StringHash,
                     std::equal_to<>>
      by_product_;
};

}  // namespace cts::instrument

#endif  // CTS_INSTRUMENT_INSTRUMENT_MASTER_H_
