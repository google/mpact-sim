// Copyright 2024 Google LLC
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

#include "mpact/sim/util/memory/tagged_flat_demand_memory.h"

#include <cstdint>
#include <cstring>
#include <memory>

#include "absl/log/log_sink_registry.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "googlemock/include/gmock/gmock.h"
#include "googletest/include/gtest/gtest.h"
#include "mpact/sim/generic/arch_state.h"
#include "mpact/sim/generic/data_buffer.h"
#include "mpact/sim/util/other/log_sink.h"

namespace {

using ::mpact::sim::generic::ArchState;
using ::mpact::sim::generic::DataBuffer;
using ::mpact::sim::util::LogSink;
using ::mpact::sim::util::TaggedFlatDemandMemory;

constexpr unsigned kTagGranule = 16;

// Define a class that derives from ArchState since constructors are
// protected.
class MyArchState : public ArchState {
 public:
  explicit MyArchState(absl::string_view id) : ArchState(id, nullptr) {}
};

class TaggedFlatDemandMemoryTest : public testing::Test {
 protected:
  TaggedFlatDemandMemoryTest() {
    arch_state_ = new MyArchState("TestArchitecture");
  }
  ~TaggedFlatDemandMemoryTest() override { delete arch_state_; }

  MyArchState *arch_state_;
};

// The first set of tests verify that the tagged flat demand memory works just
// like the flat demand memory class.

TEST_F(TaggedFlatDemandMemoryTest, BasicLoadStore) {
  auto mem = std::make_unique<TaggedFlatDemandMemory>(kTagGranule);
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

TEST_F(TaggedFlatDemandMemoryTest, SpanningLoadStore) {
  auto mem = std::make_unique<TaggedFlatDemandMemory>(kTagGranule);

  DataBuffer *st_db1 = arch_state_->db_factory()->Allocate<uint8_t>(1);
  DataBuffer *st_db2 = arch_state_->db_factory()->Allocate<uint16_t>(1);
  DataBuffer *st_db4 = arch_state_->db_factory()->Allocate<uint32_t>(1);
  DataBuffer *st_db8 = arch_state_->db_factory()->Allocate<uint64_t>(1);

  DataBuffer *ld_db1 = arch_state_->db_factory()->Allocate<uint8_t>(1);
  DataBuffer *ld_db2 = arch_state_->db_factory()->Allocate<uint16_t>(1);
  DataBuffer *ld_db4 = arch_state_->db_factory()->Allocate<uint32_t>(1);
  DataBuffer *ld_db8 = arch_state_->db_factory()->Allocate<uint64_t>(1);

  st_db1->Set<uint8_t>(0, 0x0F);
  st_db2->Set<uint16_t>(0, 0xA5A5);
  st_db4->Set<uint32_t>(0, 0xDEADBEEF);
  st_db8->Set<uint64_t>(0, 0x0F0F0F0F'A5A5A5A5);

  mem->Store(TaggedFlatDemandMemory::kAllocationSize - 4, st_db8);
  mem->Load(TaggedFlatDemandMemory::kAllocationSize - 4, ld_db8, nullptr,
            nullptr);
  mem->Store(TaggedFlatDemandMemory::kAllocationSize - 2, st_db4);
  mem->Load(TaggedFlatDemandMemory::kAllocationSize - 2, ld_db4, nullptr,
            nullptr);
  mem->Store(TaggedFlatDemandMemory::kAllocationSize - 1, st_db2);
  mem->Load(TaggedFlatDemandMemory::kAllocationSize - 1, ld_db2, nullptr,
            nullptr);
  mem->Store(TaggedFlatDemandMemory::kAllocationSize - 0, st_db1);
  mem->Load(TaggedFlatDemandMemory::kAllocationSize - 0, ld_db1, nullptr,
            nullptr);

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

TEST_F(TaggedFlatDemandMemoryTest, MultiLoadUnitStride) {
  auto mem =
      std::make_unique<TaggedFlatDemandMemory>(1024, 0x1000, 1, 0, kTagGranule);

  DataBuffer *address_db = arch_state_->db_factory()->Allocate<uint64_t>(1);
  DataBuffer *mask_db = arch_state_->db_factory()->Allocate<bool>(4);
  DataBuffer *ld_db = arch_state_->db_factory()->Allocate<uint32_t>(4);
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

TEST_F(TaggedFlatDemandMemoryTest, HalfWordAddressable) {
  auto mem = std::make_unique<TaggedFlatDemandMemory>(0x4000, 0x1000, 2, 0,
                                                      kTagGranule);
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

TEST_F(TaggedFlatDemandMemoryTest, LargeBlockOfMemory) {
  auto mem = std::make_unique<TaggedFlatDemandMemory>(kTagGranule);
  DataBuffer *ld_db = arch_state_->db_factory()->Allocate<uint8_t>(
      TaggedFlatDemandMemory::kAllocationSize * 2);
  DataBuffer *st_db = arch_state_->db_factory()->Allocate<uint8_t>(
      TaggedFlatDemandMemory::kAllocationSize * 2);
  // Set the store data to known value.
  std::memset(st_db->raw_ptr(), 0xbe, TaggedFlatDemandMemory::kAllocationSize);
  mem->Store(0x1234, st_db);
  // Set the load data buffer to a different value.
  std::memset(ld_db->raw_ptr(), 0xff, TaggedFlatDemandMemory::kAllocationSize);
  mem->Load(0x1234, ld_db, nullptr, nullptr);
  // Compare the values loaded to the store data.
  EXPECT_THAT(ld_db->Get<uint8_t>(),
              testing::ElementsAreArray(st_db->Get<uint8_t>()));

  ld_db->DecRef();
  st_db->DecRef();
}

// Make sure that
TEST_F(TaggedFlatDemandMemoryTest, UnalignedAddress) {
  LogSink log_sink;
  absl::AddLogSink(&log_sink);
  auto mem = std::make_unique<TaggedFlatDemandMemory>(kTagGranule);
  DataBuffer *data_db =
      arch_state_->db_factory()->Allocate<uint8_t>(kTagGranule * 16);
  DataBuffer *tag_db = arch_state_->db_factory()->Allocate<uint8_t>(16);
  int expected_err_count = 0;
  for (uint64_t address = 0x1000; address < 0x1010; address++) {
    // Only expect errors when the address is not aligned with kTagGranule.
    expected_err_count += (address & (kTagGranule - 1)) != 0;
    mem->Load(address, data_db, tag_db, nullptr, nullptr);
    EXPECT_EQ(log_sink.num_error(), expected_err_count);
    expected_err_count += (address & (kTagGranule - 1)) != 0;
    mem->Store(address, data_db, tag_db);
    EXPECT_EQ(log_sink.num_error(), expected_err_count);
  }
  data_db->DecRef();
  tag_db->DecRef();
  absl::RemoveLogSink(&log_sink);
}

TEST_F(TaggedFlatDemandMemoryTest, UnalignedSize) {
  LogSink log_sink;
  absl::AddLogSink(&log_sink);
  auto mem = std::make_unique<TaggedFlatDemandMemory>(kTagGranule);
  DataBuffer *data_db =
      arch_state_->db_factory()->Allocate<uint8_t>(kTagGranule * 16);
  DataBuffer *tag_db = arch_state_->db_factory()->Allocate<uint8_t>(16);
  DataBuffer *short_data_db =
      arch_state_->db_factory()->Allocate<uint8_t>(kTagGranule * 16 - 1);
  DataBuffer *long_data_db =
      arch_state_->db_factory()->Allocate<uint8_t>(kTagGranule * 16 + 1);

  // The correct data and tag db's will work.
  mem->Load(0x1000, data_db, tag_db, nullptr, nullptr);
  EXPECT_EQ(log_sink.num_error(), 0);
  // Too short will not work.
  mem->Load(0x1000, short_data_db, tag_db, nullptr, nullptr);
  EXPECT_EQ(log_sink.num_error(), 1);
  // Too long will not work.
  mem->Load(0x1000, long_data_db, tag_db, nullptr, nullptr);
  EXPECT_EQ(log_sink.num_error(), 2);

  data_db->DecRef();
  tag_db->DecRef();
  short_data_db->DecRef();
  long_data_db->DecRef();
  absl::RemoveLogSink(&log_sink);
}

TEST_F(TaggedFlatDemandMemoryTest, WrongTagSize) {
  LogSink log_sink;
  absl::AddLogSink(&log_sink);
  auto mem = std::make_unique<TaggedFlatDemandMemory>(kTagGranule);
  DataBuffer *data_db =
      arch_state_->db_factory()->Allocate<uint8_t>(kTagGranule * 16);
  DataBuffer *tag_db = arch_state_->db_factory()->Allocate<uint8_t>(16);
  DataBuffer *short_tag_db = arch_state_->db_factory()->Allocate<uint8_t>(8);
  DataBuffer *long_tag_db = arch_state_->db_factory()->Allocate<uint8_t>(18);

  // The correct data and tag db's will work.
  mem->Load(0x1000, data_db, tag_db, nullptr, nullptr);
  EXPECT_EQ(log_sink.num_error(), 0);
  // Too short will not work.
  mem->Load(0x1000, data_db, short_tag_db, nullptr, nullptr);
  EXPECT_EQ(log_sink.num_error(), 1);
  // Too long will not work.
  mem->Load(0x1000, data_db, long_tag_db, nullptr, nullptr);
  EXPECT_EQ(log_sink.num_error(), 2);

  data_db->DecRef();
  tag_db->DecRef();
  short_tag_db->DecRef();
  long_tag_db->DecRef();
  absl::RemoveLogSink(&log_sink);
}

TEST_F(TaggedFlatDemandMemoryTest, TaggedLoadStore) {
  auto mem = std::make_unique<TaggedFlatDemandMemory>(kTagGranule);
  DataBuffer *ld_data_db =
      arch_state_->db_factory()->Allocate<uint8_t>(kTagGranule * 16);
  DataBuffer *ld_tag_db = arch_state_->db_factory()->Allocate<uint8_t>(16);
  DataBuffer *st_data_db =
      arch_state_->db_factory()->Allocate<uint8_t>(kTagGranule * 16);
  DataBuffer *st_tag_db = arch_state_->db_factory()->Allocate<uint8_t>(16);
  mem->Load(0x1000, ld_data_db, ld_tag_db, nullptr, nullptr);
  // The loaded data should be all zeros.
  for (int i = 0; i < 16; i++) {
    for (int j = 0; j < kTagGranule; j++) {
      EXPECT_EQ(ld_data_db->Get<uint8_t>(i * kTagGranule + j), 0);
    }
    EXPECT_EQ(ld_tag_db->Get<uint8_t>(i), 0);
  }
  // Write out known data.
  for (int i = 0; i < st_data_db->size<uint8_t>(); i++) {
    st_data_db->Set<uint8_t>(i, i);
  }
  for (int i = 0; i < st_tag_db->size<uint8_t>(); i++) {
    st_tag_db->Set<uint8_t>(i, 1);
  }
  mem->Store(0x1000, st_data_db, st_tag_db);
  // Verify that the loaded data is equal to the stored data.
  mem->Load(0x1000, ld_data_db, ld_tag_db, nullptr, nullptr);
  for (int i = 0; i < ld_data_db->size<uint8_t>(); i++) {
    EXPECT_EQ(ld_data_db->Get<uint8_t>(i), i);
  }
  for (int i = 0; i < ld_tag_db->size<uint8_t>(); i++) {
    EXPECT_EQ(ld_tag_db->Get<uint8_t>(i), 1);
  }
  // Clear every third tag and store them.
  for (int i = 0; i < st_tag_db->size<uint8_t>(); i++) {
    if (i % 3 == 0) st_tag_db->Set<uint8_t>(i, 0);
  }
  mem->Store(0x1000, st_data_db, st_tag_db);
  // Re-load and compare.
  mem->Load(0x1000, ld_data_db, ld_tag_db, nullptr, nullptr);
  for (int i = 0; i < ld_data_db->size<uint8_t>(); i++) {
    EXPECT_EQ(ld_data_db->Get<uint8_t>(i), i);
  }
  for (int i = 0; i < ld_tag_db->size<uint8_t>(); i++) {
    EXPECT_EQ(ld_tag_db->Get<uint8_t>(i), i % 3 == 0 ? 0 : 1) << i;
  }
  ld_data_db->DecRef();
  ld_tag_db->DecRef();
  st_data_db->DecRef();
  st_tag_db->DecRef();
}

TEST_F(TaggedFlatDemandMemoryTest, ClearTags) {
  LogSink log_sink;
  absl::AddLogSink(&log_sink);
  auto mem = std::make_unique<TaggedFlatDemandMemory>(kTagGranule);
  DataBuffer *ld_data_db =
      arch_state_->db_factory()->Allocate<uint8_t>(kTagGranule * 16);
  DataBuffer *ld_tag_db = arch_state_->db_factory()->Allocate<uint8_t>(16);
  DataBuffer *st_data_db =
      arch_state_->db_factory()->Allocate<uint8_t>(kTagGranule * 16);
  DataBuffer *st_tag_db = arch_state_->db_factory()->Allocate<uint8_t>(16);
  // Write tagged data to memory.
  for (int i = 0; i < st_data_db->size<uint8_t>(); i++) {
    st_data_db->Set<uint8_t>(i, i);
  }
  for (int i = 0; i < st_tag_db->size<uint8_t>(); i++) {
    st_tag_db->Set<uint8_t>(i, 1);
  }
  mem->Store(0x1000, st_data_db, st_tag_db);
  // Load the tags, verify that they are set.
  mem->Load(0x1000, nullptr, ld_tag_db, nullptr, nullptr);
  for (int i = 0; i < ld_tag_db->size<uint8_t>(); i++) {
    EXPECT_EQ(ld_tag_db->Get<uint8_t>(i), 1);
  }
  // Clear tags with non-tagged stores.
  DataBuffer *st_untagged_db = arch_state_->db_factory()->Allocate<uint8_t>(1);
  for (int i = 0; i < st_data_db->size<uint8_t>(); i++) {
    st_untagged_db->Set<uint8_t>(0, st_data_db->size<uint8_t>() - i);
    mem->Store(0x1000 + i, st_untagged_db);
    mem->Load(0x1000, nullptr, ld_tag_db, nullptr, nullptr);
    // Verify that the store cleared a tag at each time it crosses the granule
    // boundary.
    for (int j = 0; j < ld_tag_db->size<uint8_t>(); j++) {
      int granule_indx = i / kTagGranule;
      EXPECT_EQ(ld_tag_db->Get<uint8_t>(j), j <= granule_indx ? 0 : 1)
          << absl::StrCat("i: ", i, ", j: ", j);
    }
  }
  // Clean up.
  ld_data_db->DecRef();
  ld_tag_db->DecRef();
  st_data_db->DecRef();
  st_tag_db->DecRef();
  st_untagged_db->DecRef();
  absl::RemoveLogSink(&log_sink);
}

}  // namespace
