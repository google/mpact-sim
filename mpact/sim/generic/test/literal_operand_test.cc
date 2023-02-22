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

#include "mpact/sim/generic/literal_operand.h"

#include <memory>

#include "absl/memory/memory.h"
#include "googlemock/include/gmock/gmock.h"

namespace {

using ::mpact::sim::generic::BoolLiteralOperand;
using ::mpact::sim::generic::BoolLiteralPredicateOperand;
using ::mpact::sim::generic::IntLiteralOperand;

// Test BoolLiteralPredicateOperand<true>.
TEST(LiteralOperandTest, TrueBoolPredicateLiteral) {
  BoolLiteralPredicateOperand<true> pred;

  EXPECT_TRUE(pred.Value());
}

// Test BoolLiteralPredciateOperand<false>.
TEST(LiteralOperandTest, FalseBoolPredicateLiteral) {
  BoolLiteralPredicateOperand<false> pred;

  EXPECT_FALSE(pred.Value());
}

// Test BoolLiteralOperand<true> created from the default constructor.
TEST(LiteralOperandTest, TrueBoolLiteral) {
  auto operand = std::make_unique<BoolLiteralOperand<true>>();

  EXPECT_EQ(operand->shape().size(), 0);
  EXPECT_EQ(operand->GetObject().has_value(), false);
  EXPECT_EQ(operand->AsBool(0), true);
  EXPECT_EQ(operand->AsInt8(0), 1);
  EXPECT_EQ(operand->AsUint8(0), 1);
  EXPECT_EQ(operand->AsInt16(0), 1);
  EXPECT_EQ(operand->AsUint16(0), 1);
  EXPECT_EQ(operand->AsInt32(0), 1);
  EXPECT_EQ(operand->AsUint32(0), 1);
  EXPECT_EQ(operand->AsInt64(0), 1);
  EXPECT_EQ(operand->AsUint64(0), 1);
}

// Test BoolLiteralOperand<false> created from the simple constructor.
TEST(LiteralOperandTest, FalseBoolLiteral) {
  auto operand = std::make_unique<BoolLiteralOperand<false>>();

  EXPECT_EQ(operand->shape().size(), 0);
  EXPECT_EQ(operand->GetObject().has_value(), false);
  EXPECT_EQ(operand->AsBool(0), false);
  EXPECT_EQ(operand->AsInt8(0), 0);
  EXPECT_EQ(operand->AsUint8(0), 0);
  EXPECT_EQ(operand->AsInt16(0), 0);
  EXPECT_EQ(operand->AsUint16(0), 0);
  EXPECT_EQ(operand->AsInt32(0), 0);
  EXPECT_EQ(operand->AsUint32(0), 0);
  EXPECT_EQ(operand->AsInt64(0), 0);
  EXPECT_EQ(operand->AsUint64(0), 0);
}

// Test a "vector" immediate where shape is also given.
TEST(LiteralOperandTest, VectorBoolLiteral) {
  auto operand =
      std::make_unique<BoolLiteralOperand<true>>(std::vector<int>{128});

  EXPECT_EQ(operand->shape().size(), 1);
  EXPECT_THAT(operand->shape(), testing::ElementsAre(128));
  EXPECT_EQ(operand->GetObject().has_value(), false);

  for (int index = 0; index < 128; index += 16) {
    EXPECT_EQ(operand->AsBool(index), true);
    EXPECT_EQ(operand->AsInt8(index), 1);
    EXPECT_EQ(operand->AsUint8(index), 1);
    EXPECT_EQ(operand->AsInt16(index), 1);
    EXPECT_EQ(operand->AsUint16(index), 1);
    EXPECT_EQ(operand->AsInt32(index), 1);
    EXPECT_EQ(operand->AsUint32(index), 1);
    EXPECT_EQ(operand->AsInt64(index), 1);
    EXPECT_EQ(operand->AsUint64(index), 1);
  }
}

// Test LiteralOperand created from the simple constructor (scalar).
TEST(LiteralOperandTest, IntLiteral) {
  auto operand = std::make_unique<IntLiteralOperand<-123>>();

  EXPECT_EQ(operand->shape().size(), 0);
  EXPECT_EQ(operand->GetObject().has_value(), false);
  EXPECT_EQ(operand->AsBool(0), true);
  EXPECT_EQ(operand->AsInt8(0), -123);
  EXPECT_EQ(operand->AsUint8(0), 133);  // 2^8 - 123
  EXPECT_EQ(operand->AsInt16(0), -123);
  EXPECT_EQ(operand->AsUint16(0), 65413);  // 2^16 - 123
  EXPECT_EQ(operand->AsInt32(0), -123);
  EXPECT_EQ(operand->AsUint32(0), 4294967173);  // 2^32 - 123
  EXPECT_EQ(operand->AsInt64(0), -123);
  EXPECT_EQ(operand->AsUint64(0), 18446744073709551493U);  // 2^64 - 123
}

// Test that it always returns the same value even if index is outside
// that expected by the shape.
TEST(LiteralOperandTest, IntLiteralNonZeroIndex) {
  auto operand = std::make_unique<IntLiteralOperand<123>>();

  // The index doesn't matter.
  EXPECT_EQ(operand->AsBool(4), true);
  EXPECT_EQ(operand->AsInt8(4), 123);
  EXPECT_EQ(operand->AsUint8(4), 123);
  EXPECT_EQ(operand->AsInt16(4), 123);
  EXPECT_EQ(operand->AsUint16(4), 123);
  EXPECT_EQ(operand->AsInt32(4), 123);
  EXPECT_EQ(operand->AsUint32(4), 123);
  EXPECT_EQ(operand->AsInt64(4), 123);
  EXPECT_EQ(operand->AsUint64(4), 123);
}

// Test a "vector" literal where shape is also given.
TEST(LiteralOperandTest, VectorLiteral) {
  auto operand =
      std::make_unique<IntLiteralOperand<123>>(std::vector<int>{128});

  EXPECT_EQ(operand->shape().size(), 1);
  EXPECT_THAT(operand->shape(), testing::ElementsAre(128));
  EXPECT_EQ(operand->GetObject().has_value(), false);

  for (int index = 0; index < 128; index += 16) {
    EXPECT_EQ(operand->AsBool(index), true);
    EXPECT_EQ(operand->AsInt8(index), 123);
    EXPECT_EQ(operand->AsUint8(index), 123);
    EXPECT_EQ(operand->AsInt16(index), 123);
    EXPECT_EQ(operand->AsUint16(index), 123);
    EXPECT_EQ(operand->AsInt32(index), 123);
    EXPECT_EQ(operand->AsUint32(index), 123);
    EXPECT_EQ(operand->AsInt64(index), 123);
    EXPECT_EQ(operand->AsUint64(index), 123);
  }
}
}  // namespace
