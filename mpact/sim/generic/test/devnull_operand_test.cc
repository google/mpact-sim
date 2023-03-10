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

#include "mpact/sim/generic/devnull_operand.h"

#include <cstdint>
#include <memory>

#include "absl/memory/memory.h"
#include "googlemock/include/gmock/gmock.h"
#include "googletest/include/gtest/gtest.h"
#include "mpact/sim/generic/arch_state.h"
#include "mpact/sim/generic/data_buffer.h"
#include "mpact/sim/generic/operand_interface.h"

namespace {

using ::mpact::sim::generic::ArchState;
using ::mpact::sim::generic::DevNullOperand;
using ::mpact::sim::generic::SourceOperandInterface;

class MockArchState : public ArchState {
 public:
  MockArchState(absl::string_view id, SourceOperandInterface *pc_op)
      : ArchState(id, pc_op) {}
  explicit MockArchState(absl::string_view id) : MockArchState(id, nullptr) {}
};

class DevNullOperandTest : public testing::Test {
 protected:
  DevNullOperandTest() { arch_state_ = new MockArchState("MockArchState"); }
  ~DevNullOperandTest() override { delete arch_state_; }

  MockArchState *arch_state_;
};

// Testing that the operand behaves like a normal destination, that the
// data buffer can be allocated or copied, and then submitted, with no errors.

TEST_F(DevNullOperandTest, ScalarDevNull) {
  auto operand = std::make_unique<DevNullOperand<uint32_t>>(
      arch_state_, std::vector<int>{1});
  EXPECT_EQ(operand->shape().size(), 1);
  EXPECT_EQ(operand->shape()[0], 1);
  auto db = operand->AllocateDataBuffer();
  EXPECT_EQ(db->size<uint32_t>(), operand->shape()[0]);
  db->Submit();
  db = operand->CopyDataBuffer();
  EXPECT_EQ(db->size<uint32_t>(), operand->shape()[0]);
  db->Submit();
}

TEST_F(DevNullOperandTest, VectorDevNull) {
  auto operand = std::make_unique<DevNullOperand<uint32_t>>(
      arch_state_, std::vector<int>{8});
  EXPECT_EQ(operand->shape().size(), 1);
  EXPECT_EQ(operand->shape()[0], 8);
  auto db = operand->AllocateDataBuffer();
  EXPECT_EQ(db->size<uint32_t>(), operand->shape()[0]);
  db->Submit();
  db = operand->CopyDataBuffer();
  EXPECT_EQ(db->size<uint32_t>(), operand->shape()[0]);
  db->Submit();
}

}  // namespace
