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

#include "mpact/sim/generic/arch_state.h"

#include <any>
#include <cstdint>
#include <string>
#include <vector>

#include "absl/strings/string_view.h"
#include "googlemock/include/gmock/gmock.h"
#include "googletest/include/gtest/gtest.h"
#include "mpact/sim/generic/delay_line.h"
#include "mpact/sim/generic/delay_line_interface.h"
#include "mpact/sim/generic/fifo.h"
#include "mpact/sim/generic/operand_interface.h"
#include "mpact/sim/generic/register.h"

namespace mpact {
namespace sim {
namespace generic {
namespace {

using ::testing::Ne;
using ::testing::StrEq;

using ScalarFifo = Fifo<uint32_t>;
using ScalarRegister = Register<uint32_t>;
using Vector8Register = VectorRegister<uint32_t, 8>;

constexpr int kFifoDepth = 3;

constexpr char kRegName1[] = "Reg1";
constexpr char kRegName2[] = "Reg2";
constexpr char kFifoName1[] = "Fifo1";
constexpr char kFifoName2[] = "Fifo2";

// Define a new delay line type.
struct IntDelayRecord {
  int *destination;
  int value;
  void Apply() { *destination = value; }
  IntDelayRecord() = delete;
  IntDelayRecord(int *dest, int val) : destination(dest), value(val) {}
};

using IntDelayLine = DelayLine<IntDelayRecord>;

// Define a class that maintains program counter without being a register.
// This is so simplify maintaining a scalar value that changes quickly.
class MyProgramCounter : public SourceOperandInterface {
 public:
  MyProgramCounter() = default;

  uint32_t pc() const { return pc_; }
  void set_pc(uint32_t val) { pc_ = val; }
  // Methods for accessing the nth value element.
  bool AsBool(int index) override { return static_cast<bool>(pc_); }
  int8_t AsInt8(int index) override { return static_cast<int8_t>(pc_); }
  uint8_t AsUint8(int index) override { return static_cast<uint8_t>(pc_); }
  int16_t AsInt16(int index) override { return static_cast<int16_t>(pc_); }
  uint16_t AsUint16(int) override { return static_cast<uint16_t>(pc_); }
  int32_t AsInt32(int index) override { return static_cast<int32_t>(pc_); }
  uint32_t AsUint32(int index) override { return static_cast<uint32_t>(pc_); }
  int64_t AsInt64(int index) override { return static_cast<int64_t>(pc_); }
  uint64_t AsUint64(int index) override { return static_cast<uint64_t>(pc_); }

  // Return a pointer to the object instance that implmements the state in
  // question (or nullptr) if no such object "makes sense". This is used if
  // the object requires additional manipulation - such as a fifo that needs
  // to be pop'ed. If no such manipulation is required, nullptr should be
  // returned.
  std::any GetObject() const override { return nullptr; }

  // Returns the shape of the operand {}
  std::vector<int> shape() const override { return shape_; }

  std::string AsString() const override { return std::string("PC"); }

 private:
  uint32_t pc_;
  std::vector<int> shape_;
};

// Define a class that derives from ArchState since constructors are
// protected.
class MyArchState : public ArchState {
 public:
  MyArchState(absl::string_view id, SourceOperandInterface *pc_op)
      : ArchState(id, pc_op) {}
  explicit MyArchState(absl::string_view id) : MyArchState(id, nullptr) {}
};

// Test fixture class that sets up the ArchState derived class and registers
// some registers, a fifo, and a pc source operand.
class ArchStateTest : public testing::Test {
 protected:
  ArchStateTest() : my_pc_() {
    arch_state_ = new MyArchState("TestArchitecture", &my_pc_);
    for (int reg_no = 0; reg_no < 16; reg_no++) {
      arch_state_->AddRegister<ScalarRegister>(absl::StrCat("R", reg_no));
    }
    for (int reg_no = 0; reg_no < 16; reg_no++) {
      arch_state_->AddRegister<Vector8Register>(absl::StrCat("V", reg_no));
    }
    arch_state_->AddFifo<ScalarFifo>("F0", kFifoDepth);
  }

  ~ArchStateTest() override { delete arch_state_; }

