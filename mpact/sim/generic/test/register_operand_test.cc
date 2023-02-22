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

#include "absl/strings/string_view.h"
#include "absl/types/any.h"
#include "googlemock/include/gmock/gmock.h"
#include "mpact/sim/generic/arch_state.h"
#include "mpact/sim/generic/data_buffer.h"
#include "mpact/sim/generic/delay_line.h"
#include "mpact/sim/generic/operand_interface.h"
#include "mpact/sim/generic/register.h"
#include "mpact/sim/generic/simple_resource.h"

namespace mpact {
namespace sim {
namespace generic {
namespace {

using ScalarRegister = Register<uint32_t>;
using Vector8Register = VectorRegister<uint32_t, 8>;
using ScalarReservedRegister = ReservedRegister<uint32_t>;

static constexpr char kTestPoolName[] = "TestPool";
static constexpr int kTestPoolSize = 35;

// Define a class that derives from ArchState since constructors are
// protected.
class MockArchState : public ArchState {
 public:
  MockArchState(absl::string_view id, SourceOperandInterface *pc_op)
      : ArchState(id, pc_op) {}
  explicit MockArchState(absl::string_view id) : MockArchState(id, nullptr) {}
};

// Test fixture to keep a single copy of registers, delay line and
// DataBufferFactory.
class RegisterOperandTest : public testing::Test {
 protected:
  RegisterOperandTest() {
    arch_state_ = new MockArchState("MockArchState");
    sreg_ = new ScalarRegister(arch_state_, "S0");
    vreg_ = new Vector8Register(arch_state_, "V0");
    pool_ = new SimpleResourcePool(kTestPoolName, kTestPoolSize);
    auto result = pool_->AddResource("R0");
    if (!result.ok()) return;
    rreg_ =
        new ScalarReservedRegister(arch_state_, "R0", pool_->GetResource("R0"));
  }

  ~RegisterOperandTest() override {
    delete vreg_;
    delete sreg_;
    delete rreg_;
    delete pool_;
    delete arch_state_;
  }

