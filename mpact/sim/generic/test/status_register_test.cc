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

#include "mpact/sim/generic/status_register.h"

#include "googlemock/include/gmock/gmock.h"
#include "googletest/include/gtest/gtest.h"

namespace {

using ::mpact::sim::generic::StatusRegister;
using ::mpact::sim::generic::StatusRegisterSourceOperand;

// Using bit number 3 as the bit that's testing in the evaluation functions
// below.
constexpr int kBitNum = 3;
constexpr int kBitValue = 1 << kBitNum;

class StatusRegisterTest : public testing::Test {
 protected:
  StatusRegisterTest() {
    // Create 4 different status registers and their source operands.
    status_8_ = new StatusRegister<uint8_t>(nullptr, "status8");
    status_16_ = new StatusRegister<uint16_t>(nullptr, "status16");
    status_32_ = new StatusRegister<uint32_t>(nullptr, "status32");
    status_64_ = new StatusRegister<uint64_t>(nullptr, "status64");

    src_op_8_ = new StatusRegisterSourceOperand(status_8_);
    src_op_16_ = new StatusRegisterSourceOperand(status_16_);
    src_op_32_ = new StatusRegisterSourceOperand(status_32_);
    src_op_64_ = new StatusRegisterSourceOperand(status_64_);
  }

  ~StatusRegisterTest() override {
    // Cleanup.
    delete status_8_;
    delete status_16_;
    delete status_32_;
    delete status_64_;

    delete src_op_8_;
    delete src_op_16_;
    delete src_op_32_;
    delete src_op_64_;
  }

  StatusRegister<uint8_t>* status_8_;
  StatusRegister<uint16_t>* status_16_;
  StatusRegister<uint32_t>* status_32_;
  StatusRegister<uint64_t>* status_64_;
  StatusRegisterSourceOperand<uint8_t>* src_op_8_;
  StatusRegisterSourceOperand<uint16_t>* src_op_16_;
  StatusRegisterSourceOperand<uint32_t>* src_op_32_;
  StatusRegisterSourceOperand<uint64_t>* src_op_64_;
};

// Test that initial values are all 0.
TEST_F(StatusRegisterTest, Initial) {
  uint8_t value8 = status_8_->Read();
  EXPECT_EQ(value8, 0);
  uint16_t value16 = status_16_->Read();
  EXPECT_EQ(value16, 0);
  uint32_t value32 = status_32_->Read();
  EXPECT_EQ(value32, 0);
  uint64_t value64 = status_64_->Read();
  EXPECT_EQ(value64, 0);
}

// Verify that the read function returns the correct value when the function
// evaluates to true.
TEST_F(StatusRegisterTest, Read) {
  int value = 0;
  status_8_->SetEvaluateFunction(kBitNum, [&value]() { return value != 0; });
  status_16_->SetEvaluateFunction(kBitNum, [&value]() { return value != 0; });
  status_32_->SetEvaluateFunction(kBitNum, [&value]() { return value != 0; });
  status_64_->SetEvaluateFunction(kBitNum, [&value]() { return value != 0; });
  EXPECT_EQ(status_8_->Read(), 0);
  EXPECT_EQ(status_16_->Read(), 0);
  EXPECT_EQ(status_32_->Read(), 0);
  EXPECT_EQ(status_64_->Read(), 0);
  value = 1;
  EXPECT_EQ(status_8_->Read(), kBitValue);
  EXPECT_EQ(status_16_->Read(), kBitValue);
  EXPECT_EQ(status_32_->Read(), kBitValue);
  EXPECT_EQ(status_64_->Read(), kBitValue);
}

// Verify that the value for the 4th bit (index 3), is only returned if that
// bit is set in the bitmask.
TEST_F(StatusRegisterTest, ReadMask) {
  int value = 1;
  status_8_->SetEvaluateFunction(kBitNum, [&value]() { return value != 0; });
  status_16_->SetEvaluateFunction(kBitNum, [&value]() { return value != 0; });
  status_32_->SetEvaluateFunction(kBitNum, [&value]() { return value != 0; });
  status_64_->SetEvaluateFunction(kBitNum, [&value]() { return value != 0; });
  EXPECT_EQ(status_8_->Read(0x0), 0);
  EXPECT_EQ(status_16_->Read(0x0), 0);
  EXPECT_EQ(status_32_->Read(0x0), 0);
  EXPECT_EQ(status_64_->Read(0x0), 0);
  value = 1;
  EXPECT_EQ(status_8_->Read(0xff), kBitValue);
  EXPECT_EQ(status_16_->Read(0xffff), kBitValue);
  EXPECT_EQ(status_32_->Read(0xffff'ffff), kBitValue);
  EXPECT_EQ(status_64_->Read(0xffff'ffff'ffff'ffff), kBitValue);
  EXPECT_EQ(status_8_->Read(0b1000), kBitValue);
  EXPECT_EQ(status_16_->Read(0b1000), kBitValue);
  EXPECT_EQ(status_32_->Read(0b1000), kBitValue);
  EXPECT_EQ(status_64_->Read(0b1000), kBitValue);
}

// Double check that the source operands work as expected.
TEST_F(StatusRegisterTest, SourceOperand) {
  EXPECT_EQ(src_op_8_->AsUint16(0), 0);
  EXPECT_EQ(src_op_16_->AsUint16(0), 0);
  EXPECT_EQ(src_op_32_->AsUint16(0), 0);
  EXPECT_EQ(src_op_64_->AsUint16(0), 0);

  int value = 1;
  status_8_->SetEvaluateFunction(kBitNum, [&value]() { return value != 0; });
  status_16_->SetEvaluateFunction(kBitNum, [&value]() { return value != 0; });
  status_32_->SetEvaluateFunction(kBitNum, [&value]() { return value != 0; });
  status_64_->SetEvaluateFunction(kBitNum, [&value]() { return value != 0; });

  EXPECT_EQ(src_op_8_->AsUint32(0), kBitValue);
  EXPECT_EQ(src_op_16_->AsUint32(0), kBitValue);
  EXPECT_EQ(src_op_32_->AsUint32(0), kBitValue);
  EXPECT_EQ(src_op_64_->AsUint32(0), kBitValue);
}

}  // namespace
