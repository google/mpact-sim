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

#ifndef MPACT_SIM_DECODER_EXTRACT_H_
#define MPACT_SIM_DECODER_EXTRACT_H_

#include <cstdint>
#include <string>
#include <vector>

#include "absl/strings/string_view.h"

// This file defines utility functions for parsing a bit mask and creating an
// extraction recipe of shifts and masks, as well as a function to extract
// the bitfield value from a format using the shift recipe. A third function
// is defined to write out c code that performs the extraction.

namespace mpact {
namespace sim {
namespace decoder {
namespace bin_format {

// A single step of the extraction. (value >> shift) & mask.
struct ExtractionStep {
  uint64_t mask;
  int shift;
};

using ExtractionRecipe = std::vector<ExtractionStep>;

// Using the extraction recipe given, perform the extraction from the input
// value and return the result.
uint64_t ExtractValue(uint64_t value, const ExtractionRecipe &recipe);

// Using the extraction recipe given, return a string that has the C code for
// performing the extraction assuming the input value is stored in a variable
// with the name stored in 'value' and the result should be stored in variable
// named as stored in 'result'. Each line in the code is indented by 'indent'.
std::string WriteExtraction(const ExtractionRecipe &recipe,
                            absl::string_view value, absl::string_view result,
                            absl::string_view indent);

// Based on the bit mask passed in 'value', create a mask, shift and or recipe
// to extract the corresponding bits in a packed format without changing the
// order of the bits left to right.
// E.g. 1010 would produce the recipe:
//   shift right 1, mask 0b01
//   shift right 2, mask 0b10
ExtractionRecipe GetExtractionRecipe(uint64_t value);

}  // namespace bin_format
}  // namespace decoder
}  // namespace sim
}  // namespace mpact
#endif  // MPACT_SIM_DECODER_EXTRACT_H_
