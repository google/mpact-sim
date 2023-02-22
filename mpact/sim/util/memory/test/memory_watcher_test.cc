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
  MemoryWatcher *watcher_;
  FlatDemandMemory *memory_;
};

// This tests if status is ok when setting non overlapping ranges.
TEST_F(MemoryWatcherTest, SetRanges) {
  int counter = 0;
  uint64_t address = 0;
  // Load callbacks.
  EXPECT_TRUE(watcher_->SetLoadWatchCallback(
      AddressRange(0x1000), [&counter, &address](uint64_t addr, int) {
        address = addr;
        counter++;
      }).ok());
  EXPECT_TRUE(watcher_->SetLoadWatchCallback(
      AddressRange(0x1001, 0x1003), [&counter, &address](uint64_t addr, int) {
        address = addr;
        counter++;
      }).ok());
  // Store callbacks.
  EXPECT_TRUE(watcher_->SetStoreWatchCallback(
      AddressRange(0x1000), [&counter, &address](uint64_t addr, int sz) {
        address = addr;
        counter++;
      }).ok());
  EXPECT_TRUE(watcher_->SetStoreWatchCallback(
      AddressRange(0x1001, 0x1003),
      [&counter, &address](uint64_t addr, int sz) {
        address = addr;
        counter++;
      }).ok());
}

// This checks that overlapping ranges generate errors.
TEST_F(MemoryWatcherTest, OverlappingRanges) {
  int counter = 0;
  uint64_t address = 0;
  // Load callbacks.
  EXPECT_TRUE(watcher_->SetLoadWatchCallback(
      AddressRange(0x1000), [&counter, &address](uint64_t addr, int) {
        address = addr;
        counter++;
      }).ok());
  EXPECT_FALSE(
      watcher_
          ->SetLoadWatchCallback(AddressRange(0x1000, 0x1003),
                                 [&counter, &address](uint64_t addr, int) {
                                   address = addr;
                                   counter++;
                                 })
          .ok());

  // Store callbacks.
  EXPECT_TRUE(watcher_->SetStoreWatchCallback(
      AddressRange(0x1000), [&counter, &address](uint64_t addr, int) {
        address = addr;
        counter++;
      }).ok());
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
  EXPECT_TRUE(watcher_->SetLoadWatchCallback(
      AddressRange(0x1000), [&counter, &address, &size](uint64_t addr, int sz) {
        address = addr;
        size = sz;
        counter++;
      }).ok());
  EXPECT_TRUE(watcher_->SetLoadWatchCallback(
      AddressRange(0x1002, 0x1003),
      [&counter, &address, &size](uint64_t addr, int sz) {
        address = addr;
        size = sz;
        counter++;
      }).ok());
  EXPECT_TRUE(watcher_->SetLoadWatchCallback(
      AddressRange(0x1004, 0x1007),
      [&counter, &address, &size](uint64_t addr, int sz) {
        address = addr;
        size = sz;
        counter++;
      }).ok());

  DataBuffer *db1 = db_factory_.Allocate<uint8_t>(1);
  DataBuffer *db2 = db_factory_.Allocate<uint16_t>(1);
  DataBuffer *db4 = db_factory_.Allocate<uint32_t>(1);
  DataBuffer *db8 = db_factory_.Allocate<uint64_t>(1);

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
  EXPECT_TRUE(watcher_->SetLoadWatchCallback(
      AddressRange(0x1000), [&counter, &address, &size](uint64_t addr, int sz) {
        address = addr;
        size = sz;
        counter++;
      }).ok());
  EXPECT_TRUE(watcher_->SetLoadWatchCallback(
      AddressRange(0x1002, 0x1003),
      [&counter, &address, &size](uint64_t addr, int sz) {
        address = addr;
        size = sz;
        counter++;
      }).ok());
  EXPECT_TRUE(watcher_->SetLoadWatchCallback(
      AddressRange(0x1004, 0x1007),
      [&counter, &address, &size](uint64_t addr, int sz) {
        address = addr;
        size = sz;
        counter++;
      }).ok());

  DataBuffer *address_db = db_factory_.Allocate<uint64_t>(4);
  DataBuffer *mask_db = db_factory_.Allocate<bool>(4);
  DataBuffer *data_db = db_factory_.Allocate<uint32_t>(4);

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
  EXPECT_TRUE(watcher_->SetStoreWatchCallback(
      AddressRange(0x1000), [&counter, &address, &size](uint64_t addr, int sz) {
        address = addr;
        size = sz;
        counter++;
      }).ok());
  EXPECT_TRUE(watcher_->SetStoreWatchCallback(
      AddressRange(0x1002, 0x1003),
      [&counter, &address, &size](uint64_t addr, int sz) {
        address = addr;
        size = sz;
        counter++;
      }).ok());
  EXPECT_TRUE(watcher_->SetStoreWatchCallback(
      AddressRange(0x1004, 0x1007),
      [&counter, &address, &size](uint64_t addr, int sz) {
        address = addr;
        size = sz;
        counter++;
      }).ok());

  DataBuffer *db1 = db_factory_.Allocate<uint8_t>(1);
  DataBuffer *db2 = db_factory_.Allocate<uint16_t>(1);
  DataBuffer *db4 = db_factory_.Allocate<uint32_t>(1);
  DataBuffer *db8 = db_factory_.Allocate<uint64_t>(1);

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
  EXPECT_TRUE(watcher_->SetStoreWatchCallback(
      AddressRange(0x1000), [&counter, &address, &size](uint64_t addr, int sz) {
        address = addr;
        size = sz;
        counter++;
      }).ok());
  EXPECT_TRUE(watcher_->SetStoreWatchCallback(
      AddressRange(0x1002, 0x1003),
      [&counter, &address, &size](uint64_t addr, int sz) {
        address = addr;
        size = sz;
        counter++;
      }).ok());
  EXPECT_TRUE(watcher_->SetStoreWatchCallback(
      AddressRange(0x1004, 0x1007),
      [&counter, &address, &size](uint64_t addr, int sz) {
        address = addr;
        size = sz;
        counter++;
      }).ok());

  DataBuffer *address_db = db_factory_.Allocate<uint64_t>(4);
  DataBuffer *mask_db = db_factory_.Allocate<bool>(4);
  DataBuffer *data_db = db_factory_.Allocate<uint32_t>(4);

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

}  // namespace
