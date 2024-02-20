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

#include "mpact/sim/util/memory/atomic_tagged_memory.h"

#include <cstdint>

#include "googlemock/include/gmock/gmock.h"  // IWYU pragma: keep
#include "googletest/include/gtest/gtest.h"
#include "mpact/sim/generic/data_buffer.h"
#include "mpact/sim/util/memory/atomic_memory.h"
#include "mpact/sim/util/memory/tagged_flat_demand_memory.h"

namespace {

// This file implements some tests to verify that the atomic memory class
// performs the required operations appropriately.

using ::mpact::sim::generic::DataBuffer;
using ::mpact::sim::generic::DataBufferFactory;
using ::mpact::sim::util::AtomicTaggedMemory;
using ::mpact::sim::util::TaggedFlatDemandMemory;
using Operation = ::mpact::sim::util::AtomicMemory::Operation;

constexpr uint32_t kBaseValue = 0x8765'4321;
constexpr uint32_t kSecondValue = 0x4321'8765;
constexpr uint64_t kBaseAddr = 0x1000;

class AtomicTaggedMemoryTest : public ::testing::Test {
 protected:
  AtomicTaggedMemoryTest() {
    flat_memory_ = new TaggedFlatDemandMemory(8);
    memory_ = new AtomicTaggedMemory(flat_memory_);
  }

  ~AtomicTaggedMemoryTest() override {
    delete memory_;
    delete flat_memory_;
  }

