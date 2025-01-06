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

#ifndef MPACT_SIM_UTIL_ASM_SIMPLE_ASSEMBLER_H_
#define MPACT_SIM_UTIL_ASM_SIMPLE_ASSEMBLER_H_

#include <cstddef>
#include <cstdint>
#include <istream>
#include <ostream>
#include <string>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "elfio/elf_types.hpp"
#include "elfio/elfio.hpp"
#include "elfio/elfio_section.hpp"
#include "elfio/elfio_strings.hpp"
#include "elfio/elfio_symbols.hpp"
#include "mpact/sim/util/asm/opcode_assembler_interface.h"
#include "mpact/sim/util/asm/resolver_interface.h"
#include "re2/re2.h"

// This file declares the SimpleAssembler class, which provides simple handling
// of assembly source, including a number of assembly directives. It currently
// handles three sections: .text, .data, and .bss. It produces an executable
// ELF file with the text section in its own segment starting at the base
// address, followed by the data section, and then the bss section. The entry
// point is set either by calling SetEntryPoint(), or by specifying the entry
// symbol with the .entry directive inside the text section of the input
// assembly source. If SetEntryPoint() is called after parsing it overrides the
// entry point set by the .entry directive.
namespace mpact {
namespace sim {
namespace util {
namespace assembler {

class SimpleAssembler {
 public:
  SimpleAssembler(int os_abi, int type, int machine, uint64_t base_address,
                  OpcodeAssemblerInterface *opcode_assembler_if);
  SimpleAssembler(const SimpleAssembler &) = delete;
  SimpleAssembler &operator=(const SimpleAssembler &) = delete;
  virtual ~SimpleAssembler();

  // Parse the input stream as assembly.
  absl::Status Parse(std::istream &is);
  // Set the entry point. Either pass a symbol or an address.
  absl::Status SetEntryPoint(const std::string &value);
  absl::Status SetEntryPoint(uint64_t value);
  // Write out the ELF file.
  absl::Status Write(std::ostream &os);

  ELFIO::elfio &writer() { return writer_; }

 private:
  // Parse and process an assembly directive.
  absl::Status ParseAsmDirective(absl::string_view directive,
                                 ResolverInterface *resolver,
                                 std::vector<uint8_t> &byte_values);
  // Parse and process and assembly statement.
  absl::Status ParseAsmStatement(absl::string_view statement,
                                 ResolverInterface *resolver,
                                 std::vector<uint8_t> &byte_values);
  // Add the symbol to the symbol table.
  absl::Status AddSymbol(const std::string &name, ELFIO::Elf64_Addr value,
                         ELFIO::Elf_Xword size, uint8_t type, uint8_t binding,
                         uint8_t other, ELFIO::section *section);
  // Append the data to the current section.
  absl::Status AppendData(const char *data, size_t size);

  // Set the the given section as the current section. Create if it has not
  // already been created.
  void SetTextSection(const std::string &name);
  void SetDataSection(const std::string &name);
  void SetBssSection(const std::string &name);

  // Elf file top level object.
  ELFIO::elfio writer_;
  // The current section being processed.
  ELFIO::section *current_section_ = nullptr;
  // Interface used to parse and encode assembly statements.
  OpcodeAssemblerInterface *opcode_assembler_if_ = nullptr;
  // Interface used to access strings in the string table.
  ELFIO::string_section_accessor *string_accessor_ = nullptr;
  // Interface used to access symbols in the symbol table.
  ELFIO::symbol_section_accessor *symbol_accessor_ = nullptr;
  // ELF symbol table section.
  ELFIO::section *symtab_ = nullptr;
  // Elf string table section.
  ELFIO::section *strtab_ = nullptr;
  // Map that tracks the current address of each section.
  absl::flat_hash_map<ELFIO::section *, uint64_t> section_address_map_;

  // Base address of the ELF file that is to be written.
  uint64_t base_address_ = 0;
  // Program entry point.
  std::string entry_point_;
  // Current symbol resolver (looks up symbols in the symbol table and returns
  // their values).
  ResolverInterface *symbol_resolver_ = nullptr;
  std::vector<std::string> lines_;
  // Regular expressions used to parse the assembly source.
  RE2 comment_re_;
  RE2 asm_line_re_;
  RE2 directive_re_;
  // Set of symbol names declared as global.
  absl::flat_hash_set<std::string> global_symbols_;
  // Map from symbol name to symbol index in the symbol table.
  absl::flat_hash_map<std::string, ELFIO::Elf_Xword> symbol_indices_;
};

}  // namespace assembler
}  // namespace util
}  // namespace sim
}  // namespace mpact

#endif  // MPACT_SIM_UTIL_ASM_SIMPLE_ASSEMBLER_H_
