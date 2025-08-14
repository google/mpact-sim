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

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "absl/functional/any_invocable.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "elfio/elf_types.hpp"
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
  RelocationInfo(uint64_t offset, const std::string& symbol, uint32_t type,
                 uint64_t addend, uint16_t section_index)
      : offset(offset),
        symbol(symbol),
        type(type),
        addend(addend),
        section_index(section_index) {}
};

class OpcodeAssemblerInterface {
 public:
  virtual ~OpcodeAssemblerInterface() = default;
  using AddSymbolCallback = absl::AnyInvocable<absl::Status(
      const std::string&, ELFIO::Elf64_Addr /*value*/,
      ELFIO::Elf_Xword /*size*/, uint8_t /*type*/, uint8_t /*binding*/,
      uint8_t /*other*/)>;
  // Takes the current address, the text for the assembly instruction (including
  // any label definitions), and a symbol resolver interface.Return ok status if
  // the text is successfully encoded into the bytes vector. Symbols for any
  // labels are added using the callback function interface. The method returns
  // the increment to the address after the instruction is encoded.
  virtual absl::StatusOr<size_t> Encode(
      uint64_t address, absl::string_view text,
      AddSymbolCallback add_symbol_callback, ResolverInterface* resolver,
      std::vector<uint8_t>& bytes,
      std::vector<RelocationInfo>& relocations) = 0;
};

}  // namespace assembler
}  // namespace util
}  // namespace sim
}  // namespace mpact

#endif  // MPACT_SIM_UTIL_ASM_OPCODE_ASSEMBLER_INTERFACE_H_
