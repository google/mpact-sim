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

#include "mpact/sim/generic/complex_resource_operand.h"

#include "absl/status/status.h"
#include "googlemock/include/gmock/gmock.h"
#include "googletest/include/gtest/gtest.h"

namespace {

using ::mpact::sim::generic::ArchState;
using ::mpact::sim::generic::ComplexResource;
using ::mpact::sim::generic::ComplexResourceOperand;
using ::mpact::sim::generic::SourceOperandInterface;

constexpr size_t kCycleDepth = 234;
constexpr size_t kLow = 100;
constexpr size_t kHigh = 107;
constexpr char kResourceName[] = "my_resource";
constexpr char kArchName[] = "test_architecture";

// Bits 100-107 are cleared in this bit vector.
constexpr uint64_t kFree100To107[] = {
    0xffff'ffff'ffff'ffff, 0xffff'f00f'ffff'ffff, 0xffff'ffff'ffff'ffff,
    0x0000'03ff'ffff'ffff};

// Longer than what is supported.
constexpr uint64_t kTooLong[5] = {0xffff, 0, 0, 0, 0};
// All zeros, no cycle is reserved.
constexpr uint64_t kAllZeros[4] = {0};
// The request vector corresponding to kFree100To107
constexpr uint64_t kAcquire100To107[] = {~kFree100To107[0], ~kFree100To107[1],
                                         ~kFree100To107[2],
                                         ~kFree100To107[3] & 0x3ff'ffff'ffff};

// ArchState derived class that is passed in to the resource (so that it can
// access the clock.
class MockArchState : public ArchState {
 public:
  MockArchState(absl::string_view id, SourceOperandInterface *pc_op)
      : ArchState(id, pc_op) {}
  explicit MockArchState(absl::string_view id) : MockArchState(id, nullptr) {}
  void set_cycle(uint64_t value) { ArchState::set_cycle(value); }
};

// Test fixture. Instantiates and deletes instances of MockArchState and the
// ComplexResource and ComplexResourceOperand.
class ComplexResourceOperandTest : public testing::Test {
 protected:
  ComplexResourceOperandTest() {
    arch_state_ = new MockArchState(kArchName);
    resource_ = new ComplexResource(arch_state_, kResourceName, kCycleDepth);
    operand_ = new ComplexResourceOperand(resource_);
  }

  ~ComplexResourceOperandTest() override {
    delete operand_;
    delete resource_;
    delete arch_state_;
  }

  MockArchState *arch_state_;
  ComplexResource *resource_;
  ComplexResourceOperand *operand_;
};

// Check error status from setting the cycle mask.
TEST_F(ComplexResourceOperandTest, CycleMask) {
  auto *op = new ComplexResourceOperand(nullptr);
  EXPECT_TRUE(absl::IsInternal(op->SetCycleMask(kLow, kHigh)));
  EXPECT_TRUE(absl::IsInvalidArgument(operand_->SetCycleMask(kHigh, kLow)));
  EXPECT_TRUE(
      absl::IsInvalidArgument(operand_->SetCycleMask(kLow, kCycleDepth)));
  EXPECT_TRUE(operand_->SetCycleMask(kLow, kHigh).ok());
  EXPECT_THAT(
      operand_->bit_array(),
      testing::ElementsAreArray(absl::MakeSpan(kAcquire100To107)
                                    .first(operand_->bit_array().size())));
  // Now try setting using arrays.
  EXPECT_TRUE(absl::IsInvalidArgument(operand_->SetCycleMask(kTooLong)));
  EXPECT_TRUE(absl::IsInvalidArgument(operand_->SetCycleMask(kAllZeros)));
  EXPECT_TRUE(operand_->SetCycleMask(kAcquire100To107).ok());
  delete op;
}

// Test IsFree function.
TEST_F(ComplexResourceOperandTest, IsFree) {
  // First set the resource to be busy except for cycles 100..107.
  resource_->Acquire(kFree100To107);
  // Initialize the cycle mask in the operand.
  EXPECT_TRUE(operand_->SetCycleMask(kLow, kHigh).ok());
  // Verify the bit array in the resource.
  EXPECT_THAT(resource_->bit_array(), testing::ElementsAreArray(kFree100To107));
  EXPECT_TRUE(operand_->IsFree());

  // Set the operand mask using the array itself.
  EXPECT_TRUE(operand_->SetCycleMask(kAcquire100To107).ok());
  EXPECT_THAT(operand_->bit_array(),
              testing::ElementsAreArray(kAcquire100To107));
  EXPECT_TRUE(operand_->IsFree());
  // Advance the cycle count (which shifts the resource bit vector at the next
  // call).
  arch_state_->set_cycle(1);
  // The call should fail now as a "busy" cycle has been shifted into the
  // request window.
  EXPECT_FALSE(operand_->IsFree());
}

// Test Acquire
TEST_F(ComplexResourceOperandTest, Acquire) {
  EXPECT_TRUE(operand_->SetCycleMask(kLow, kHigh).ok());
  operand_->Acquire();
  // Verify that the resource was acquired for the right cycles.
  EXPECT_THAT(resource_->bit_array(),
              testing::ElementsAreArray(kAcquire100To107));
}

}  // namespace
