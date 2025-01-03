// Copyright 2024 Google LLC
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

#ifndef MPACT_SIM_UTIL_ASM_RESOLVER_INTERFACE_H_
#define MPACT_SIM_UTIL_ASM_RESOLVER_INTERFACE_H_

#include <cstdint>

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

// This file defines the interface that the symbol resolver must implement. It
// is used by the SimpleAssembler to resolve symbol names to values.

namespace mpact {
namespace sim {
namespace util {
namespace assembler {

class ResolverInterface {
 public:
  virtual ~ResolverInterface() = default;
  virtual absl::StatusOr<uint64_t> Resolve(absl::string_view text) = 0;
};

}  // namespace assembler
}  // namespace util
}  // namespace sim
}  // namespace mpact

#endif  // MPACT_SIM_UTIL_ASM_RESOLVER_INTERFACE_H_
