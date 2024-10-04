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

#include "mpact/sim/util/memory/flat_demand_memory.h"

#include <cstdint>
#include <cstring>
#include <memory>

#include "absl/strings/string_view.h"
#include "googlemock/include/gmock/gmock.h"
#include "googletest/include/gtest/gtest.h"
#include "mpact/sim/generic/arch_state.h"
#include "mpact/sim/generic/data_buffer.h"

namespace {

using ::mpact::sim::generic::ArchState;
using ::mpact::sim::generic::DataBuffer;
using ::mpact::sim::util::FlatDemandMemory;

// Define a class that derives from ArchState since constructors are
// protected.
class MyArchState : public ArchState {
 public:
  explicit MyArchState(absl::string_view id) : ArchState(id, nullptr) {}
};

class FlatDemandMemoryTest : public testing::Test {
 protected:
  FlatDemandMemoryTest() { arch_state_ = new MyArchState("TestArchitecture"); }
  ~FlatDemandMemoryTest() override { delete arch_state_; }

  MyArchState *arch_state_;
};

TEST_F(FlatDemandMemoryTest, BasicLoadStore) {
  auto mem = std::make_unique<FlatDemandMemory>();
  DataBuffer *st_db1 = arch_state_->db_factory()->Allocate<uint8_t>(1);
  DataBuffer *st_db2 = arch_state_->db_factory()->Allocate<uint16_t>(1);
  DataBuffer *st_db4 = arch_state_->db_factory()->Allocate<uint32_t>(1);
  DataBuffer *st_db8 = arch_state_->db_factory()->Allocate<uint64_t>(1);

  st_db1->Set<uint8_t>(0, 0x0F);
  st_db2->Set<uint16_t>(0, 0xA5A5);
  st_db4->Set<uint32_t>(0, 0xDEADBEEF);
  st_db8->Set<uint64_t>(0, 0x0F0F0F0F'A5A5A5A5);

  mem->Store(0x1000, st_db1);
  mem->Store(0x1002, st_db2);
  mem->Store(0x1004, st_db4);
  mem->Store(0x1008, st_db8);

  DataBuffer *ld_db1 = arch_state_->db_factory()->Allocate<uint8_t>(1);
  DataBuffer *ld_db2 = arch_state_->db_factory()->Allocate<uint16_t>(1);
  DataBuffer *ld_db4 = arch_state_->db_factory()->Allocate<uint32_t>(1);
  DataBuffer *ld_db8 = arch_state_->db_factory()->Allocate<uint64_t>(1);
  ld_db1->set_latency(0);
  ld_db2->set_latency(0);
  ld_db4->set_latency(0);
  ld_db8->set_latency(0);

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

TEST_F(FlatDemandMemoryTest, SpanningLoadStore) {
  auto mem = std::make_unique<FlatDemandMemory>();

  DataBuffer *st_db1 = arch_state_->db_factory()->Allocate<uint8_t>(1);
  DataBuffer *st_db2 = arch_state_->db_factory()->Allocate<uint16_t>(1);
  DataBuffer *st_db4 = arch_state_->db_factory()->Allocate<uint32_t>(1);
  DataBuffer *st_db8 = arch_state_->db_factory()->Allocate<uint64_t>(1);

  DataBuffer *ld_db1 = arch_state_->db_factory()->Allocate<uint8_t>(1);
  DataBuffer *ld_db2 = arch_state_->db_factory()->Allocate<uint16_t>(1);
  DataBuffer *ld_db4 = arch_state_->db_factory()->Allocate<uint32_t>(1);
  DataBuffer *ld_db8 = arch_state_->db_factory()->Allocate<uint64_t>(1);
  ld_db1->set_latency(0);
  ld_db2->set_latency(0);
  ld_db4->set_latency(0);
  ld_db8->set_latency(0);

  st_db1->Set<uint8_t>(0, 0x0F);
  st_db2->Set<uint16_t>(0, 0xA5A5);
  st_db4->Set<uint32_t>(0, 0xDEADBEEF);
  st_db8->Set<uint64_t>(0, 0x0F0F0F0F'A5A5A5A5);

  mem->Store(FlatDemandMemory::kAllocationSize - 4, st_db8);
  mem->Load(FlatDemandMemory::kAllocationSize - 4, ld_db8, nullptr, nullptr);
  mem->Store(FlatDemandMemory::kAllocationSize - 2, st_db4);
  mem->Load(FlatDemandMemory::kAllocationSize - 2, ld_db4, nullptr, nullptr);
  mem->Store(FlatDemandMemory::kAllocationSize - 1, st_db2);
  mem->Load(FlatDemandMemory::kAllocationSize - 1, ld_db2, nullptr, nullptr);
  mem->Store(FlatDemandMemory::kAllocationSize - 0, st_db1);
  mem->Load(FlatDemandMemory::kAllocationSize - 0, ld_db1, nullptr, nullptr);

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

TEST_F(FlatDemandMemoryTest, MultiLoadUnitStride) {
  auto mem = std::make_unique<FlatDemandMemory>(1024, 0x1000, 1, 0);

  DataBuffer *address_db = arch_state_->db_factory()->Allocate<uint64_t>(1);
  DataBuffer *mask_db = arch_state_->db_factory()->Allocate<bool>(4);
  DataBuffer *ld_db = arch_state_->db_factory()->Allocate<uint32_t>(4);
  ld_db->set_latency(0);
  DataBuffer *st_db = arch_state_->db_factory()->Allocate<uint32_t>(4);
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

TEST_F(FlatDemandMemoryTest, HalfWordAddressable) {
  auto mem = std::make_unique<FlatDemandMemory>(0x4000, 0x1000, 2, 0);
  DataBuffer *st_db2 = arch_state_->db_factory()->Allocate<uint16_t>(1);
  DataBuffer *st_db4 = arch_state_->db_factory()->Allocate<uint32_t>(1);
  DataBuffer *st_db8 = arch_state_->db_factory()->Allocate<uint64_t>(1);

  st_db2->Set<uint16_t>(0, 0xA5A5);
  st_db4->Set<uint32_t>(0, 0xDEADBEEF);
  st_db8->Set<uint64_t>(0, 0x0F0F0F0F'A5A5A5A5);

  mem->Store(0x1000, st_db2);
  mem->Store(0x1001, st_db4);
  mem->Store(0x1003, st_db8);

  DataBuffer *ld_db2 = arch_state_->db_factory()->Allocate<uint16_t>(1);
  DataBuffer *ld_db4 = arch_state_->db_factory()->Allocate<uint32_t>(1);
  DataBuffer *ld_db8 = arch_state_->db_factory()->Allocate<uint64_t>(1);
  ld_db2->set_latency(0);
  ld_db4->set_latency(0);
  ld_db8->set_latency(0);

  mem->Load(0x1000, ld_db2, nullptr, nullptr);
  mem->Load(0x1001, ld_db4, nullptr, nullptr);
  mem->Load(0x1003, ld_db8, nullptr, nullptr);

  EXPECT_EQ(ld_db2->Get<uint16_t>(0), st_db2->Get<uint16_t>(0));
  EXPECT_EQ(ld_db4->Get<uint32_t>(0), st_db4->Get<uint32_t>(0));
  EXPECT_EQ(ld_db8->Get<uint64_t>(0), st_db8->Get<uint64_t>(0));

  ld_db2->DecRef();
  ld_db4->DecRef();
  ld_db8->DecRef();

  st_db2->DecRef();
  st_db4->DecRef();
  st_db8->DecRef();
}

TEST_F(FlatDemandMemoryTest, LargeBlockOfMemory) {
  auto mem = std::make_unique<FlatDemandMemory>();
  DataBuffer *ld_db = arch_state_->db_factory()->Allocate<uint8_t>(
      FlatDemandMemory::kAllocationSize * 2);
  ld_db->set_latency(0);
  DataBuffer *st_db = arch_state_->db_factory()->Allocate<uint8_t>(
      FlatDemandMemory::kAllocationSize * 2);
  // Set the store data to known value.
  std::memset(st_db->raw_ptr(), 0xbe, FlatDemandMemory::kAllocationSize * 2);
  mem->Store(0x1234, st_db);
  // Set the load data buffer to a different value.
  std::memset(ld_db->raw_ptr(), 0xff, FlatDemandMemory::kAllocationSize * 2);
  mem->Load(0x1234, ld_db, nullptr, nullptr);
  // Compare the values loaded to the store data.
  EXPECT_THAT(ld_db->Get<uint8_t>(),
              testing::ElementsAreArray(st_db->Get<uint8_t>()));

  ld_db->DecRef();
  st_db->DecRef();
}

}  // namespace
