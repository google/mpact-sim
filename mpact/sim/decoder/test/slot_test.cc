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

#include "mpact/sim/decoder/slot.h"

#include <memory>
#include <string>

#include "googlemock/include/gmock/gmock.h"
#include "mpact/sim/decoder/instruction.h"
#include "mpact/sim/decoder/instruction_set.h"

namespace mpact {
namespace sim {
namespace machine_description {
namespace instruction_set {
namespace {

constexpr char kInstructionSetName[] = "Test";
constexpr char kSlotName[] = "TestSlot";
constexpr char kBaseName[] = "TestBaseSlot";

class SlotTest : public testing::Test {
 protected:
  SlotTest() {
    instruction_set_ = std::make_unique<InstructionSet>(kInstructionSetName);
    slot_ = std::make_unique<Slot>(kSlotName, instruction_set_.get(),
                                   /* is_templated */ false);
  }

  ~SlotTest() override = default;

  std::unique_ptr<InstructionSet> instruction_set_;
  std::unique_ptr<Slot> slot_;
};

// Verify expected initial values.
TEST_F(SlotTest, Basic) {
  EXPECT_EQ(slot_->instruction_set(), instruction_set_.get());
  EXPECT_EQ(slot_->size(), 1);
  EXPECT_STREQ(slot_->name().c_str(), kSlotName);
  EXPECT_EQ(slot_->base_slots().size(), 0);
  EXPECT_EQ(slot_->instruction_map().size(), 0);
}

// Verify getter and setter for is_marked.
TEST_F(SlotTest, IsMarked) {
  EXPECT_FALSE(slot_->is_marked());
  slot_->set_is_marked(true);
  EXPECT_TRUE(slot_->is_marked());
  slot_->set_is_marked(false);
  EXPECT_FALSE(slot_->is_marked());
}

// Verify getter and setter for is_referenced.
TEST_F(SlotTest, IsReferenced) {
  EXPECT_FALSE(slot_->is_referenced());
  slot_->set_is_referenced(true);
  EXPECT_TRUE(slot_->is_referenced());
  slot_->set_is_referenced(false);
  EXPECT_FALSE(slot_->is_referenced());
}

// Verify getter and setter for base and base_name.
TEST_F(SlotTest, BaseSlot) {
  EXPECT_EQ(slot_->base_slots().size(), 0);
  auto base_slot = std::make_unique<Slot>(kBaseName, instruction_set_.get(),
                                          /* is_templated */ false);
  EXPECT_TRUE(slot_->AddBase(base_slot.get()).ok());
  EXPECT_EQ(slot_->base_slots().size(), 1);
  EXPECT_EQ(slot_->base_slots()[0].base, base_slot.get());
}

// Verify that appending opcodes to slot works.
TEST_F(SlotTest, OpcodeVec) {
  for (int inst_indx = 0; inst_indx < 4; inst_indx++) {
    EXPECT_EQ(slot_->instruction_map().size(), inst_indx);
    std::string opcode_name = absl::StrCat("opcode_", inst_indx);
    auto res = instruction_set_->opcode_factory()->CreateOpcode(opcode_name);
    ASSERT_TRUE(res.status().ok());
    auto *inst = new Instruction(res.value(), slot_.get());
    EXPECT_TRUE(slot_->AppendInstruction(inst).ok());
    EXPECT_EQ(slot_->instruction_map().size(), inst_indx + 1);
    EXPECT_EQ(slot_->instruction_map().at(opcode_name), inst);
  }
}

}  // namespace
}  // namespace instruction_set
}  // namespace machine_description
}  // namespace sim
}  // namespace mpact
