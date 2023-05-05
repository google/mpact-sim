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

#include "mpact/sim/generic/complex_resource.h"

#include <ostream>

#include "googlemock/include/gmock/gmock.h"
#include "googletest/include/gtest/gtest.h"
#include "mpact/sim/generic/arch_state.h"
#include "mpact/sim/generic/operand_interface.h"

namespace {

using ::mpact::sim::generic::ArchState;
using ::mpact::sim::generic::ComplexResource;
using ::mpact::sim::generic::SourceOperandInterface;

constexpr int kShiftLimit = 64;
constexpr int kShiftMask = 63;
constexpr int kWindow256 = 256;
constexpr size_t kCycleDepth = 234;
constexpr char kResourceName[] = "my_resource";
uint64_t kAllOnes234[] = {0xffff'ffff'ffff'ffff, 0xffff'ffff'ffff'ffff,
                          0xffff'ffff'ffff'ffff, 0xffff'ffff'ffc0'0000};
uint64_t kAllOnes256[] = {0xffff'ffff'ffff'ffff, 0xffff'ffff'ffff'ffff,
                          0xffff'ffff'ffff'ffff, 0xffff'ffff'ffff'ffff};
uint64_t kAllOnes64[] = {0xffff'ffff'ffff'ffff, 0, 0, 0};
uint64_t kAllOnes96[] = {0xffff'ffff'ffff'ffff, 0, 0, 0};

class MockArchState : public ArchState {
 public:
  MockArchState(absl::string_view id, SourceOperandInterface *pc_op)
      : ArchState(id, pc_op) {}
  explicit MockArchState(absl::string_view id) : MockArchState(id, nullptr) {}
  void set_cycle(uint64_t value) { ArchState::set_cycle(value); }
};

class ComplexResourceTest : public testing::Test {
 protected:
  ComplexResourceTest() { arch_state_ = new MockArchState("TestArchitecture"); }

  ~ComplexResourceTest() override { delete arch_state_; }

  MockArchState *arch_state_;
};

TEST_F(ComplexResourceTest, Construct) {
  auto *resource = new ComplexResource(arch_state_, kResourceName, kCycleDepth);
  EXPECT_EQ(resource->bit_array().size(), (kCycleDepth + 63) / 64);
  EXPECT_EQ(resource->name(), kResourceName);
  EXPECT_EQ(resource->AsString(), kResourceName);
  delete resource;
}

// Verify that all bits are free to start out with.
TEST_F(ComplexResourceTest, IsFreeMarchingOne) {
  auto *resource = new ComplexResource(arch_state_, kResourceName, kCycleDepth);
  uint64_t marching_one[4] = {0x8000'0000'0000'0000, 0, 0, 0};
  for (size_t i = 0; i < kCycleDepth; i++) {
    EXPECT_TRUE(resource->IsFree(marching_one)) << i;
    uint64_t prev_bit = 0;
    for (int j = 0; j < 4; j++) {
      uint64_t bit = (marching_one[j] >> 63) & 0x1;
      marching_one[j] >>= 1;
      marching_one[j] |= prev_bit;
      prev_bit = bit;
    }
  }
  delete resource;
}

// Reserve all bits, verify that they are set.
TEST_F(ComplexResourceTest, IsBusyMarchingOne) {
  auto *resource = new ComplexResource(arch_state_, kResourceName, kCycleDepth);
  resource->Acquire(kAllOnes234);
  uint64_t marching_one[4] = {0x8000'0000'0000'0000, 0, 0, 0};
  for (size_t i = 0; i < kCycleDepth; i++) {
    EXPECT_FALSE(resource->IsFree(marching_one)) << i;
    uint64_t prev_bit = 0;
    for (int j = 0; j < 4; j++) {
      uint64_t bit = (marching_one[j]) & 0x1;
      marching_one[j] >>= 1;
      marching_one[j] |= (prev_bit << 63);
      prev_bit = bit;
    }
  }
  delete resource;
}

// Acquire the resource for all the cycles. Then check if it is available and
// try to acquire it. Release the resource for that cycle, then try again.
TEST_F(ComplexResourceTest, AcquireRelease) {
  auto *resource = new ComplexResource(arch_state_, kResourceName, kCycleDepth);
  resource->Acquire(kAllOnes234);
  uint64_t marching_one[4] = {0x8000'0000'0000'0000, 0, 0, 0};
  for (size_t i = 0; i < kCycleDepth; i++) {
    EXPECT_FALSE(resource->IsFree(marching_one)) << i;
    resource->Release(marching_one);
    EXPECT_TRUE(resource->IsFree(marching_one));
    resource->Acquire(marching_one);
    uint64_t prev_bit = 0;
    for (int j = 0; j < 4; j++) {
      uint64_t bit = (marching_one[j]) & 0x1;
      marching_one[j] >>= 1;
      marching_one[j] |= (prev_bit << 63);
      prev_bit = bit;
    }
  }
  delete resource;
}

// Resource with 64 cycle window.
// Acquires the resource for all cycles. Then advances the clock and checks if
// the resource is free in the last cycle, next to last cycle, etc.
TEST_F(ComplexResourceTest, SingleWordBy1) {
  auto *resource = new ComplexResource(arch_state_, kResourceName, 64);
  EXPECT_TRUE(resource->IsFree(kAllOnes234));
  resource->Acquire(kAllOnes234);
  EXPECT_FALSE(resource->IsFree(kAllOnes234));
  uint64_t mask_array[1] = {0};
  uint64_t mask = 0xffff'ffff'ffff'ffff;
  // Increment cycle by 1.
  int cycle = 0;
  for (cycle = 1; cycle < kShiftLimit; cycle++) {
    mask_array[0] = mask << (kShiftLimit - cycle);
    EXPECT_FALSE(resource->IsFree(absl::Span<uint64_t>(mask_array)))
        << absl::StrCat(
               cycle, ": mask_array[0] = ", absl::Hex(mask_array[0]), "\n",
               cycle, ": bit_array[0]  = ", absl::Hex(resource->bit_array()[0]))
        << std::endl;
    arch_state_->set_cycle(cycle);
    EXPECT_TRUE(resource->IsFree(absl::Span<uint64_t>(mask_array)))
        << absl::StrCat(
               cycle, ": mask_array[0] = ", absl::Hex(mask_array[0]), "\n",
               cycle, ": bit_array[0]  = ", absl::Hex(resource->bit_array()[0]))
        << std::endl;
  }
  arch_state_->set_cycle(cycle);
  EXPECT_TRUE(resource->IsFree(kAllOnes234)) << cycle;
  delete resource;
}

// Same as above, but advanced the clock by 3.
TEST_F(ComplexResourceTest, SingleWordBy3) {
  auto *resource = new ComplexResource(arch_state_, kResourceName, 64);
  EXPECT_TRUE(resource->IsFree(kAllOnes234));
  resource->Acquire(kAllOnes234);
  EXPECT_FALSE(resource->IsFree(kAllOnes234));
  uint64_t mask_array[1] = {0};
  uint64_t mask = 0xffff'ffff'ffff'ffff;
  // Increment cycle by 3.
  int cycle = 0;
  for (cycle = 1; cycle < kShiftLimit; cycle += 3) {
    mask_array[0] = mask << (kShiftLimit - cycle);
    EXPECT_FALSE(resource->IsFree(absl::Span<uint64_t>(mask_array)))
        << absl::StrCat(
               cycle, ": mask_array[0] = ", absl::Hex(mask_array[0]), "\n",
               cycle, ": bit_array[0]  = ", absl::Hex(resource->bit_array()[0]))
        << std::endl;
    arch_state_->set_cycle(cycle);
    EXPECT_TRUE(resource->IsFree(absl::Span<uint64_t>(mask_array)))
        << absl::StrCat(
               cycle, ": mask_array[0] = ", absl::Hex(mask_array[0]), "\n",
               cycle, ": bit_array[0]  = ", absl::Hex(resource->bit_array()[0]))
        << std::endl;
  }
  arch_state_->set_cycle(cycle);
  EXPECT_TRUE(resource->IsFree(kAllOnes234)) << cycle;
  delete resource;
}

// Resource with 256 cycle window.
// Acquires the resource for all cycles. Then advances the clock and checks if
// the resource is free in the last cycle, next to last cycle, etc.
TEST_F(ComplexResourceTest, QuadWordBy1) {
  auto *resource = new ComplexResource(arch_state_, kResourceName, 256);
  EXPECT_TRUE(resource->IsFree(kAllOnes256));
  resource->Acquire(kAllOnes256);
  EXPECT_FALSE(resource->IsFree(kAllOnes256));
  uint64_t mask_array[4] = {0};
  uint64_t mask = 0xffff'ffff'ffff'ffff;
  // Increment cycle by 1.
  int cycle = 0;
  for (cycle = 1; cycle < kWindow256; cycle++) {
    int index = (kWindow256 - cycle) / kShiftLimit;
    int shift_amount = kShiftLimit - (cycle & kShiftMask);
    if (shift_amount == kShiftLimit) shift_amount = 0;
    mask_array[index] = mask << shift_amount;
    EXPECT_FALSE(resource->IsFree(absl::Span<uint64_t>(mask_array)))
        << absl::StrCat(cycle, ": mask_array[", index,
                        "] = ", absl::Hex(mask_array[index]), "\n", cycle,
                        ": bit_array[", index,
                        "]  = ", absl::Hex(resource->bit_array()[index]))
        << std::endl;
    arch_state_->set_cycle(cycle);
    EXPECT_TRUE(resource->IsFree(absl::Span<uint64_t>(mask_array)))
        << absl::StrCat(cycle, ": mask_array[", index,
                        "] = ", absl::Hex(mask_array[index]), "\n", cycle,
                        ": bit_array[", index,
                        "]  = ", absl::Hex(resource->bit_array()[index]))
        << std::endl;
  }
  arch_state_->set_cycle(cycle);
  EXPECT_TRUE(resource->IsFree(kAllOnes234));
  delete resource;
}

// Same as above, but advances the clock by 7 cycles at a time.
TEST_F(ComplexResourceTest, QuadWordBy5) {
  auto *resource = new ComplexResource(arch_state_, kResourceName, 256);
  EXPECT_TRUE(resource->IsFree(kAllOnes256));
  resource->Acquire(kAllOnes256);
  EXPECT_FALSE(resource->IsFree(kAllOnes256));
  uint64_t mask_array[4] = {0};
  uint64_t mask = 0xffff'ffff'ffff'ffff;
  // Increment cycle by 7.
  int cycle = 0;
  for (cycle = 1; cycle < kWindow256; cycle += 7) {
    int index = (kWindow256 - cycle) / kShiftLimit;
    int shift_amount = kShiftLimit - (cycle & kShiftMask);
    if (shift_amount == kShiftLimit) shift_amount = 0;
    mask_array[index] = mask << shift_amount;
    EXPECT_FALSE(resource->IsFree(absl::Span<uint64_t>(mask_array)))
        << absl::StrCat(cycle, ": mask_array[", index,
                        "] = ", absl::Hex(mask_array[index]), "\n", cycle,
                        ": bit_array[", index,
                        "]  = ", absl::Hex(resource->bit_array()[index]))
        << std::endl;
    arch_state_->set_cycle(cycle);
    EXPECT_TRUE(resource->IsFree(absl::Span<uint64_t>(mask_array)))
        << absl::StrCat(cycle, ": mask_array[", index,
                        "] = ", absl::Hex(mask_array[index]), "\n", cycle,
                        ": bit_array[", index,
                        "]  = ", absl::Hex(resource->bit_array()[index]))
        << std::endl;
  }
  arch_state_->set_cycle(cycle);
  EXPECT_TRUE(resource->IsFree(kAllOnes256));
  delete resource;
}

// Check that advancing clock by more than 256 yields free resource.
TEST_F(ComplexResourceTest, ShiftGreaterThan256) {
  auto *resource = new ComplexResource(arch_state_, kResourceName, 256);
  EXPECT_TRUE(resource->IsFree(kAllOnes256));
  resource->Acquire(kAllOnes256);
  EXPECT_FALSE(resource->IsFree(kAllOnes256));
  arch_state_->set_cycle(300);
  EXPECT_TRUE(resource->IsFree(kAllOnes256));
  delete resource;
}

// Verify that shifts over 64 bits work.
TEST_F(ComplexResourceTest, ShiftGreaterThan64) {
  auto *resource = new ComplexResource(arch_state_, kResourceName, 256);
  EXPECT_TRUE(resource->IsFree(kAllOnes256));
  resource->Acquire(kAllOnes256);
  EXPECT_FALSE(resource->IsFree(kAllOnes64));
  arch_state_->set_cycle(96);
  EXPECT_TRUE(resource->IsFree(kAllOnes96));
  delete resource;
}

}  // namespace
