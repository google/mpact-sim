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

#include "mpact/sim/decoder/extract.h"

#include <ios>

#include "absl/strings/str_cat.h"
#include "googlemock/include/gmock/gmock.h"
#include "googletest/include/gtest/gtest.h"

namespace {

using ::mpact::sim::decoder::bin_format::ExtractValue;
using ::mpact::sim::decoder::bin_format::GetExtractionRecipe;
using ::mpact::sim::decoder::bin_format::WriteExtraction;

constexpr uint64_t kMask = 0b1111'0111'011'01'0ULL;
constexpr uint64_t kInputValue = 0b1010'0101'010'10'0ULL;
constexpr uint64_t kOutputValue = 0b1010'101'10'0ULL;

TEST(ExtractTest, RecipeZero) {
  // No elements in the recipe for a zero mask.
  auto recipe = GetExtractionRecipe(0);
  EXPECT_EQ(recipe.size(), 0);
}

TEST(ExtractTest, RecipeOne) {
  // Single bit mask.
  for (int i = 0; i < 4; i++) {
    uint64_t mask = 1 << (8 * i);
    auto recipe = GetExtractionRecipe(mask);
    EXPECT_EQ(recipe.size(), 1);
    EXPECT_EQ(recipe[0].shift, 8 * i);
    EXPECT_EQ(recipe[0].mask, 1);
  }
}

TEST(ExtractTest, RecipleMultiBitContiguous) {
  // Contiguous multi bit mask.
  for (int i = 0; i < 4; i++) {
    // Mask is 3, 7, 11, and 15 bits wide.
    uint64_t mask = ((1 << (3 + 4 * i)) - 1) << (4 * i);
    auto recipe = GetExtractionRecipe(mask);
    EXPECT_EQ(recipe.size(), 1);
    EXPECT_EQ(recipe[0].shift, 4 * i);
    EXPECT_EQ(recipe[0].mask, mask >> recipe[0].shift);
  }
}

TEST(ExtractTest, RecipleMultiField) {
  for (int i = 0; i < 4; i++) {
    // Bit fields of length 1, 2, 3, and 4.
    uint64_t mask = kMask << (4 * i);
    auto recipe = GetExtractionRecipe(mask);
    EXPECT_EQ(recipe.size(), 4);
    int width = 0;
    for (int j = 0; j < 4; j++) {
      // The shifts should be 4 * i + the bit field lengths (1, 2, 3, 4).
      EXPECT_EQ(recipe[j].shift, 4 * i + (1 + j));
      EXPECT_EQ(recipe[j].mask, ((1 << (j + 1)) - 1) << width);
      width += j + 1;
    }
  }
}

TEST(ExtractTest, ExtractValue) {
  auto recipe = GetExtractionRecipe(kMask);
  auto result = ExtractValue(kInputValue, recipe);
  EXPECT_EQ(result, kOutputValue);
}

TEST(ExtractTest, WriteExtract) {
  auto recipe = GetExtractionRecipe(kMask);
  auto output = WriteExtraction(recipe, "value", "result", "  ");
  for (int j = 0; j < 4; j++) {
    EXPECT_THAT(output, testing::HasSubstr(absl::StrCat(
                            "= (value >> ", recipe[j].shift, ") & 0x",
                            absl::Hex(recipe[j].mask), ";")));
  }
}

}  // namespace
