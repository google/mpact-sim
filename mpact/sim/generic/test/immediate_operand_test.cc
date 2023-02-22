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

#include "mpact/sim/generic/immediate_operand.h"

#include <cstdint>
#include <limits>
#include <memory>
#include <vector>

#include "absl/memory/memory.h"
#include "googlemock/include/gmock/gmock.h"

namespace mpact {
namespace sim {
namespace generic {
namespace {

// Test ImmediateOperand created from the simple constructor (scalar).
TEST(ImmediateOperandTest, ScalarImmediate) {
  auto operand = std::make_unique<ImmediateOperand<uint32_t>>(
      std::numeric_limits<uint32_t>::max());

  EXPECT_EQ(operand->shape().size(), 1);
  EXPECT_EQ(operand->shape()[0], 1);
  EXPECT_FALSE(operand->GetObject().has_value());
  EXPECT_EQ(operand->AsBool(0), true);
  EXPECT_EQ(operand->AsInt8(0), -1);
  EXPECT_EQ(operand->AsUint8(0), std::numeric_limits<uint8_t>::max());
  EXPECT_EQ(operand->AsInt16(0), -1);
  EXPECT_EQ(operand->AsUint16(0), std::numeric_limits<uint16_t>::max());
  EXPECT_EQ(operand->AsInt32(0), -1);
  EXPECT_EQ(operand->AsUint32(0), std::numeric_limits<uint32_t>::max());
  EXPECT_EQ(operand->AsInt64(0), std::numeric_limits<uint32_t>::max());
  EXPECT_EQ(operand->AsUint64(0), std::numeric_limits<uint32_t>::max());
}

// Test that it always returns the same value even if index is outside
// that expected by the shape.
TEST(ImmediateOperandTest, ScalarImmediateNonZeroIndex) {
  auto operand = std::make_unique<ImmediateOperand<uint32_t>>(
      std::numeric_limits<uint32_t>::max());

  // The index doesn't matter.
  EXPECT_EQ(operand->AsBool(4), true);
  EXPECT_EQ(operand->AsInt8(4), -1);
  EXPECT_EQ(operand->AsUint8(4), std::numeric_limits<uint8_t>::max());
  EXPECT_EQ(operand->AsInt16(4), -1);
  EXPECT_EQ(operand->AsUint16(4), std::numeric_limits<uint16_t>::max());
  EXPECT_EQ(operand->AsInt32(4), -1);
  EXPECT_EQ(operand->AsUint32(4), std::numeric_limits<uint32_t>::max());
  EXPECT_EQ(operand->AsInt64(4), std::numeric_limits<uint32_t>::max());
  EXPECT_EQ(operand->AsUint64(4), std::numeric_limits<uint32_t>::max());
}

// Test a "vector" immediate where shape is also given.
TEST(ImmediateOperandTest, VectorShapedImmediate) {
  auto operand = std::make_unique<ImmediateOperand<uint32_t>>(
      std::numeric_limits<uint32_t>::max(), std::vector<int>{128});

  EXPECT_EQ(operand->shape().size(), 1);
  EXPECT_EQ(operand->shape()[0], 128);
  EXPECT_FALSE(operand->GetObject().has_value());

  for (int index = 0; index < 128; index += 16) {
    EXPECT_EQ(operand->AsBool(index), true);
    EXPECT_EQ(operand->AsInt8(index), -1);
    EXPECT_EQ(operand->AsUint8(index), std::numeric_limits<uint8_t>::max());
    EXPECT_EQ(operand->AsInt16(index), -1);
    EXPECT_EQ(operand->AsUint16(index), std::numeric_limits<uint16_t>::max());
    EXPECT_EQ(operand->AsInt32(index), -1);
    EXPECT_EQ(operand->AsUint32(index), std::numeric_limits<uint32_t>::max());
    EXPECT_EQ(operand->AsInt64(index), std::numeric_limits<uint32_t>::max());
    EXPECT_EQ(operand->AsUint64(index), std::numeric_limits<uint32_t>::max());
  }
}

TEST(ImmediateOperandTest, VectorValuedImmediate) {
  std::vector<uint32_t> values = {0, 1, 2, 3, 4, 5, 6, 7};
  auto operand = std::make_unique<VectorImmediateOperand<uint32_t>>(values);
  EXPECT_EQ(operand->shape().size(), 1);
  EXPECT_EQ(operand->shape()[0], values.size());
  EXPECT_FALSE(operand->GetObject().has_value());
  for (size_t i = 0; i < values.size(); i++) {
    EXPECT_EQ(operand->AsUint32(i), values[i]);
  }
}

}  // namespace
}  // namespace generic
}  // namespace sim
}  // namespace mpact
