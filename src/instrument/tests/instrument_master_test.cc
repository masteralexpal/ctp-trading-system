/**
 * @file instrument_master_test.cc
 * @author lipingan (lipingan.dev@outlook.com)
 * @brief InstrumentMaster tests.
 */
#include "instrument/instrument_master.h"

#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace cts::instrument {
namespace {

// Sample modeled on the 2026-10-12 SimNow live query result
std::vector<Instrument> Fixture() {
  return {
      {.instrument_id = "rb2611",
       .product_id = "rb",
       .exchange_id = "SHFE",
       .product_class = ProductClass::kFutures,
       .open_date = "20251117",
       .expire_date = "20261116",
       .price_tick = 1.0,
       .volume_multiple = 10},
      {.instrument_id = "rb2701",
       .product_id = "rb",
       .exchange_id = "SHFE",
       .product_class = ProductClass::kFutures,
       .open_date = "20260116",
       .expire_date = "20270115",
       .price_tick = 1.0,
       .volume_multiple = 10},
      {.instrument_id = "rb2611C2750",
       .product_id = "rb",
       .exchange_id = "SHFE",
       .product_class = ProductClass::kOption,
       .open_date = "20251013",
       .expire_date = "20261026",
       .price_tick = 0.5,
       .volume_multiple = 10},
      {.instrument_id = "cu2611",
       .product_id = "cu",
       .exchange_id = "SHFE",
       .product_class = ProductClass::kFutures,
       .open_date = "20251117",
       .expire_date = "20261116",
       .price_tick = 10.0,
       .volume_multiple = 5},
  };
}

TEST(InstrumentMasterTest, FindHit) {
  auto const master = InstrumentMaster::Build(Fixture());
  auto const* inst = master.Find("rb2701");
  ASSERT_NE(inst, nullptr);
  EXPECT_EQ(inst->product_id, "rb");
  EXPECT_EQ(inst->expire_date, "20270115");
  EXPECT_DOUBLE_EQ(inst->price_tick, 1.0);
  EXPECT_EQ(inst->volume_multiple, 10);
  EXPECT_EQ(inst->product_class, ProductClass::kFutures);
}

TEST(InstrumentMasterTest, FindMiss) {
  auto const master = InstrumentMaster::Build(Fixture());
  EXPECT_EQ(master.Find("rb2501"), nullptr);  // delisted
  EXPECT_EQ(master.Find(""), nullptr);
}

TEST(InstrumentMasterTest, StringViewLookup) {
  auto const master = InstrumentMaster::Build(Fixture());
  std::string const id = "cu2611";
  std::string_view const sv(id.data(), id.size());
  auto const* inst = master.Find(sv);
  ASSERT_NE(inst, nullptr);
  EXPECT_EQ(inst->instrument_id, "cu2611");
}

TEST(InstrumentMasterTest, ByProduct) {
  auto const master = InstrumentMaster::Build(Fixture());
  auto const rb = master.ByProduct("rb");
  EXPECT_EQ(rb.size(), 3u);  // rb2611, rb2701, rb2611C2750 (option included)
  auto const cu = master.ByProduct("cu");
  EXPECT_EQ(cu.size(), 1u);
  EXPECT_TRUE(master.ByProduct("au").empty());
}

TEST(InstrumentMasterTest, EmptyStore) {
  auto const master = InstrumentMaster::Build({});
  EXPECT_EQ(master.size(), 0u);
  EXPECT_EQ(master.Find("rb2701"), nullptr);
  EXPECT_TRUE(master.ByProduct("rb").empty());
}

TEST(InstrumentMasterTest, OptionClassified) {
  auto const master = InstrumentMaster::Build(Fixture());
  auto const* opt = master.Find("rb2611C2750");
  ASSERT_NE(opt, nullptr);
  EXPECT_EQ(opt->product_class, ProductClass::kOption);
}

}  // namespace
}  // namespace cts::instrument
