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

#include <any>
#include <cstdint>
#include <memory>

#include "absl/strings/string_view.h"
#include "absl/types/any.h"
#include "googlemock/include/gmock/gmock.h"  // IWYU pragma: keep
#include "googletest/include/gtest/gtest.h"
#include "mpact/sim/generic/arch_state.h"
#include "mpact/sim/generic/data_buffer.h"
#include "mpact/sim/generic/fifo.h"
#include "mpact/sim/generic/operand_interface.h"

namespace mpact {
namespace sim {
namespace generic {
namespace {

using ScalarFifo = Fifo<uint32_t>;
using Vector8Fifo = VectorFifo<uint32_t, 8>;

constexpr int kFifoCapacity = 3;
constexpr char kScalarFifoName[] = "S0";
constexpr char kVectorFifoName[] = "S0";

// Define a class that derives from ArchState since constructors are
// protected.
class MockArchState : public ArchState {
 public:
  MockArchState(absl::string_view id, SourceOperandInterface* pc_op)
      : ArchState(id, pc_op) {}
  explicit MockArchState(absl::string_view id) : MockArchState(id, nullptr) {}
};

// Test fixture to keep a single copy of registers, delay line and
// DataBufferFactory.
class FifoOperandTest : public testing::Test {
 protected:
  FifoOperandTest() {
    arch_state_ = new MockArchState("MockArchState");
    sfifo_ = new ScalarFifo(arch_state_, kScalarFifoName, kFifoCapacity);
    vfifo_ = new Vector8Fifo(arch_state_, kVectorFifoName, kFifoCapacity);
  }

  ~FifoOperandTest() override {
    delete vfifo_;
    delete sfifo_;
    delete arch_state_;
  }

