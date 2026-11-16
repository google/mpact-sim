// Copyright 2023 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "mpact/sim/util/memory/memory_watcher.h"

#include <cstdint>

#include "absl/strings/string_view.h"
#include "googlemock/include/gmock/gmock.h"
#include "googletest/include/gtest/gtest.h"
#include "mpact/sim/generic/data_buffer.h"
#include "mpact/sim/util/memory/flat_demand_memory.h"

namespace {

using ::mpact::sim::generic::DataBuffer;
using ::mpact::sim::generic::DataBufferFactory;
using ::mpact::sim::util::FlatDemandMemory;
using ::mpact::sim::util::MemoryWatcher;
using AddressRange = ::mpact::sim::util::MemoryWatcher::AddressRange;

class MemoryWatcherTest : public testing::Test {
 protected:
  MemoryWatcherTest() {
    memory_ = new FlatDemandMemory(0);
    watcher_ = new MemoryWatcher(memory_);
  }
  ~MemoryWatcherTest() override {
    delete watcher_;
    delete memory_;
  }

  DataBufferFactory db_factory_;
  MemoryWatcher* watcher_;
  FlatDemandMemory* memory_;
};

// This tests if status is ok when setting non overlapping ranges.
TEST_F(MemoryWatcherTest, SetRanges) {
  int counter = 0;
  uint64_t address = 0;
  // Load callbacks.
  EXPECT_TRUE(
      watcher_
          ->SetLoadWatchCallback(AddressRange(0x1000),
                                 [&counter, &address](uint64_t addr, int) {
                                   address = addr;
                                   counter++;
                                 })
          .ok());
  EXPECT_TRUE(
      watcher_
          ->SetLoadWatchCallback(AddressRange(0x1001, 0x1003),
                                 [&counter, &address](uint64_t addr, int) {
                                   address = addr;
                                   counter++;
                                 })
          .ok());
  // Store callbacks.
  EXPECT_TRUE(
      watcher_
          ->SetStoreWatchCallback(AddressRange(0x1000),
                                  [&counter, &address](uint64_t addr, int sz) {
                                    address = addr;
                                    counter++;
                                  })
          .ok());
  EXPECT_TRUE(
      watcher_
          ->SetStoreWatchCallback(AddressRange(0x1001, 0x1003),
                                  [&counter, &address](uint64_t addr, int sz) {
                                    address = addr;
                                    counter++;
                                  })
          .ok());
}

// This checks that overlapping ranges generate errors.
TEST_F(MemoryWatcherTest, OverlappingRanges) {
  int counter = 0;
  uint64_t address = 0;
  // Load callbacks.
  EXPECT_TRUE(
      watcher_
          ->SetLoadWatchCallback(AddressRange(0x1000),
                                 [&counter, &address](uint64_t addr, int) {
                                   address = addr;
                                   counter++;
                                 })
          .ok());
  EXPECT_FALSE(
      watcher_
          ->SetLoadWatchCallback(AddressRange(0x1000, 0x1003),
                                 [&counter, &address](uint64_t addr, int) {
                                   address = addr;
                                   counter++;
                                 })
          .ok());

  // Store callbacks.
  EXPECT_TRUE(
      watcher_
          ->SetStoreWatchCallback(AddressRange(0x1000),
                                  [&counter, &address](uint64_t addr, int) {
                                    address = addr;
                                    counter++;
                                  })
          .ok());
  EXPECT_FALSE(
      watcher_
          ->SetStoreWatchCallback(AddressRange(0x1000, 0x1003),
                                  [&counter, &address](uint64_t addr, int) {
                                    address = addr;
                                    counter++;
                                  })
          .ok());
}

// Verifying that load callbacks are made.
TEST_F(MemoryWatcherTest, LoadWatch) {
  int counter = 0;
  uint64_t address = 0;
  int size = 0;
  // Three load callbacks are set.
  EXPECT_TRUE(watcher_
                  ->SetLoadWatchCallback(
                      AddressRange(0x1000),
                      [&counter, &address, &size](uint64_t addr, int sz) {
                        address = addr;
                        size = sz;
                        counter++;
                      })
                  .ok());
  EXPECT_TRUE(watcher_
                  ->SetLoadWatchCallback(
                      AddressRange(0x1002, 0x1003),
                      [&counter, &address, &size](uint64_t addr, int sz) {
                        address = addr;
                        size = sz;
                        counter++;
                      })
                  .ok());
  EXPECT_TRUE(watcher_
                  ->SetLoadWatchCallback(
                      AddressRange(0x1004, 0x1007),
                      [&counter, &address, &size](uint64_t addr, int sz) {
                        address = addr;
                        size = sz;
                        counter++;
                      })
                  .ok());

  DataBuffer* db1 = db_factory_.Allocate<uint8_t>(1);
  DataBuffer* db2 = db_factory_.Allocate<uint16_t>(1);
  DataBuffer* db4 = db_factory_.Allocate<uint32_t>(1);
  DataBuffer* db8 = db_factory_.Allocate<uint64_t>(1);

  // First ensure that the store doesn't trigger a callback.
  watcher_->Store(0x1000, db8);
  EXPECT_EQ(counter, 0);
  EXPECT_EQ(address, 0);
  EXPECT_EQ(size, 0);

  // Triggers a single callback.
  watcher_->Load(0x1000, db1, nullptr, nullptr);
  EXPECT_EQ(counter, 1);
  EXPECT_EQ(address, 0x1000);
  EXPECT_EQ(size, 1);
  // Triggers another callback, this time with size 2.
  watcher_->Load(0x1000, db2, nullptr, nullptr);
  EXPECT_EQ(counter, 2);
  EXPECT_EQ(address, 0x1000);
  EXPECT_EQ(size, 2);
  // Triggers two callbacks, size = 4.
  watcher_->Load(0x1000, db4, nullptr, nullptr);
  EXPECT_EQ(counter, 4);
  EXPECT_EQ(address, 0x1000);
  EXPECT_EQ(size, 4);
  // Triggers another three callbacks.
  watcher_->Load(0x1000, db8, nullptr, nullptr);
  EXPECT_EQ(counter, 7);
  EXPECT_EQ(address, 0x1000);
  EXPECT_EQ(size, 8);

  db1->DecRef();
  db2->DecRef();
  db4->DecRef();
  db8->DecRef();
}

TEST_F(MemoryWatcherTest, GatherWatch) {
  int counter = 0;
  uint64_t address = 0;
  int size = 0;
  // Three load watch points.
  EXPECT_TRUE(watcher_
                  ->SetLoadWatchCallback(
                      AddressRange(0x1000),
                      [&counter, &address, &size](uint64_t addr, int sz) {
                        address = addr;
                        size = sz;
                        counter++;
                      })
                  .ok());
  EXPECT_TRUE(watcher_
                  ->SetLoadWatchCallback(
                      AddressRange(0x1002, 0x1003),
                      [&counter, &address, &size](uint64_t addr, int sz) {
                        address = addr;
                        size = sz;
                        counter++;
                      })
                  .ok());
  EXPECT_TRUE(watcher_
                  ->SetLoadWatchCallback(
                      AddressRange(0x1004, 0x1007),
                      [&counter, &address, &size](uint64_t addr, int sz) {
                        address = addr;
                        size = sz;
                        counter++;
                      })
                  .ok());

  DataBuffer* address_db = db_factory_.Allocate<uint64_t>(4);
  DataBuffer* mask_db = db_factory_.Allocate<bool>(4);
  DataBuffer* data_db = db_factory_.Allocate<uint32_t>(4);

  auto address_span = address_db->Get<uint64_t>();
  auto mask_span = mask_db->Get<bool>();

  // Set up the gather address and mask vectors.
  address_span[0] = 0x1000;
  address_span[1] = 0x1004;
  address_span[2] = 0x1000;
  address_span[3] = 0x1004;
  mask_span[0] = true;
  mask_span[1] = true;
  mask_span[2] = false;
  mask_span[3] = true;

  // No callbacks for a scatter store.
  watcher_->Store(address_db, mask_db, sizeof(uint32_t), data_db);
  EXPECT_EQ(counter, 0);
  EXPECT_EQ(address, 0);
  EXPECT_EQ(size, 0);

  // The gather load should generate 2 callbacks from 0x1000, one from the
  // first 0x1004, and finally another one from the last 0x1004, for a total
  // of 4. Size is 4 and the last address is 0x1004.
  watcher_->Load(address_db, mask_db, sizeof(uint32_t), data_db, nullptr,
                 nullptr);
  EXPECT_EQ(counter, 4);
  EXPECT_EQ(address, 0x1004);
  EXPECT_EQ(size, 4);

  address_db->DecRef();
  mask_db->DecRef();
  data_db->DecRef();
}

TEST_F(MemoryWatcherTest, StoreWatch) {
  int counter = 0;
  uint64_t address = 0;
  int size = 0;
  // Set three watchpoints [1000,1000], [1002, 1003], [1004, 1007]
  EXPECT_TRUE(watcher_
                  ->SetStoreWatchCallback(
                      AddressRange(0x1000),
                      [&counter, &address, &size](uint64_t addr, int sz) {
                        address = addr;
                        size = sz;
                        counter++;
                      })
                  .ok());
  EXPECT_TRUE(watcher_
                  ->SetStoreWatchCallback(
                      AddressRange(0x1002, 0x1003),
                      [&counter, &address, &size](uint64_t addr, int sz) {
                        address = addr;
                        size = sz;
                        counter++;
                      })
                  .ok());
  EXPECT_TRUE(watcher_
                  ->SetStoreWatchCallback(
                      AddressRange(0x1004, 0x1007),
                      [&counter, &address, &size](uint64_t addr, int sz) {
                        address = addr;
                        size = sz;
                        counter++;
                      })
                  .ok());

  DataBuffer* db1 = db_factory_.Allocate<uint8_t>(1);
  DataBuffer* db2 = db_factory_.Allocate<uint16_t>(1);
  DataBuffer* db4 = db_factory_.Allocate<uint32_t>(1);
  DataBuffer* db8 = db_factory_.Allocate<uint64_t>(1);

  // No callbacks for a load.
  watcher_->Load(0x1000, db8, nullptr, nullptr);
  EXPECT_EQ(counter, 0);
  EXPECT_EQ(address, 0);
  EXPECT_EQ(size, 0);

  // Shold generate 1 callback.
  watcher_->Store(0x1000, db1);
  EXPECT_EQ(counter, 1);
  EXPECT_EQ(address, 0x1000);
  EXPECT_EQ(size, 1);
  // Should generate another single callback.
  watcher_->Store(0x1000, db2);
  EXPECT_EQ(counter, 2);
  EXPECT_EQ(address, 0x1000);
  EXPECT_EQ(size, 2);
  // Should generate two callbacks.
  watcher_->Store(0x1000, db4);
  EXPECT_EQ(counter, 4);
  EXPECT_EQ(address, 0x1000);
  EXPECT_EQ(size, 4);
  // Should generate three callbacks.
  watcher_->Store(0x1000, db8);
  EXPECT_EQ(counter, 7);
  EXPECT_EQ(address, 0x1000);
  EXPECT_EQ(size, 8);

  db1->DecRef();
  db2->DecRef();
  db4->DecRef();
  db8->DecRef();
}

TEST_F(MemoryWatcherTest, ScatterWatch) {
  int counter = 0;
  uint64_t address = 0;
  int size = 0;
  EXPECT_TRUE(watcher_
                  ->SetStoreWatchCallback(
                      AddressRange(0x1000),
                      [&counter, &address, &size](uint64_t addr, int sz) {
                        address = addr;
                        size = sz;
                        counter++;
                      })
                  .ok());
  EXPECT_TRUE(watcher_
                  ->SetStoreWatchCallback(
                      AddressRange(0x1002, 0x1003),
                      [&counter, &address, &size](uint64_t addr, int sz) {
                        address = addr;
                        size = sz;
                        counter++;
                      })
                  .ok());
  EXPECT_TRUE(watcher_
                  ->SetStoreWatchCallback(
                      AddressRange(0x1004, 0x1007),
                      [&counter, &address, &size](uint64_t addr, int sz) {
                        address = addr;
                        size = sz;
                        counter++;
                      })
                  .ok());

  DataBuffer* address_db = db_factory_.Allocate<uint64_t>(4);
  DataBuffer* mask_db = db_factory_.Allocate<bool>(4);
  DataBuffer* data_db = db_factory_.Allocate<uint32_t>(4);

  auto address_span = address_db->Get<uint64_t>();
  auto mask_span = mask_db->Get<bool>();

  address_span[0] = 0x1000;
  address_span[1] = 0x1004;
  address_span[2] = 0x1000;
  address_span[3] = 0x1004;
  mask_span[0] = true;
  mask_span[1] = true;
  mask_span[2] = false;
  mask_span[3] = true;

  // The gather load shouldn't generate a callback.
  watcher_->Load(address_db, mask_db, sizeof(uint32_t), data_db, nullptr,
                 nullptr);
  EXPECT_EQ(counter, 0);
  EXPECT_EQ(address, 0);
  EXPECT_EQ(size, 0);

  // The scatter should generate 2 callbacks from 0x1000, one from the
  // first 0x1004, and finally another one from the last 0x1004, for a total
  // of 4. Size is 4 and the last address is 0x1004.
  watcher_->Store(address_db, mask_db, sizeof(uint32_t), data_db);
  EXPECT_EQ(counter, 4);
  EXPECT_EQ(address, 0x1004);
  EXPECT_EQ(size, 4);

  address_db->DecRef();
  mask_db->DecRef();
  data_db->DecRef();
}

TEST_F(MemoryWatcherTest, UnitStrideVectorStoreAndLoad) {
  int load_hits = 0;
  int store_hits = 0;
  uint64_t last_addr = 0;
  EXPECT_TRUE(watcher_
                  ->SetStoreWatchCallback(AddressRange(0x1008, 0x100b),
                                          [&](uint64_t addr, int) {
                                            last_addr = addr;
                                            store_hits++;
                                          })
                  .ok());
  EXPECT_TRUE(watcher_
                  ->SetLoadWatchCallback(AddressRange(0x1008, 0x100b),
                                         [&](uint64_t addr, int) {
                                           last_addr = addr;
                                           load_hits++;
                                         })
                  .ok());

  DataBuffer* address_db = db_factory_.Allocate<uint64_t>(1);
  DataBuffer* mask_db = db_factory_.Allocate<bool>(4);
  DataBuffer* data_db = db_factory_.Allocate<uint32_t>(4);
  address_db->Set<uint64_t>(0, 0x1000);
  auto masks = mask_db->Get<bool>();
  masks[0] = true;
  masks[1] = true;
  masks[2] = true;
  masks[3] = true;

  watcher_->Store(address_db, mask_db, sizeof(uint32_t), data_db);
  EXPECT_EQ(store_hits, 1);
  EXPECT_EQ(last_addr, 0x1008);

  last_addr = 0;
  watcher_->Load(address_db, mask_db, sizeof(uint32_t), data_db, nullptr,
                 nullptr);
  EXPECT_EQ(load_hits, 1);
  EXPECT_EQ(last_addr, 0x1008);

  address_db->DecRef();
  mask_db->DecRef();
  data_db->DecRef();
}

TEST(WordAddressedMemoryWatcherTest, SingleWordStoreDoesNotHitNeighborWord) {
  FlatDemandMemory word_mem(1024, 0, /*addressable_unit_size=*/4, 0);
  MemoryWatcher word_watcher(&word_mem, /*addressable_unit_shift=*/2);
  DataBufferFactory db_factory;

  int hits = 0;
  EXPECT_TRUE(word_watcher
                  .SetStoreWatchCallback(AddressRange(102),
                                         [&](uint64_t, int) { hits++; })
                  .ok());

  DataBuffer* word_db = db_factory.Allocate<uint32_t>(1);
  word_db->Set<uint32_t>(0, 0xdeadbeef);
  // Storing 1 word (4 bytes) at word 100 must NOT falsely match word 102.
  word_watcher.Store(100, word_db);
  EXPECT_EQ(hits, 0);
  word_watcher.Store(102, word_db);
  EXPECT_EQ(hits, 1);
  word_db->DecRef();
}

}  // namespace
