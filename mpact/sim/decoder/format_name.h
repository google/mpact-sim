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

#ifndef MPACT_SIM_DECODER_FORMAT_NAME_H_
#define MPACT_SIM_DECODER_FORMAT_NAME_H_

#include <string>

#include "absl/strings/string_view.h"

namespace mpact {
namespace sim {
namespace machine_description {
namespace instruction_set {

// Function to convert from snake_case to PascalCase for output.
// TODO(torerik) Replace locally defined function with a library version
// if available.
// Function to convert to PascalCase.
std::string ToPascalCase(absl::string_view name);
// Function to convert to snake_case.
std::string ToSnakeCase(absl::string_view name);
// Function to generate header file guard name.
std::string ToHeaderGuard(absl::string_view name);
// Function to generate lower case version of name.
std::string ToLowerCase(absl::string_view name);
// Return a string with the number of spaces equal to the length of str.
std::string Indent(absl::string_view str);
// Return a string with the num spaces.
std::string Indent(int num);
}  // namespace instruction_set
}  // namespace machine_description
}  // namespace sim
}  // namespace mpact

#endif  // MPACT_SIM_DECODER_FORMAT_NAME_H_