  MockArchState *arch_state_;
  ScalarRegister *sreg_;
  Vector8Register *vreg_;
  ScalarReservedRegister *rreg_ = nullptr;
  SimpleResourcePool *pool_;
};

// Tests that the register source operands are initialized correctly.
TEST_F(RegisterOperandTest, SourceOperandInitialization) {
  auto s_src_op = sreg_->CreateSourceOperand();
  EXPECT_EQ(std::any_cast<RegisterBase *>(s_src_op->GetObject()),
            static_cast<RegisterBase *>(sreg_));
  EXPECT_EQ(s_src_op->shape(), sreg_->shape());
  delete s_src_op;

  auto v_src_op = vreg_->CreateSourceOperand();
  EXPECT_EQ(std::any_cast<RegisterBase *>(v_src_op->GetObject()),
            static_cast<RegisterBase *>(vreg_));
  EXPECT_EQ(v_src_op->shape(), vreg_->shape());
  delete v_src_op;

  auto r_src_op = rreg_->CreateSourceOperand();
  EXPECT_EQ(std::any_cast<RegisterBase *>(r_src_op->GetObject()),
            static_cast<RegisterBase *>(rreg_));
  EXPECT_EQ(r_src_op->shape(), rreg_->shape());
  delete r_src_op;
}

// Tests that the register destination operands are initialized correctly.
TEST_F(RegisterOperandTest, DestinationOperandInitialization) {
  auto s_dst_op = sreg_->CreateDestinationOperand(1);
  EXPECT_EQ(s_dst_op->latency(), 1);
  EXPECT_EQ(s_dst_op->shape(), sreg_->shape());
  EXPECT_EQ(std::any_cast<RegisterBase *>(s_dst_op->GetObject()),
            static_cast<RegisterBase *>(sreg_));
  delete s_dst_op;

  auto v_dst_op = vreg_->CreateDestinationOperand(4);
  EXPECT_EQ(v_dst_op->latency(), 4);
  EXPECT_EQ(v_dst_op->shape(), vreg_->shape());
  EXPECT_EQ(std::any_cast<RegisterBase *>(v_dst_op->GetObject()),
            static_cast<RegisterBase *>(vreg_));
  delete v_dst_op;

  auto r_dst_op = rreg_->CreateDestinationOperand(3);
  EXPECT_EQ(r_dst_op->latency(), 3);
  EXPECT_EQ(r_dst_op->shape(), rreg_->shape());
  EXPECT_EQ(std::any_cast<RegisterBase *>(r_dst_op->GetObject()),
            static_cast<RegisterBase *>(rreg_));
  delete r_dst_op;
}

// Tests that a destination register operand can update a register so that
// it is visible in a source register operand.
TEST_F(RegisterOperandTest, ScalarRegisterValueWriteAndRead) {
  auto dst_op = sreg_->CreateDestinationOperand(1);
  auto src_op = sreg_->CreateSourceOperand();

  // Get DataBuffer from destination operand and initialize the value.
  DataBuffer *db = dst_op->AllocateDataBuffer();
  db->Set<uint32_t>(0, 0xDEADBEEF);
  // Submit data buffer and advance the delay line by the 1 cycle latency.
  db->Submit();
  arch_state_->AdvanceDelayLines();

  // Verify that the source operand can read the new value.
  EXPECT_EQ(src_op->AsUint32(0), 0xDEADBEEF);

  // Get a new data buffer with copy of the current value and verify value is
  // correct.
  DataBuffer *db2 = dst_op->CopyDataBuffer();
  EXPECT_EQ(db2->Get<uint32_t>(0), 0xDEADBEEF);

  // Change value, submit the data buffer and advance the delay line 1 cycle.
  db2->Set<uint32_t>(0, 0);
  db2->Submit();
  arch_state_->AdvanceDelayLines();

  // Verify the updated value.
  EXPECT_EQ(src_op->AsUint32(0), 0);

  delete src_op;
  delete dst_op;
}

// Tests that a destination vector register operand can update a register so
// that it is visible in a source register operand.
TEST_F(RegisterOperandTest, VectorRegisterValueWriteAndRead) {
  auto dst_op = vreg_->CreateDestinationOperand(2);
  auto src_op = vreg_->CreateSourceOperand();

  // Get DataBuffer from destination operand and initialize the value.
  DataBuffer *db = dst_op->AllocateDataBuffer();
  for (int index = 0; index < vreg_->shape()[0]; index++) {
    db->Set<uint32_t>(index, 0xDEAD0000 | index);
  }

  // Submit the data buffer and advance the delay line by the 2 cycle latency.
  db->Submit();
  arch_state_->AdvanceDelayLines();
  arch_state_->AdvanceDelayLines();

  // Verify that the value has been written correctly to the register.
  for (int index = 0; index < vreg_->shape()[0]; index++) {
    EXPECT_EQ(src_op->AsUint32(index), 0xDEAD0000 | index);
  }

  // Get DataBuffer from destination operand with copy of the current value.
  DataBuffer *db2 = dst_op->CopyDataBuffer();

  // Verify that the value is the same as before.
  for (int index = 0; index < vreg_->shape()[0]; index++) {
    EXPECT_EQ(db2->Get<uint32_t>(index), 0xDEAD0000 | index);
    db2->Set<uint32_t>(index, 0);
  }

  // Submit the data buffer and advance the delay line by the 2 cycle latency.
  db2->Submit();
  arch_state_->AdvanceDelayLines();
  arch_state_->AdvanceDelayLines();

  // Verify that the value has been updated.
  for (int index = 0; index < vreg_->shape()[0]; index++) {
    EXPECT_EQ(src_op->AsUint32(index), 0);
  }

  delete src_op;
  delete dst_op;
}

// Tests that a destination reserved register operand can update a register so
// that it is visible in a source register operand.
TEST_F(RegisterOperandTest, ReservedRegisterValueWriteAndRead) {
  auto dst_op = rreg_->CreateDestinationOperand(2);
  auto src_op = rreg_->CreateSourceOperand();

  EXPECT_TRUE(rreg_->resource()->IsFree());
  rreg_->resource()->Acquire();

  // Get DataBuffer from destination operand and initialize the value.
  DataBuffer *db = dst_op->AllocateDataBuffer();

  db->Set<uint32_t>(0, 0xDEAD0000);

  // Submit the data buffer and advance the delay line by the 2 cycle latency.
  db->Submit();
  EXPECT_FALSE(rreg_->resource()->IsFree());
  arch_state_->AdvanceDelayLines();
  EXPECT_FALSE(rreg_->resource()->IsFree());
  arch_state_->AdvanceDelayLines();
  EXPECT_TRUE(rreg_->resource()->IsFree());

  EXPECT_EQ(src_op->AsUint32(0), 0xDEAD0000);

  // Get DataBuffer from destination operand with copy of the current value.
  DataBuffer *db2 = dst_op->CopyDataBuffer();

  EXPECT_TRUE(rreg_->resource()->IsFree());
  rreg_->resource()->Acquire();
  // Verify that the value is the same as before.
  EXPECT_EQ(db2->Get<uint32_t>(0), 0xDEAD0000);
  db2->Set<uint32_t>(0, 0);

  // Submit the data buffer and advance the delay line by the 2 cycle latency.
  db2->Submit();
  EXPECT_FALSE(rreg_->resource()->IsFree());
  arch_state_->AdvanceDelayLines();
  EXPECT_FALSE(rreg_->resource()->IsFree());
  arch_state_->AdvanceDelayLines();
  EXPECT_TRUE(rreg_->resource()->IsFree());

  // Verify that the value has been updated.
  EXPECT_EQ(src_op->AsUint32(0), 0);

  delete src_op;
  delete dst_op;
}

}  // namespace
}  // namespace generic
}  // namespace sim
}  // namespace mpact
