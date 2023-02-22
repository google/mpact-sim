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

#include "mpact/sim/decoder/format_name.h"

#include <string>

#include "absl/strings/string_view.h"

namespace mpact {
namespace sim {
namespace machine_description {
namespace instruction_set {

// Converts to PascalCase.
std::string ToPascalCase(absl::string_view name) {
  std::string formatted;
  bool capitalize = true;
  for (auto c : name) {
    if (c != '_') {
      if (capitalize) {
        c = toupper(c);
      }
      formatted.append(1, c);
    }
    capitalize = (c == '_');
  }
  return formatted;
}

// Converts to SnakeCase.
std::string ToSnakeCase(absl::string_view name) {
  std::string formatted;
  for (auto c : name) {
    if (!formatted.empty() && isupper(c)) {
      formatted.append(1, '_');
    }
    formatted.append(1, tolower(c));
  }
  return formatted;
}

// convert to guard name.
std::string ToHeaderGuard(absl::string_view name) {
  std::string formatted;
  for (auto c : name) {
    if ((c == '/') || (c == '.')) {
      formatted.append(1, '_');
    } else {
      formatted.append(1, toupper(c));
    }
  }
  return formatted;
}

// Convert to lower case.
std::string ToLowerCase(absl::string_view name) {
  std::string formatted;
  for (auto c : name) formatted.append(1, tolower(c));
  return formatted;
}

std::string Indent(absl::string_view str) {
  return std::string(str.size(), ' ');
}

std::string Indent(int num) { return std::string(num, ' '); }

}  // namespace instruction_set
}  // namespace machine_description
}  // namespace sim
}  // namespace mpact