  AtomicTaggedMemory *memory_;
  TaggedFlatDemandMemory *flat_memory_;
  DataBufferFactory db_factory_;
  DataBuffer *db_;
};

// Test that loads and stores are passed through to the FlatDemandMemory
// for simple loads and stores.
TEST_F(AtomicTaggedMemoryTest, PassThroughLoadsStores) {
  DataBuffer *st_db1 = db_factory_.Allocate<uint8_t>(1);
  DataBuffer *st_db2 = db_factory_.Allocate<uint16_t>(1);
  DataBuffer *st_db4 = db_factory_.Allocate<uint32_t>(1);
  DataBuffer *st_db8 = db_factory_.Allocate<uint64_t>(1);

  st_db1->Set<uint8_t>(0, 0x0F);
  st_db2->Set<uint16_t>(0, 0xA5A5);
  st_db4->Set<uint32_t>(0, 0xDEADBEEF);
  st_db8->Set<uint64_t>(0, 0x0F0F0F0F'A5A5A5A5);

  memory_->Store(0x1000, st_db1);
  memory_->Store(0x1002, st_db2);
  memory_->Store(0x1004, st_db4);
  memory_->Store(0x1008, st_db8);

  DataBuffer *ld_db1 = db_factory_.Allocate<uint8_t>(1);
  DataBuffer *ld_db2 = db_factory_.Allocate<uint16_t>(1);
  DataBuffer *ld_db4 = db_factory_.Allocate<uint32_t>(1);
  DataBuffer *ld_db8 = db_factory_.Allocate<uint64_t>(1);

  flat_memory_->Load(0x1000, ld_db1, nullptr, nullptr);
  flat_memory_->Load(0x1002, ld_db2, nullptr, nullptr);
  flat_memory_->Load(0x1004, ld_db4, nullptr, nullptr);
  flat_memory_->Load(0x1008, ld_db8, nullptr, nullptr);

  EXPECT_EQ(ld_db1->Get<uint8_t>(0), st_db1->Get<uint8_t>(0));
  EXPECT_EQ(ld_db2->Get<uint16_t>(0), st_db2->Get<uint16_t>(0));
  EXPECT_EQ(ld_db4->Get<uint32_t>(0), st_db4->Get<uint32_t>(0));
  EXPECT_EQ(ld_db8->Get<uint64_t>(0), st_db8->Get<uint64_t>(0));

  memory_->Load(0x1000, ld_db1, nullptr, nullptr);
  memory_->Load(0x1002, ld_db2, nullptr, nullptr);
  memory_->Load(0x1004, ld_db4, nullptr, nullptr);
  memory_->Load(0x1008, ld_db8, nullptr, nullptr);

  EXPECT_EQ(ld_db1->Get<uint8_t>(0), st_db1->Get<uint8_t>(0));
  EXPECT_EQ(ld_db2->Get<uint16_t>(0), st_db2->Get<uint16_t>(0));
  EXPECT_EQ(ld_db4->Get<uint32_t>(0), st_db4->Get<uint32_t>(0));
  EXPECT_EQ(ld_db8->Get<uint64_t>(0), st_db8->Get<uint64_t>(0));

  ld_db1->DecRef();
  ld_db2->DecRef();
  ld_db4->DecRef();
  ld_db8->DecRef();

  st_db1->DecRef();
  st_db2->DecRef();
  st_db4->DecRef();
  st_db8->DecRef();
}

// Test load-linked, store-conditional.
TEST_F(AtomicTaggedMemoryTest, TestLlSc) {
  auto *db = db_factory_.Allocate<uint32_t>(1);
  // Store a known value to kBaseAddr.
  db->Set<uint32_t>(0, kBaseValue);
  flat_memory_->Store(kBaseAddr, db);
  // Perform load linked.
  auto status = memory_->PerformMemoryOp(kBaseAddr, Operation::kLoadLinked, db,
                                         nullptr, nullptr);
  EXPECT_TRUE(status.ok());
  ASSERT_EQ(kBaseValue, db->Get<uint32_t>(0));
  // Perform unrelated store.
  db->Set<uint32_t>(0, 0xDEADBEEF);
  memory_->Store(kBaseAddr + 0x100, db);
  // Store conditionally.
  db->Set<uint32_t>(0, kBaseValue + 1);
  status = memory_->PerformMemoryOp(kBaseAddr, Operation::kStoreConditional, db,
                                    nullptr, nullptr);
  EXPECT_TRUE(status.ok());
  // Check that the store succeeded.
  EXPECT_EQ(db->Get<uint32_t>(0), 0);
  // Verify that the value in memory is correct.
  flat_memory_->Load(kBaseAddr, db, nullptr, nullptr);
  EXPECT_EQ(db->Get<uint32_t>(0), kBaseValue + 1);

  // Try a second store - it should fail.
  status = memory_->PerformMemoryOp(kBaseAddr, Operation::kStoreConditional, db,
                                    nullptr, nullptr);
  EXPECT_TRUE(status.ok());
  EXPECT_NE(db->Get<uint32_t>(0), 0);

  db->DecRef();
}

// Test load-linked, store-conditional.
TEST_F(AtomicTaggedMemoryTest, TestLlScFailure) {
  auto *db = db_factory_.Allocate<uint32_t>(1);
  auto *db2 = db_factory_.Allocate<uint16_t>(1);
  // Store a known value to kBaseAddr.
  db->Set<uint32_t>(0, kBaseValue);
  memory_->Store(kBaseAddr, db);
  // Perform load linked.
  auto status = memory_->PerformMemoryOp(kBaseAddr, Operation::kLoadLinked, db,
                                         nullptr, nullptr);
  ASSERT_TRUE(status.ok());
  ASSERT_EQ(kBaseValue, db->Get<uint32_t>(0));
  // Perform store to overlapping memory.
  db2->Set<uint16_t>(0, 0xDEAD);
  memory_->Store(kBaseAddr + 2, db2);
  // Store conditionally.
  db->Set<uint32_t>(0, kBaseValue + 1);
  status = memory_->PerformMemoryOp(kBaseAddr, Operation::kStoreConditional, db,
                                    nullptr, nullptr);
  EXPECT_TRUE(status.ok());
  // Check that the store succeeded.
  EXPECT_NE(db->Get<uint32_t>(0), 0);
  // Verify that the value in memory is not updated.
  flat_memory_->Load(kBaseAddr, db, nullptr, nullptr);
  EXPECT_EQ(db->Get<uint32_t>(0), (kBaseValue & 0x0000'ffff) | 0xDEAD0000);

  // Try a second store - it should fail.
  status = memory_->PerformMemoryOp(kBaseAddr, Operation::kStoreConditional, db,
                                    nullptr, nullptr);
  EXPECT_TRUE(status.ok());
  EXPECT_NE(db->Get<uint32_t>(0), 0);

  db->DecRef();
  db2->DecRef();
}

// Swap
TEST_F(AtomicTaggedMemoryTest, Swap) {
  auto *db = db_factory_.Allocate<uint32_t>(1);
  // Store a known value to kBaseAddr.
  db->Set<uint32_t>(0, kBaseValue);
  flat_memory_->Store(kBaseAddr, db);
  // Perform swap.
  db->Set<uint32_t>(0, 0xDEADBEEF);
  auto status = memory_->PerformMemoryOp(kBaseAddr, Operation::kAtomicSwap, db,
                                         nullptr, nullptr);
  EXPECT_TRUE(status.ok());
  EXPECT_EQ(kBaseValue, db->Get<uint32_t>(0));
  flat_memory_->Load(kBaseAddr, db, nullptr, nullptr);
  EXPECT_EQ(db->Get<uint32_t>(0), 0xDEADBEEF);
  db->DecRef();
}

// Add
TEST_F(AtomicTaggedMemoryTest, Add) {
  auto *db = db_factory_.Allocate<uint32_t>(1);
  // Store a known value to kBaseAddr.
  db->Set<uint32_t>(0, kBaseValue);
  flat_memory_->Store(kBaseAddr, db);
  // Perform swap.
  db->Set<uint32_t>(0, kSecondValue);
  auto status = memory_->PerformMemoryOp(kBaseAddr, Operation::kAtomicAdd, db,
                                         nullptr, nullptr);
  EXPECT_TRUE(status.ok());
  EXPECT_EQ(kBaseValue, db->Get<uint32_t>(0));
  flat_memory_->Load(kBaseAddr, db, nullptr, nullptr);
  EXPECT_EQ(db->Get<uint32_t>(0), kBaseValue + kSecondValue);
  db->DecRef();
}

// Subtract
TEST_F(AtomicTaggedMemoryTest, Sub) {
  auto *db = db_factory_.Allocate<uint32_t>(1);
  // Store a known value to kBaseAddr.
  db->Set<uint32_t>(0, kBaseValue);
  flat_memory_->Store(kBaseAddr, db);
  // Perform swap.
  db->Set<uint32_t>(0, kSecondValue);
  auto status = memory_->PerformMemoryOp(kBaseAddr, Operation::kAtomicSub, db,
                                         nullptr, nullptr);
  EXPECT_TRUE(status.ok());
  EXPECT_EQ(kBaseValue, db->Get<uint32_t>(0));
  flat_memory_->Load(kBaseAddr, db, nullptr, nullptr);
  EXPECT_EQ(db->Get<uint32_t>(0), kBaseValue - kSecondValue);
  db->DecRef();
}

// And
TEST_F(AtomicTaggedMemoryTest, And) {
  auto *db = db_factory_.Allocate<uint32_t>(1);
  // Store a known value to kBaseAddr.
  db->Set<uint32_t>(0, kBaseValue);
  flat_memory_->Store(kBaseAddr, db);
  // Perform swap.
  db->Set<uint32_t>(0, kSecondValue);
  auto status = memory_->PerformMemoryOp(kBaseAddr, Operation::kAtomicAnd, db,
                                         nullptr, nullptr);
  EXPECT_TRUE(status.ok());
  EXPECT_EQ(kBaseValue, db->Get<uint32_t>(0));
  flat_memory_->Load(kBaseAddr, db, nullptr, nullptr);
  EXPECT_EQ(db->Get<uint32_t>(0), kBaseValue & kSecondValue);
  db->DecRef();
}

// Or
TEST_F(AtomicTaggedMemoryTest, Or) {
  auto *db = db_factory_.Allocate<uint32_t>(1);
  // Store a known value to kBaseAddr.
  db->Set<uint32_t>(0, kBaseValue);
  flat_memory_->Store(kBaseAddr, db);
  // Perform swap.
  db->Set<uint32_t>(0, kSecondValue);
  auto status = memory_->PerformMemoryOp(kBaseAddr, Operation::kAtomicOr, db,
                                         nullptr, nullptr);
  EXPECT_TRUE(status.ok());
  EXPECT_EQ(kBaseValue, db->Get<uint32_t>(0));
  flat_memory_->Load(kBaseAddr, db, nullptr, nullptr);
  EXPECT_EQ(db->Get<uint32_t>(0), kBaseValue | kSecondValue);
  db->DecRef();
}

// Xor
TEST_F(AtomicTaggedMemoryTest, Xor) {
  auto *db = db_factory_.Allocate<uint32_t>(1);
  // Store a known value to kBaseAddr.
  db->Set<uint32_t>(0, kBaseValue);
  flat_memory_->Store(kBaseAddr, db);
  // Perform swap.
  db->Set<uint32_t>(0, kSecondValue);
  auto status = memory_->PerformMemoryOp(kBaseAddr, Operation::kAtomicXor, db,
                                         nullptr, nullptr);
  EXPECT_TRUE(status.ok());
  EXPECT_EQ(kBaseValue, db->Get<uint32_t>(0));
  flat_memory_->Load(kBaseAddr, db, nullptr, nullptr);
  EXPECT_EQ(db->Get<uint32_t>(0), kBaseValue ^ kSecondValue);
  db->DecRef();
}

// Max
TEST_F(AtomicTaggedMemoryTest, Max) {
  auto *db = db_factory_.Allocate<uint32_t>(1);
  // Store a known value to kBaseAddr.
  db->Set<uint32_t>(0, kBaseValue);
  flat_memory_->Store(kBaseAddr, db);
  // Perform swap.
  db->Set<uint32_t>(0, kSecondValue);
  auto status = memory_->PerformMemoryOp(kBaseAddr, Operation::kAtomicMax, db,
                                         nullptr, nullptr);
  EXPECT_TRUE(status.ok());
  EXPECT_EQ(kBaseValue, db->Get<uint32_t>(0));
  flat_memory_->Load(kBaseAddr, db, nullptr, nullptr);
  EXPECT_EQ(db->Get<uint32_t>(0), std::max(static_cast<int32_t>(kBaseValue),
                                           static_cast<int32_t>(kSecondValue)));
  db->DecRef();
}

// Maxu
TEST_F(AtomicTaggedMemoryTest, Maxu) {
  auto *db = db_factory_.Allocate<uint32_t>(1);
  // Store a known value to kBaseAddr.
  db->Set<uint32_t>(0, kBaseValue);
  flat_memory_->Store(kBaseAddr, db);
  // Perform swap.
  db->Set<uint32_t>(0, kSecondValue);
  auto status = memory_->PerformMemoryOp(kBaseAddr, Operation::kAtomicMaxu, db,
                                         nullptr, nullptr);
  EXPECT_TRUE(status.ok());
  EXPECT_EQ(kBaseValue, db->Get<uint32_t>(0));
  flat_memory_->Load(kBaseAddr, db, nullptr, nullptr);
  EXPECT_EQ(db->Get<uint32_t>(0),
            std::max(static_cast<uint32_t>(kBaseValue),
                     static_cast<uint32_t>(kSecondValue)));
  db->DecRef();
}

// Min
TEST_F(AtomicTaggedMemoryTest, Min) {
  auto *db = db_factory_.Allocate<uint32_t>(1);
  // Store a known value to kBaseAddr.
  db->Set<uint32_t>(0, kBaseValue);
  flat_memory_->Store(kBaseAddr, db);
  // Perform swap.
  db->Set<uint32_t>(0, kSecondValue);
  auto status = memory_->PerformMemoryOp(kBaseAddr, Operation::kAtomicMin, db,
                                         nullptr, nullptr);
  EXPECT_TRUE(status.ok());
  EXPECT_EQ(kBaseValue, db->Get<uint32_t>(0));
  flat_memory_->Load(kBaseAddr, db, nullptr, nullptr);
  EXPECT_EQ(db->Get<uint32_t>(0), std::min(static_cast<int32_t>(kBaseValue),
                                           static_cast<int32_t>(kSecondValue)));
  db->DecRef();
}

// Minu
TEST_F(AtomicTaggedMemoryTest, Minu) {
  auto *db = db_factory_.Allocate<uint32_t>(1);
  // Store a known value to kBaseAddr.
  db->Set<uint32_t>(0, kBaseValue);
  flat_memory_->Store(kBaseAddr, db);
  // Perform swap.
  db->Set<uint32_t>(0, kSecondValue);
  auto status = memory_->PerformMemoryOp(kBaseAddr, Operation::kAtomicMinu, db,
                                         nullptr, nullptr);
  EXPECT_TRUE(status.ok());
  EXPECT_EQ(kBaseValue, db->Get<uint32_t>(0));
  flat_memory_->Load(kBaseAddr, db, nullptr, nullptr);
  EXPECT_EQ(db->Get<uint32_t>(0),
            std::min(static_cast<uint32_t>(kBaseValue),
                     static_cast<uint32_t>(kSecondValue)));
  db->DecRef();
}

}  // namespace
