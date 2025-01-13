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

#ifndef MPACT_SIM_UTIL_ASM_OPCODE_ASSEMBLER_INTERFACE_H_
#define MPACT_SIM_UTIL_ASM_OPCODE_ASSEMBLER_INTERFACE_H_

#include <cstdint>
#include <string>
#include <vector>

#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "mpact/sim/util/asm/resolver_interface.h"

// This file defines the interface that the opcode assembler must implement. It
// is used by the SimpleAssembler to parse an assembly source line and convert
// it into a vector of bytes.

namespace mpact {
namespace sim {
namespace util {
namespace assembler {

struct RelocationInfo {
  uint64_t offset;
  std::string symbol;
  uint32_t type;
  uint64_t addend;
  uint16_t section_index;
};

class OpcodeAssemblerInterface {
 public:
  virtual ~OpcodeAssemblerInterface() = default;
  // Takes the current address, the text for the assembly instruction, and a
  // symbol resolver interface.Return ok status if the text is successfully
  // encoded into the bytes vector.
  virtual absl::Status Encode(uint64_t address, absl::string_view text,
                              ResolverInterface *resolver,
                              std::vector<uint8_t> &bytes,
                              std::vector<RelocationInfo> &relocations) = 0;
};

}  // namespace assembler
}  // namespace util
}  // namespace sim
}  // namespace mpact

#endif  // MPACT_SIM_UTIL_ASM_OPCODE_ASSEMBLER_INTERFACE_H_
