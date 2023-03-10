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

#include "mpact/sim/decoder/opcode.h"

#include <memory>
#include <string>

#include "absl/status/statusor.h"
#include "googlemock/include/gmock/gmock.h"
#include "googletest/include/gtest/gtest.h"
#include "mpact/sim/decoder/instruction_set.h"

namespace mpact {
namespace sim {
namespace machine_description {
namespace instruction_set {
namespace {

constexpr char kInstructionSetName[] = "Test";
constexpr char kOpcodeName0[] = "opcode_0";
constexpr char kOpcodeName1[] = "opcode_1";
constexpr char kOpcodeName2[] = "opcode_2";
constexpr char kPredicateOpName[] = "pred";

const char *kOpcodeNames[] = {kOpcodeName0, kOpcodeName1, kOpcodeName2};

// Test fixture for opcode test.
class OpcodeTest : public testing::Test {
 protected:
  OpcodeTest() {
    instruction_set_ = std::make_unique<InstructionSet>(kInstructionSetName);

    absl::StatusOr<Opcode *> result =
        instruction_set_->opcode_factory()->CreateOpcode(kOpcodeName0);
    opcode_ = result.ok() ? result.value() : nullptr;
  }

  ~OpcodeTest() override { delete opcode_; }

  std::unique_ptr<InstructionSet> instruction_set_;
  Opcode *opcode_;  // Does not have to be deleted, as OpcodeFactory handles it.
};

TEST_F(OpcodeTest, Basic) {
  EXPECT_STREQ(opcode_->name().c_str(), kOpcodeName0);
  EXPECT_EQ(opcode_->value(), 1);
  EXPECT_STREQ(opcode_->predicate_op_name().c_str(), "");
  EXPECT_EQ(opcode_->source_op_name_vec().size(), 0);
  EXPECT_EQ(opcode_->dest_op_vec().size(), 0);
}

// Verify that values of opcodes increment as you create new ones.
TEST_F(OpcodeTest, Multiple) {
  // Creating a duplicate opcode should fail.
  absl::StatusOr<Opcode *> result =
      instruction_set_->opcode_factory()->CreateOpcode(kOpcodeNames[0]);
  EXPECT_TRUE(absl::IsInternal(result.status()));
  for (int indx = 1; indx < 3; indx++) {
    result =
        instruction_set_->opcode_factory()->CreateOpcode(kOpcodeNames[indx]);
    // Since one opcode is created in fixture, these opcode values start at 2.
    ASSERT_TRUE(result.ok());
    Opcode *opcode = result.value();
    EXPECT_EQ(opcode->value(), indx + 1);
    delete opcode;
  }
}

// Verify setter for predicate operand name.
TEST_F(OpcodeTest, PredicateOperandName) {
  opcode_->set_predicate_op_name(kPredicateOpName);
  EXPECT_STREQ(opcode_->predicate_op_name().c_str(), kPredicateOpName);
}

// Verify source operand name vector.
TEST_F(OpcodeTest, SourceOperandNames) {
  for (int indx = 0; indx < 3; indx++) {
    std::string source_op_name = absl::StrCat("SourceOp", indx);
    opcode_->AppendSourceOpName(source_op_name);
    EXPECT_EQ(opcode_->source_op_name_vec().size(), indx + 1);
    EXPECT_STREQ(opcode_->source_op_name_vec()[indx].c_str(),
                 source_op_name.c_str());
  }
}

// Verify destination operand name vector.
TEST_F(OpcodeTest, DestOperandNames) {
  for (int indx = 0; indx < 2; indx++) {
    std::string dest_op_name = absl::StrCat("DestOp", indx);
    if (indx == 0) {
      opcode_->AppendDestOp(dest_op_name);
    } else if (indx == 1) {
      // Using nullptr - the value isn't checked upon append.
      opcode_->AppendDestOp(dest_op_name, nullptr);
    }
    EXPECT_EQ(opcode_->dest_op_vec().size(), indx + 1);
    EXPECT_STREQ(opcode_->dest_op_vec()[indx]->name().c_str(),
                 dest_op_name.c_str());
  }
}

}  // namespace
}  // namespace instruction_set
}  // namespace machine_description
}  // namespace sim
}  // namespace mpact
