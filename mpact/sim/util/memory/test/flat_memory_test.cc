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

#include "mpact/sim/util/memory/flat_memory.h"

#include <cstdint>
#include <memory>

#include "absl/strings/string_view.h"
#include "googlemock/include/gmock/gmock.h"
#include "googletest/include/gtest/gtest.h"
#include "mpact/sim/generic/arch_state.h"
#include "mpact/sim/generic/data_buffer.h"
#include "mpact/sim/generic/instruction.h"
#include "mpact/sim/generic/operand_interface.h"
#include "mpact/sim/generic/ref_count.h"

namespace mpact {
namespace sim {
namespace util {
namespace {

using mpact::sim::generic::DataBuffer;

struct InstructionContext : public generic::ReferenceCount {
 public:
  uint32_t* value;
};

// Define a class that derives from ArchState since constructors are
// protected.
class MyArchState : public generic::ArchState {
 public:
  MyArchState(absl::string_view id, generic::SourceOperandInterface* pc_op)
      : ArchState(id, pc_op) {}
  explicit MyArchState(absl::string_view id) : MyArchState(id, nullptr) {}
};

// Test fixture class that instantiates an ArchState derived class.
class FlatMemoryTest : public testing::Test {
 protected:
  FlatMemoryTest() { arch_state_ = new MyArchState("TestArchitecture"); }
  ~FlatMemoryTest() override { delete arch_state_; }

