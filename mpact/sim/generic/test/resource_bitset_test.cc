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

#include "mpact/sim/generic/resource_bitset.h"

#include <utility>

#include "googlemock/include/gmock/gmock.h"
#include "googletest/include/gtest/gtest.h"

// This file tests the methods of the ResourceBitSet class.

namespace {

using mpact::sim::generic::ResourceBitSet;

constexpr int kSmallSize = 23;
constexpr int kLargeSize = 223;

TEST(ResourceBitSet, Create) {
  ResourceBitSet bitset;
  EXPECT_EQ(bitset.GetOnesCount(), 0);
  EXPECT_FALSE(bitset.FindFirstSetBit(nullptr));
  ResourceBitSet bitset2(kSmallSize);
  EXPECT_EQ(bitset2.GetOnesCount(), 0);
  EXPECT_FALSE(bitset2.FindFirstSetBit(nullptr));
}

TEST(ResourceBitSet, SetBitSmall) {
  ResourceBitSet bitset(kSmallSize);
  EXPECT_EQ(bitset.GetOnesCount(), 0);
  for (int i = 0; i < kSmallSize; ++i) {
    bitset.Set(kSmallSize - 1 - i);
    EXPECT_EQ(bitset.GetOnesCount(), i + 1);
    int pos = 0;
    EXPECT_EQ(bitset.FindFirstSetBit(&pos), true);
    EXPECT_EQ(pos, kSmallSize - 1 - i);
  }
}

// Set bits in a multi-word set.
TEST(ResourceBitSet, SetBitLarge) {
  ResourceBitSet bitset(kLargeSize);
  EXPECT_EQ(bitset.GetOnesCount(), 0);
  int count = 0;
  for (int i = 0; i < kLargeSize; i += 5) {
    count++;
    bitset.Set(kLargeSize - 1 - i);
    EXPECT_EQ(bitset.GetOnesCount(), count) << "i: " << i;
    int pos = 0;
    EXPECT_EQ(bitset.FindFirstSetBit(&pos), true);
    EXPECT_EQ(pos, kLargeSize - 1 - i);
  }
}

TEST(ResourceBitSet, OtherConstructors) {
  ResourceBitSet bitset(kLargeSize);
  bitset.Set(150);
  // Copy constructor.
  ResourceBitSet bitset2(bitset);
  int pos;
  EXPECT_TRUE(bitset2.FindFirstSetBit(&pos));
  EXPECT_EQ(pos, 150);
  // Move constructor.
  ResourceBitSet bitset3(std::move(bitset2));
  EXPECT_TRUE(bitset3.FindFirstSetBit(&pos));
  EXPECT_EQ(pos, 150);
  // Assignment.
  ResourceBitSet bitset4;
  bitset4 = bitset3;
  EXPECT_TRUE(bitset4.FindFirstSetBit(&pos));
  EXPECT_EQ(pos, 150);
}

TEST(ResourceBitSet, FindNextSetBit) {
  ResourceBitSet bitset(kLargeSize);
  EXPECT_EQ(bitset.GetOnesCount(), 0);
  int count = 0;
  for (int i = 0; i < kLargeSize; i += 5) {
    count++;
    bitset.Set(i);
    int pos = 0;
    ASSERT_EQ(bitset.FindFirstSetBit(&pos), true);
    // Check that the next bits are in their expected positions.
    for (int j = 0; j < count - 1; ++j) {
      int next_pos = pos + 5;
      pos++;
      EXPECT_TRUE(bitset.FindNextSetBit(&pos));
      EXPECT_EQ(pos, next_pos);
    }
    // Check that no more bits are set.
    pos++;
    EXPECT_FALSE(bitset.FindNextSetBit(&pos));
  }
}

// Test or of two sets.
TEST(ResourceBitSet, Or) {
  ResourceBitSet small(kSmallSize);
  small.Set(10);
  int pos = 0;
  EXPECT_TRUE(small.FindFirstSetBit(&pos));
  EXPECT_EQ(pos, 10);
  ResourceBitSet large(kLargeSize);
  large.Set(72);
  EXPECT_TRUE(large.FindFirstSetBit(&pos));
  EXPECT_EQ(pos, 72);

  // Perform or.
  small.Or(large);
  EXPECT_EQ(small.GetOnesCount(), 2);
  EXPECT_TRUE(small.FindFirstSetBit(&pos));
  EXPECT_EQ(pos, 10);
  pos++;
  EXPECT_TRUE(small.FindNextSetBit(&pos));
  EXPECT_EQ(pos, 72);
  pos++;
  EXPECT_FALSE(small.FindNextSetBit(&pos));
  EXPECT_EQ(pos, 73);
}

TEST(ResourceBitSet, AndNot) {
  ResourceBitSet small(kSmallSize);
  small.Set(10);
  int pos = 0;
  EXPECT_TRUE(small.FindFirstSetBit(&pos));
  EXPECT_EQ(pos, 10);
  ResourceBitSet large(kLargeSize);
  large.Set(72);
  EXPECT_TRUE(large.FindFirstSetBit(&pos));
  EXPECT_EQ(pos, 72);

  // Or the sets.
  large.Or(small);

  // Set another bit in small.
  small.Set(5);
  // Now AndNot the small from the large.
  small.AndNot(large);
  // Bit should be cleared.
  EXPECT_EQ(small.GetOnesCount(), 1);
  EXPECT_TRUE(small.FindFirstSetBit(&pos));
  // First set should be 5.
  EXPECT_EQ(pos, 5);
  // AndNot the large with itself.
  large.AndNot(large);
  EXPECT_EQ(large.GetOnesCount(), 0);
}

}  // namespace
