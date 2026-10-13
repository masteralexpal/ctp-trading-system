/**
 * @file instrument_master.cc
 * @author lipingan (lipingan.dev@outlook.com)
 * @brief Instrument master: authoritative whole-market snapshot, built per
 * trading day, read-only afterwards.
 */
#include "instrument/instrument_master.h"

#include <utility>

namespace cts::instrument {

InstrumentMaster InstrumentMaster::Build(std::vector<Instrument> instruments) {
  InstrumentMaster master;
  master.by_id_.reserve(instruments.size());
  for (auto& inst : instruments) {
    master.by_id_.emplace(inst.instrument_id, std::move(inst));
  }
  for (auto& [id, inst] : master.by_id_) {
    master.by_product_[inst.product_id].push_back(&inst);
  }
  return master;
}

Instrument const* InstrumentMaster::Find(std::string_view instrument_id) const {
  auto const it = by_id_.find(instrument_id);
  return it == by_id_.end() ? nullptr : &it->second;
}

std::vector<Instrument const*> InstrumentMaster::ByProduct(
    std::string_view product_id) const {
  auto const it = by_product_.find(product_id);
  return it == by_product_.end() ? std::vector<Instrument const*>{}
                                 : it->second;
}

}  // namespace cts::instrument
