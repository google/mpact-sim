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

#include "mpact/sim/decoder/instruction_set.h"

#include <memory>

#include "googlemock/include/gmock/gmock.h"
#include "googletest/include/gtest/gtest.h"
#include "mpact/sim/decoder/bundle.h"
#include "mpact/sim/decoder/slot.h"

namespace mpact {
namespace sim {
namespace machine_description {
namespace instruction_set {
namespace {

constexpr char kInstructionSetName[] = "Test";
constexpr char kBundleName[] = "TestBundle";
constexpr char kSlotName[] = "TestSlot";

class InstructionSetTest : public testing::Test {
 protected:
  InstructionSetTest() {
    instruction_set_ = std::make_unique<InstructionSet>(kInstructionSetName);
  }
  ~InstructionSetTest() override {}

  std::unique_ptr<InstructionSet> instruction_set_;
};

// Test that the InstructionSet class in properly initialized.
TEST_F(InstructionSetTest, Basic) {
  EXPECT_STREQ(instruction_set_->name().c_str(), kInstructionSetName);
  EXPECT_EQ(nullptr, instruction_set_->bundle());
  Bundle *bundle = new Bundle(kBundleName, instruction_set_.get(), nullptr);
  instruction_set_->set_bundle(bundle);
  EXPECT_EQ(instruction_set_->bundle(), bundle);
}

// Test that a bundle added to the instruction set class can be retrieved by
// name.
TEST_F(InstructionSetTest, SingleBundle) {
  EXPECT_EQ(nullptr, instruction_set_->GetBundle(kBundleName));
  Bundle *bundle = new Bundle(kBundleName, instruction_set_.get(), nullptr);
  instruction_set_->AddBundle(bundle);
  EXPECT_EQ(bundle, instruction_set_->GetBundle(kBundleName));
  EXPECT_EQ(nullptr, instruction_set_->GetBundle(kSlotName));
  // The bundle will be deleted by the InstructionSet destructor.
}

// Test that a slot added to the instruction set class can be retrieved by
// name.
TEST_F(InstructionSetTest, SingleSlot) {
  EXPECT_EQ(nullptr, instruction_set_->GetSlot(kSlotName));
  Slot *slot = new Slot(kSlotName, instruction_set_.get(),
                        /* is_templated */ false, nullptr);
  instruction_set_->AddSlot(slot);
  EXPECT_EQ(slot, instruction_set_->GetSlot(kSlotName));
  EXPECT_EQ(nullptr, instruction_set_->GetSlot(kBundleName));
  // The slot will be delted by the InstructionSet destructor.
}

}  // namespace
}  // namespace instruction_set
}  // namespace machine_description
}  // namespace sim
}  // namespace mpact