  MyProgramCounter my_pc_;
  ArchState *arch_state_;
};

// Test that the ArchState instance is properly initialized.
TEST_F(ArchStateTest, BasicProperties) {
  EXPECT_EQ(arch_state_->id().compare("TestArchitecture"), 0);
  EXPECT_NE(arch_state_->program_error_controller(), nullptr);
  EXPECT_THAT(arch_state_->program_error_controller()->name(),
              StrEq("TestArchitectureErrors"));
  EXPECT_EQ(arch_state_->pc_operand(),
            static_cast<SourceOperandInterface *>(&my_pc_));
  for (int reg_no = 0; reg_no < 16; reg_no++) {
    EXPECT_THAT(arch_state_->registers()->find(absl::StrCat("R", reg_no)),
                Ne(arch_state_->registers()->end()));
    EXPECT_THAT(arch_state_->registers()->find(absl::StrCat("V", reg_no)),
                Ne(arch_state_->registers()->end()));
  }
  EXPECT_THAT(arch_state_->fifos()->find("F0"),
              Ne(arch_state_->fifos()->end()));
  EXPECT_EQ(arch_state_->registers()->find("X0"),
            arch_state_->registers()->end());
  EXPECT_EQ(arch_state_->fifos()->find("X0"), arch_state_->fifos()->end());
}

TEST_F(ArchStateTest, EmptyDelayLineAdvance) {
  arch_state_->AdvanceDelayLines();
}

// Test that the function delay line works and get advanced by ArchState.
TEST_F(ArchStateTest, FunctionDelayLine) {
  int my_value = 0;
  arch_state_->function_delay_line()->Add(1, [&]() { my_value = 1; });
  arch_state_->function_delay_line()->Add(2, [&]() { my_value = 2; });
  EXPECT_EQ(my_value, 0);
  arch_state_->AdvanceDelayLines();
  EXPECT_EQ(my_value, 1);
  arch_state_->AdvanceDelayLines();
  EXPECT_EQ(my_value, 2);
}

// Test creation of a new delay line and that it gets advanced by ArchState.
TEST_F(ArchStateTest, AddDelayLine) {
  IntDelayLine *int_delay_line =
      arch_state_->CreateAndAddDelayLine<IntDelayLine>();
  int my_value = 0;
  int_delay_line->Add(1, &my_value, 1);
  int_delay_line->Add(2, &my_value, 2);
  EXPECT_EQ(my_value, 0);
  arch_state_->AdvanceDelayLines();
  EXPECT_EQ(my_value, 1);
  arch_state_->AdvanceDelayLines();
  EXPECT_EQ(my_value, 2);
}

// Test that the pc source operand is properly tied to the pc object.
TEST_F(ArchStateTest, PcOperand) {
  my_pc_.set_pc(0xDEADBEEF);
  EXPECT_EQ(arch_state_->pc_operand()->AsUint32(0), 0xDEADBEEF);
  my_pc_.set_pc(0xA5A5A5A5);
  EXPECT_EQ(arch_state_->pc_operand()->AsUint32(0), 0xA5A5A5A5);
}

// Test alternate ways to add registers.
TEST_F(ArchStateTest, AddRegister) {
  auto *reg = new ScalarRegister(arch_state_, kRegName1);
  arch_state_->AddRegister(reg);
  // Also add the register with an alias.
  arch_state_->AddRegister(kRegName2, reg);
  auto iter = arch_state_->registers()->find(kRegName1);
  auto *reg1 =
      (iter == arch_state_->registers()->end()) ? nullptr : iter->second;
  iter = arch_state_->registers()->find(kRegName2);
  auto *reg2 =
      (iter == arch_state_->registers()->end()) ? nullptr : iter->second;
  EXPECT_EQ(reg1, reg);
  EXPECT_EQ(reg2, reg);
  arch_state_->RemoveRegister(kRegName2);
  iter = arch_state_->registers()->find(kRegName1);
  reg1 = (iter == arch_state_->registers()->end()) ? nullptr : iter->second;
  iter = arch_state_->registers()->find(kRegName2);
  reg2 = (iter == arch_state_->registers()->end()) ? nullptr : iter->second;
  EXPECT_EQ(reg1, reg);
  EXPECT_EQ(reg2, nullptr);
}

// Test alternate ways to add registers.
TEST_F(ArchStateTest, AddFifo) {
  auto *fifo = new ScalarFifo(arch_state_, kFifoName1, 8);
  arch_state_->AddFifo(fifo);
  // Add fifo with an alias.
  arch_state_->AddFifo(kFifoName2, fifo);
  auto iter = arch_state_->fifos()->find(kFifoName1);
  auto *fifo1 = (iter == arch_state_->fifos()->end()) ? nullptr : iter->second;
  iter = arch_state_->fifos()->find(kFifoName2);
  auto *fifo2 = (iter == arch_state_->fifos()->end()) ? nullptr : iter->second;
  EXPECT_EQ(fifo1, fifo);
  EXPECT_EQ(fifo2, fifo);
  arch_state_->RemoveFifo(kFifoName2);
  iter = arch_state_->fifos()->find(kFifoName1);
  fifo1 = (iter == arch_state_->fifos()->end()) ? nullptr : iter->second;
  iter = arch_state_->fifos()->find(kFifoName2);
  fifo2 = (iter == arch_state_->fifos()->end()) ? nullptr : iter->second;
  EXPECT_EQ(fifo1, fifo);
  EXPECT_EQ(fifo2, nullptr);
}

}  // namespace
}  // namespace generic
}  // namespace sim
}  // namespace mpact
