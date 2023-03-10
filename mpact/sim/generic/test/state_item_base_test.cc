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

#include "mpact/sim/generic/state_item_base.h"

#include <memory>
#include <string>
#include <vector>

#include "absl/memory/memory.h"
#include "absl/strings/string_view.h"
#include "googlemock/include/gmock/gmock.h"
#include "googletest/include/gtest/gtest.h"
#include "mpact/sim/generic/data_buffer.h"

namespace mpact {
namespace sim {
namespace generic {
namespace {

using testing::StrEq;

class TestStateItemBase : public StateItemBase {
 public:
  void SetDataBuffer(DataBuffer *db) override {}
  TestStateItemBase(ArchState *state, absl::string_view nm,
                    const std::vector<int> sz, int unit_size)
      : StateItemBase(state, nm, sz, unit_size) {}
  ~TestStateItemBase() override {}
};

// Create object and verify properties.
TEST(TestStateItemBase, BasicProperties) {
  const std::string kRegName("R0");
  const int kElementSize = 4;
  auto sb = std::make_unique<TestStateItemBase>(
      nullptr, kRegName, std::vector<int>({1}), kElementSize);

  EXPECT_EQ(sb->arch_state(), nullptr);
  EXPECT_THAT(sb->name(), StrEq(kRegName));
  EXPECT_EQ(sb->size(), kElementSize);
  EXPECT_EQ(sb->element_size(), kElementSize);
}

}  // namespace
}  // namespace generic
}  // namespace sim
}  // namespace mpact
