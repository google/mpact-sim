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

#include "mpact/sim/generic/delay_line.h"

#include "googlemock/include/gmock/gmock.h"  // IWYU pragma: keep
#include "googletest/include/gtest/gtest.h"

namespace mpact {
namespace sim {
namespace generic {
namespace {

class DelayLineTest : public testing::Test {
 protected:
  // Delay Line entry type, simple value and pointer to value.
  class TestRecord {
   public:
    void Apply() { *dest_ = value_; }
    TestRecord(int value, int* dest) : value_(value), dest_(dest) {}

   private:
    int value_;
    int* dest_;
  };

  // Delay Line is 8 entries deep.
  DelayLineTest() { delay_line_ = new DelayLine<TestRecord>(8); }

  ~DelayLineTest() override { delete delay_line_; }

  DelayLine<TestRecord>* delay_line_;
};

// Test that the value changes after two calls to Advance.
TEST_F(DelayLineTest, SimpleWriteBack) {
  int dest = 0;
  const int kNewValue = 2;
  DelayLineTest::TestRecord rec = {kNewValue, &dest};

  EXPECT_TRUE(delay_line_->IsEmpty());
  int count = delay_line_->Add(2, rec);
  EXPECT_EQ(count, 1);
  EXPECT_FALSE(delay_line_->IsEmpty());
  EXPECT_EQ(dest, 0);
  count = delay_line_->Advance();
  EXPECT_EQ(count, 1);
  EXPECT_FALSE(delay_line_->IsEmpty());
  EXPECT_EQ(dest, 0);
  count = delay_line_->Advance();
  EXPECT_EQ(count, 0);
  EXPECT_TRUE(delay_line_->IsEmpty());
  EXPECT_EQ(dest, kNewValue);
}

// The delay line is 8 deep. Advancing by 6, then adding entry
// with delay of 3 requires the delay line to wrap.
TEST_F(DelayLineTest, SimpleWriteBackWithWrap) {
  int dest = 0;
  const int kNewValue = 2;
  DelayLineTest::TestRecord rec = {kNewValue, &dest};

  // Advance delay line 6 spots.
  for (int cycle = 0; cycle < 6; cycle++) {
    delay_line_->Advance();
  }

  // Add record to delay line
  delay_line_->Add(3, rec);

  EXPECT_EQ(dest, 0);
  delay_line_->Advance();
  EXPECT_EQ(dest, 0);
  delay_line_->Advance();
  EXPECT_EQ(dest, 0);
  delay_line_->Advance();
  EXPECT_EQ(dest, kNewValue);
}

// Testing that wrapped entries are processed correctly when having to
// resize delay line due to the latency being greater than the depth.
TEST_F(DelayLineTest, WriteBackRequiringWrapAndResize) {
  int dest = 0;
  const int kNewValue1 = 2;
  const int kNewValue2 = 3;
  DelayLineTest::TestRecord rec1 = {kNewValue1, &dest};
  DelayLineTest::TestRecord rec2 = {kNewValue2, &dest};

  // Advance delay line 6 spots.
  for (int cycle = 0; cycle < 6; cycle++) {
    delay_line_->Advance();
  }
  int count;
  // Add record to delay line.
  EXPECT_TRUE(delay_line_->IsEmpty());
  count = delay_line_->Add(3, rec1);
  EXPECT_EQ(count, 1);
  EXPECT_FALSE(delay_line_->IsEmpty());
  count = delay_line_->Add(8 + 2, rec2);
  EXPECT_EQ(count, 2);
  EXPECT_FALSE(delay_line_->IsEmpty());

  EXPECT_EQ(dest, 0);
  count = delay_line_->Advance();
  EXPECT_EQ(count, 2);
  EXPECT_EQ(dest, 0);
  count = delay_line_->Advance();
  EXPECT_EQ(count, 2);
  EXPECT_EQ(dest, 0);
  count = delay_line_->Advance();
  EXPECT_EQ(count, 1);
  EXPECT_EQ(dest, kNewValue1);

  for (int cycle = 0; cycle < 6; cycle++) {
    count = delay_line_->Advance();
    EXPECT_EQ(dest, kNewValue1);
    EXPECT_FALSE(delay_line_->IsEmpty());
    EXPECT_EQ(count, 1);
  }
  count = delay_line_->Advance();
  EXPECT_EQ(dest, kNewValue2);
  EXPECT_EQ(count, 0);
  EXPECT_TRUE(delay_line_->IsEmpty());
}

}  // namespace
}  // namespace generic
}  // namespace sim
}  // namespace mpact
