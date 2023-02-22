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

#include <string>
#include <vector>

#include "absl/strings/str_cat.h"

namespace mpact {
namespace sim {
namespace decoder {
namespace bin_format {

uint64_t ExtractValue(uint64_t value,
                      const std::vector<ExtractionStep> &recipe) {
  uint64_t extracted_value = 0;
  for (auto [mask, shift] : recipe) {
    extracted_value |= ((value >> shift) & mask);
  }
  return extracted_value;
}

std::string WriteExtraction(const std::vector<ExtractionStep> &recipe,
                            absl::string_view value, absl::string_view result,
                            absl::string_view indent) {
  std::string output;
  std::string assign = " = ";
  for (auto [mask, shift] : recipe) {
    absl::StrAppend(&output, indent, result, assign, "(", value, " >> ", shift,
                    ") & 0x", absl::Hex(mask), ";\n");
    assign = " |= ";
  }
  return output;
}

std::vector<ExtractionStep> GetExtractionRecipe(uint64_t value) {
  std::vector<ExtractionStep> recipe;
  uint64_t mask = 0;
  int shift = 0;
  int width = 0;
  int total_width = 0;
  // Parse the input value. Process runs of 1s as single extractions.
  while (value != 0) {
    if (value & 1) {
      mask <<= 1;
      mask |= 1;
      width += 1;
    } else {
      if (mask != 0) {
        recipe.push_back(
            ExtractionStep{mask << total_width, shift - width - total_width});
        total_width += width;
      }
      mask = 0;
      width = 0;
    }
    shift++;
    value >>= 1;
  }
  if (mask != 0) {
    recipe.push_back(
        ExtractionStep{mask << total_width, shift - width - total_width});
  }
  return recipe;
}

}  // namespace bin_format
}  // namespace decoder
}  // namespace sim
}  // namespace mpact