  MockArchState* arch_state_;
  ScalarFifo* sfifo_;
  Vector8Fifo* vfifo_;
};

// Tests that the fifo source operands are initialized correctly.
TEST_F(FifoOperandTest, SourceOperandInitialization) {
  auto s_src_op = std::make_unique<FifoSourceOperand<uint32_t>>(sfifo_);
  EXPECT_EQ(std::any_cast<FifoBase*>(s_src_op->GetObject()),
            static_cast<FifoBase*>(sfifo_));
  EXPECT_EQ(s_src_op->shape(), sfifo_->shape());
  EXPECT_EQ(s_src_op->AsString(), kScalarFifoName);

  s_src_op = std::make_unique<FifoSourceOperand<uint32_t>>(sfifo_, "Fifo");
  EXPECT_EQ(s_src_op->AsString(), "Fifo");

  auto v_src_op = std::make_unique<FifoSourceOperand<uint32_t>>(vfifo_);
  EXPECT_EQ(std::any_cast<FifoBase*>(v_src_op->GetObject()),
            static_cast<FifoBase*>(vfifo_));
  EXPECT_EQ(v_src_op->shape(), vfifo_->shape());
  EXPECT_EQ(v_src_op->AsString(), kVectorFifoName);

  v_src_op = std::make_unique<FifoSourceOperand<uint32_t>>(vfifo_, "Fifo");
  EXPECT_EQ(v_src_op->AsString(), "Fifo");
}

// Tests that the fifo destination operands are initialized correctly.
TEST_F(FifoOperandTest, DestinationOperandInitialization) {
  auto s_dst_op = std::make_unique<FifoDestinationOperand<uint32_t>>(sfifo_, 1);
  EXPECT_EQ(s_dst_op->latency(), 1);
  EXPECT_EQ(s_dst_op->shape(), sfifo_->shape());
  EXPECT_EQ(s_dst_op->CopyDataBuffer(), nullptr);
  EXPECT_EQ(std::any_cast<FifoBase*>(s_dst_op->GetObject()),
            static_cast<FifoBase*>(sfifo_));
  EXPECT_EQ(s_dst_op->AsString(), kScalarFifoName);

  s_dst_op =
      std::make_unique<FifoDestinationOperand<uint32_t>>(sfifo_, 1, "Fifo");
  EXPECT_EQ(s_dst_op->AsString(), "Fifo");

  auto v_dst_op = std::make_unique<FifoDestinationOperand<uint32_t>>(vfifo_, 4);
  EXPECT_EQ(v_dst_op->latency(), 4);
  EXPECT_EQ(v_dst_op->shape(), vfifo_->shape());
  EXPECT_EQ(v_dst_op->CopyDataBuffer(), nullptr);
  EXPECT_EQ(std::any_cast<FifoBase*>(v_dst_op->GetObject()),
            static_cast<FifoBase*>(vfifo_));
  EXPECT_EQ(v_dst_op->AsString(), kVectorFifoName);

  v_dst_op =
      std::make_unique<FifoDestinationOperand<uint32_t>>(vfifo_, 1, "Fifo");
  EXPECT_EQ(v_dst_op->AsString(), "Fifo");
}

// Tests that a destination fifo operand can update a fifo so that
// it is visible in a source fifo operand.
TEST_F(FifoOperandTest, ScalarFifoValueWriteAndRead) {
  auto dst_op = sfifo_->CreateDestinationOperand(1);
  auto src_op = sfifo_->CreateSourceOperand();

  // Get DataBuffer from destination operand and initialize the value.
  DataBuffer* db = dst_op->AllocateDataBuffer();
  db->Set<uint32_t>(0, 0xDEADBEEF);

  // Submit data buffer and advance the delay line by the 1 cycle latency.
  db->Submit();
  arch_state_->AdvanceDelayLines();

  // Verify that the source operand can read the new value.
  EXPECT_EQ(src_op->AsBool(0), true);
  EXPECT_EQ(src_op->AsInt8(0), static_cast<int8_t>(0xEF));
  EXPECT_EQ(src_op->AsUint8(0), 0xEF);
  EXPECT_EQ(src_op->AsInt16(0), static_cast<int16_t>(0xBEEF));
  EXPECT_EQ(src_op->AsUint16(0), 0xBEEF);
  EXPECT_EQ(src_op->AsInt32(0), static_cast<int32_t>(0xDEADBEEF));
  EXPECT_EQ(src_op->AsUint32(0), 0xDEADBEEF);
  EXPECT_EQ(src_op->AsInt64(0), static_cast<int32_t>(0xDEADBEEF));
  EXPECT_EQ(src_op->AsUint64(0), 0xDEADBEEF);

  // Get a new data buffer and initialize it to a new value.
  DataBuffer* db2 = dst_op->AllocateDataBuffer();
  db2->Set<uint32_t>(0, 0xA5A55A5A);

  // Submit the data buffer and advance the delay line 1 cycle.
  db2->Submit();
  arch_state_->AdvanceDelayLines();

  // Verify the fifo still has the old value (hasn't been pop'ed).
  EXPECT_EQ(src_op->AsUint32(0), 0xDEADBEEF);

  // Pop the fifo and verify the new value is there.
  std::any_cast<FifoBase*>(src_op->GetObject())->Pop();
  EXPECT_EQ(src_op->AsUint32(0), 0xA5A55A5A);

  // Pop the fifo. It is now empty, so attempting to get a value will return 0.
  std::any_cast<FifoBase*>(src_op->GetObject())->Pop();
  EXPECT_EQ(src_op->AsUint32(0), 0);
  EXPECT_EQ(src_op->AsInt32(0), 0);

  delete dst_op;
  delete src_op;
}

// Tests that a destination vector register operand can update a register so
// that it is visible in a source register operand.
TEST_F(FifoOperandTest, VectorFifoValueWriteAndRead) {
  auto dst_op = vfifo_->CreateDestinationOperand(2);
  auto src_op = vfifo_->CreateSourceOperand();

  // Get DataBuffer from destination operand and initialize the value.
  DataBuffer* db = dst_op->AllocateDataBuffer();
  for (int index = 0; index < vfifo_->shape()[0]; index++) {
    db->Set<uint32_t>(index, 0xDEAD0000 | index);
  }

  // Submit the data buffer and advance the delay line by the 2 cycle latency.
  db->Submit();
  arch_state_->AdvanceDelayLines();
  arch_state_->AdvanceDelayLines();

  // Verify that the value has been written correctly to the register.
  for (int index = 0; index < vfifo_->shape()[0]; index++) {
    EXPECT_EQ(src_op->AsUint32(index), 0xDEAD0000 | index);
  }

  // Get another DataBuffer from destination operand and initialize to zeros.
  DataBuffer* db2 = dst_op->AllocateDataBuffer();
  for (int index = 0; index < vfifo_->shape()[0]; index++) {
    db2->Set<uint32_t>(index, 0);
  }

  // Submit the data buffer and advance the delay line by the 2 cycle latency.
  db2->Submit();
  arch_state_->AdvanceDelayLines();
  arch_state_->AdvanceDelayLines();

  // Verify that the value is the same as before (fifo hasn't been pop'ed).
  for (int index = 0; index < vfifo_->shape()[0]; index++) {
    EXPECT_EQ(src_op->AsUint32(index), 0xDEAD0000 | index);
  }

  // Pop the fifo and verify that the value has been updated.
  std::any_cast<FifoBase*>(src_op->GetObject())->Pop();
  for (int index = 0; index < vfifo_->shape()[0]; index++) {
    EXPECT_EQ(src_op->AsUint32(index), 0);
  }

  delete dst_op;
  delete src_op;
}

}  // namespace
}  // namespace generic
}  // namespace sim
}  // namespace mpact
