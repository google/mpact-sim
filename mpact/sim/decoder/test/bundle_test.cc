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

#include "mpact/sim/decoder/bundle.h"

#include <memory>
#include <vector>

#include "googlemock/include/gmock/gmock.h"
#include "mpact/sim/decoder/instruction_set.h"

namespace mpact {
namespace sim {
namespace machine_description {
namespace instruction_set {
namespace {

constexpr char kInstructionSetName[] = "Test";
constexpr char kBundleName[] = "TestBundle";

constexpr char kSubBundleName0[] = "TestSubBundle0";
constexpr char kSubBundleName1[] = "TestSubBundle1";
constexpr char kSubBundleName2[] = "TestSubBundle2";
const char *kSubBundleNames[3] = {kSubBundleName0, kSubBundleName1,
                                  kSubBundleName2};
constexpr char kSlotName0[] = "TestSlot0";
constexpr char kSlotName1[] = "TestSlot1";
constexpr char kSlotName2[] = "TestSlot2";
const char *kSlotNames[3] = {kSlotName0, kSlotName1, kSlotName2};

class BundleTest : public testing::Test {
 protected:
  BundleTest() {
    instruction_set_ = std::make_unique<InstructionSet>(kInstructionSetName);
    bundle_ = std::make_unique<Bundle>(kBundleName, instruction_set_.get());
  }

  ~BundleTest() override{};

  std::unique_ptr<InstructionSet> instruction_set_;
  std::unique_ptr<Bundle> bundle_;
};

// Verify basic initial settings.
TEST_F(BundleTest, Basic) {
  EXPECT_STREQ(bundle_->name().c_str(), kBundleName);
  EXPECT_EQ(bundle_->bundle_names().size(), 0);
  EXPECT_EQ(bundle_->slot_uses().size(), 0);
  EXPECT_EQ(bundle_->instruction_set(), instruction_set_.get());
  EXPECT_FALSE(bundle_->is_marked());
}

// Verify setter for is marked.
TEST_F(BundleTest, IsMarked) {
  bundle_->set_is_marked(true);
  EXPECT_TRUE(bundle_->is_marked());
  bundle_->set_is_marked(false);
  EXPECT_FALSE(bundle_->is_marked());
}

// Verify that sub-bundle names work.
TEST_F(BundleTest, SubBundle) {
  for (int index = 0; index < 3; index++) {
    EXPECT_EQ(bundle_->bundle_names().size(), index);
    bundle_->AppendBundleName(kSubBundleNames[index]);
    EXPECT_EQ(bundle_->bundle_names().size(), index + 1);
    EXPECT_STREQ(bundle_->bundle_names()[index].c_str(),
                 kSubBundleNames[index]);
  }
}

// Verify slot uses is correct.
TEST_F(BundleTest, Slots) {
  // Vector of slot instances used.
  std::vector<std::vector<int>> slot_vec = {{0}, {1, 3}, {0, 2, 4}};
  for (int index = 0; index < 3; index++) {
    EXPECT_EQ(bundle_->slot_uses().size(), index);
    bundle_->AppendSlot(kSlotNames[index], slot_vec[index]);
    EXPECT_EQ(bundle_->slot_uses().size(), index + 1);
    EXPECT_STREQ(bundle_->slot_uses()[index].first.c_str(), kSlotNames[index]);
    EXPECT_EQ(bundle_->slot_uses()[index].second.size(),
              slot_vec[index].size());
    for (size_t vec_indx = 0; vec_indx < slot_vec[index].size(); vec_indx++) {
      EXPECT_EQ(bundle_->slot_uses()[index].second[vec_indx],
                slot_vec[index][vec_indx]);
    }
  }
}

}  // namespace
}  // namespace instruction_set
}  // namespace machine_description
}  // namespace sim
}  // namespace mpact