  MyArchState* arch_state_;
};

// Verify that size and base address are correct when created.
TEST_F(FlatMemoryTest, BasicCreate) {
  auto mem_0 = std::make_unique<FlatMemory>(1024, 0x0, 1, 0);
  auto mem_1 = std::make_unique<FlatMemory>(2048, 0x1'0000'0000, 2, 0);

  EXPECT_EQ(mem_0->base(), 0x0);
  EXPECT_EQ(mem_0->size(), 1024);
  EXPECT_EQ(mem_0->shift(), 0);
  EXPECT_EQ(mem_1->base(), 0x1'0000'0000);
  EXPECT_EQ(mem_1->size(), 4096);
  EXPECT_EQ(mem_1->shift(), 1);
}

// Simple store followed by load to aligned addresses and no instruction to
// be executed.
TEST_F(FlatMemoryTest, SimpleStoreLoad) {
  auto mem = std::make_unique<FlatMemory>(1024, 0x1000, 1, 0);

  DataBuffer* st_db1 = arch_state_->db_factory()->Allocate<uint8_t>(1);
  DataBuffer* st_db2 = arch_state_->db_factory()->Allocate<uint16_t>(1);
  DataBuffer* st_db4 = arch_state_->db_factory()->Allocate<uint32_t>(1);
  DataBuffer* st_db8 = arch_state_->db_factory()->Allocate<uint64_t>(1);

  st_db1->Set<uint8_t>(0, 0x0F);
  st_db2->Set<uint16_t>(0, 0xA5A5);
  st_db4->Set<uint32_t>(0, 0xDEADBEEF);
  st_db8->Set<uint64_t>(0, 0x0F0F0F0F'A5A5A5A5);

  mem->Store(0x1000, st_db1);
  mem->Store(0x1002, st_db2);
  mem->Store(0x1004, st_db4);
  mem->Store(0x1008, st_db8);

  DataBuffer* ld_db1 = arch_state_->db_factory()->Allocate<uint8_t>(1);
  DataBuffer* ld_db2 = arch_state_->db_factory()->Allocate<uint16_t>(1);
  DataBuffer* ld_db4 = arch_state_->db_factory()->Allocate<uint32_t>(1);
  DataBuffer* ld_db8 = arch_state_->db_factory()->Allocate<uint64_t>(1);

  mem->Load(0x1000, ld_db1, nullptr, nullptr);
  mem->Load(0x1002, ld_db2, nullptr, nullptr);
  mem->Load(0x1004, ld_db4, nullptr, nullptr);
  mem->Load(0x1008, ld_db8, nullptr, nullptr);

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

// Test scatter/gather load/store capability of the Load/Store with
// multiple addresses.
TEST_F(FlatMemoryTest, MultiAddressLoadStore) {
  auto mem = std::make_unique<FlatMemory>(1024, 0x1000, 1, 0);

  DataBuffer* address_db = arch_state_->db_factory()->Allocate<uint64_t>(4);
  DataBuffer* mask_db = arch_state_->db_factory()->Allocate<bool>(4);
  DataBuffer* store_data_db = arch_state_->db_factory()->Allocate<uint32_t>(4);
  DataBuffer* load_data_db = arch_state_->db_factory()->Allocate<uint32_t>(4);
  DataBuffer* load_data2_db = arch_state_->db_factory()->Allocate<uint32_t>(8);

  // Should load zeros's into load_data_db
  mem->Load(0x1000, load_data_db, nullptr, nullptr);
  for (int index = 0; index < 4; index++) {
    EXPECT_EQ(load_data_db->Get<uint32_t>(index), 0);
  }

  // Set values of store_data_db DataBuffer instance.
  store_data_db->Set<uint32_t>(0, 0x01010101);
  store_data_db->Set<uint32_t>(1, 0x02020202);
  store_data_db->Set<uint32_t>(2, 0x03030303);
  store_data_db->Set<uint32_t>(3, 0x04040404);

  // Set up addresses to be
  address_db->Set<uint64_t>(0, 0x1000);
  address_db->Set<uint64_t>(1, 0x1008);
  address_db->Set<uint64_t>(2, 0x1010);
  address_db->Set<uint64_t>(3, 0x1018);

  mask_db->Set<bool>(0, true);
  mask_db->Set<bool>(1, true);
  mask_db->Set<bool>(2, true);
  mask_db->Set<bool>(3, true);

  mem->Store<uint32_t>(address_db, mask_db, store_data_db);

  mask_db->Set<bool>(2, false);
  mem->Load<uint32_t>(address_db, mask_db, load_data_db, nullptr, nullptr);

  // Loaded data should equal stored data, except for index 3 which was
  // masked out of the load.
  for (int index = 0; index < 4; index++) {
    if (mask_db->Get<bool>(index)) {
      EXPECT_EQ(store_data_db->Get<uint32_t>(index),
                load_data_db->Get<uint32_t>(index));
    } else {
      EXPECT_EQ(load_data_db->Get<uint32_t>(index), 0);
    }
  }

  // Load continuous block of data 0x1000-0x1020.
  mem->Load(0x1000, load_data2_db, nullptr, nullptr);
  // Odd words should be 0. The even words are equal to the data in
  // store_data_db.
  for (int index = 0; index < 8; index++) {
    if (index & 0x1) {
      EXPECT_EQ(load_data2_db->Get<uint32_t>(index), 0);
    } else {
      EXPECT_EQ(load_data2_db->Get<uint32_t>(index),
                store_data_db->Get<uint32_t>(index >> 1));
    }
  }

  address_db->DecRef();
  mask_db->DecRef();
  store_data_db->DecRef();
  load_data_db->DecRef();
  load_data2_db->DecRef();
}

// Testing that the instruction semantic function passed in as part of the
// instruction passed to the Load is executed at the right time.
TEST_F(FlatMemoryTest, SingleLoadWithInstruction) {
  auto context = new InstructionContext();
  auto inst = std::make_unique<Instruction>(arch_state_);
  int data = 0;
  inst->set_semantic_function([&](Instruction* instruction) {
    EXPECT_EQ(inst.get(), instruction);
    EXPECT_EQ(instruction->context(), static_cast<ReferenceCount*>(context));
    data++;
  });
  auto mem = std::make_unique<FlatMemory>(1024, 0x1000, 1, 0);

  DataBuffer* ld_db = arch_state_->db_factory()->Allocate<uint32_t>(1);

  // Set latency to zero so that the instruction semantic function in inst is
  // executed immediately.
  ld_db->set_latency(0);
  mem->Load(0x1000, ld_db, inst.get(), context);
  // Verify that the semantic function executed.
  EXPECT_EQ(data, 1);

  // This time set latency to 1 so that the instruction execution is delayed.
  ld_db->set_latency(1);
  mem->Load(0x1000, ld_db, inst.get(), context);
  // Verify that the instruction did not execute.
  EXPECT_EQ(data, 1);

  arch_state_->AdvanceDelayLines();
  // Verify that the instruction did executed.
  EXPECT_EQ(data, 2);

  context->DecRef();
  ld_db->DecRef();
}

TEST_F(FlatMemoryTest, MultiLoadWithInstruction) {
  auto context = new InstructionContext();
  auto inst = std::make_unique<Instruction>(arch_state_);
  int data = 0;
  inst->set_semantic_function([&](Instruction* instruction) {
    EXPECT_EQ(inst.get(), instruction);
    EXPECT_EQ(instruction->context(), static_cast<ReferenceCount*>(context));
    data++;
  });
  auto mem = std::make_unique<FlatMemory>(1024, 0x1000, 1, 0);

  DataBuffer* address_db = arch_state_->db_factory()->Allocate<uint64_t>(4);
  DataBuffer* mask_db = arch_state_->db_factory()->Allocate<bool>(4);
  DataBuffer* ld_db = arch_state_->db_factory()->Allocate<uint32_t>(4);

  // Set up addresses and mask values.
  for (int index = 0; index < 4; index++) {
    address_db->Set<uint64_t>(index, 0x1000 + (index << 3));
    mask_db->Set<bool>(index, true);
  }

  // Use latency of 0 (immediate execution of inst).
  ld_db->set_latency(0);
  mem->Load<uint32_t>(address_db, mask_db, ld_db, inst.get(), context);
  // Verify that the instruction semantic function executed.
  EXPECT_EQ(data, 1);

  // This time use a latency of 1.
  ld_db->set_latency(1);
  mem->Load(0x1020, ld_db, inst.get(), context);
  // Verify that the instruction semantic function has not executed.
  EXPECT_EQ(data, 1);

  arch_state_->AdvanceDelayLines();
  // Now the instruction semantic function should have executed.
  EXPECT_EQ(data, 2);

  context->DecRef();
  address_db->DecRef();
  mask_db->DecRef();
  ld_db->DecRef();
}

TEST_F(FlatMemoryTest, MultiLoadUnitStride) {
  auto mem = std::make_unique<FlatMemory>(1024, 0x1000, 1, 0);

  DataBuffer* address_db = arch_state_->db_factory()->Allocate<uint64_t>(1);
  DataBuffer* mask_db = arch_state_->db_factory()->Allocate<bool>(4);
  DataBuffer* ld_db = arch_state_->db_factory()->Allocate<uint32_t>(4);
  DataBuffer* st_db = arch_state_->db_factory()->Allocate<uint32_t>(4);
  auto ld_span = ld_db->Get<uint32_t>();
  auto st_span = st_db->Get<uint32_t>();
  auto mask_span = mask_db->Get<bool>();
  for (uint32_t i = 0; i < 4; i++) {
    mask_span[i] = true;
    st_span[i] = (i << 16) | ((i + 1) & 0xffff);
  }
  address_db->Set<uint64_t>(0, 0x1000);
  mem->Store<uint32_t>(address_db, mask_db, st_db);
  mem->Load<uint32_t>(address_db, mask_db, ld_db, nullptr, nullptr);
  EXPECT_THAT(ld_span, testing::ElementsAreArray(st_span));
  address_db->DecRef();
  mask_db->DecRef();
  ld_db->DecRef();
  st_db->DecRef();
}

TEST_F(FlatMemoryTest, WordAddressableMemory) {
  auto mem = std::make_unique<FlatMemory>(1024, 0x1000, 4, 0);
  // Allocate data buffers for store data.
  DataBuffer* st_db1 = arch_state_->db_factory()->Allocate<uint8_t>(1);
  DataBuffer* st_db2 = arch_state_->db_factory()->Allocate<uint16_t>(1);
  DataBuffer* st_db4 = arch_state_->db_factory()->Allocate<uint32_t>(1);
  DataBuffer* st_db8 = arch_state_->db_factory()->Allocate<uint64_t>(1);
  // Initialize values to be stored.
  st_db1->Set<uint8_t>(0, 0x0F);
  st_db2->Set<uint16_t>(0, 0xA5A5);
  st_db4->Set<uint32_t>(0, 0xDEADBEEF);
  st_db8->Set<uint64_t>(0, 0x0F0F0F0F'A5A5A5A5);
  // Perform stores to adjacent addresses in memory. They should not interfere.
  mem->Store(0x1000, st_db1);
  mem->Store(0x1001, st_db2);
  mem->Store(0x1002, st_db4);
  mem->Store(0x1003, st_db8);
  // Allocate data buffers for load data.
  DataBuffer* ld_db1 = arch_state_->db_factory()->Allocate<uint8_t>(1);
  DataBuffer* ld_db2 = arch_state_->db_factory()->Allocate<uint16_t>(1);
  DataBuffer* ld_db4 = arch_state_->db_factory()->Allocate<uint32_t>(1);
  DataBuffer* ld_db8 = arch_state_->db_factory()->Allocate<uint64_t>(1);
  // Perform loads from the adjacent addresses in memory.
  mem->Load(0x1000, ld_db1, nullptr, nullptr);
  mem->Load(0x1001, ld_db2, nullptr, nullptr);
  mem->Load(0x1002, ld_db4, nullptr, nullptr);
  mem->Load(0x1003, ld_db8, nullptr, nullptr);
  // Verify that the data loaded is the same as data stored.
  EXPECT_EQ(ld_db1->Get<uint8_t>(0), st_db1->Get<uint8_t>(0));
  EXPECT_EQ(ld_db2->Get<uint16_t>(0), st_db2->Get<uint16_t>(0));
  EXPECT_EQ(ld_db4->Get<uint32_t>(0), st_db4->Get<uint32_t>(0));
  EXPECT_EQ(ld_db8->Get<uint64_t>(0), st_db8->Get<uint64_t>(0));
  // Free up the data buffers.
  ld_db1->DecRef();
  ld_db2->DecRef();
  ld_db4->DecRef();
  ld_db8->DecRef();
  st_db1->DecRef();
  st_db2->DecRef();
  st_db4->DecRef();
  st_db8->DecRef();
}

}  // namespace
}  // namespace util
}  // namespace sim
}  // namespace mpact
