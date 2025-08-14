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
// handles three sections: .text, .data, and .bss. It produces either a
// relocatable file or an executable ELF file with the text section in its own
// segment starting at the base address, followed by the data section, and then
// the bss section. For the executable file, the entry point is set by calling
// SetEntryPoint().
//
// Only little-endian ELF files are currently supported.
//
// The ELF file class is specified in the constructor. Any other ELF header
// values have to be set using methods in the ELFIO writer that is accessed
// using the writer() method.
// At the very least, the following methods should be called:
//   writer().set_os_abi()
//   writer().set_machine()
// If additional sections need to be created, use the add_section() method of
// the writer (see ELFIO documentation for details).

namespace mpact {
namespace sim {
namespace util {
namespace assembler {

class SimpleAssembler {
 public:
  // The constructor takes the following parameters:
  //   comment: The comment string or character that starts a comment.
  //   elf_file_class: The ELF file class (32 or 64 bit). Use either ELFCLASS32
  //     or ELFCLASS64 from ELFIO.
  //   opcode_assembler_if: The opcode assembler interface to use for parsing
  //     and encoding assembly statements.
  SimpleAssembler(absl::string_view comment, int elf_file_class,
                  OpcodeAssemblerInterface* opcode_assembler_if);
  SimpleAssembler(const SimpleAssembler&) = delete;
  SimpleAssembler& operator=(const SimpleAssembler&) = delete;
  virtual ~SimpleAssembler();

  // Parse the input stream as assembly.
  absl::Status Parse(std::istream& is,
                     ResolverInterface* zero_resolver = nullptr);
  // Add the symbol to the symbol table for the current section. See ELFIO
  // documentation for details of the meaning of the parameters.
  absl::Status AddSymbolToCurrentSection(const std::string& name,
                                         ELFIO::Elf64_Addr value,
                                         ELFIO::Elf_Xword size, uint8_t type,
                                         uint8_t binding, uint8_t other);
  // Add the symbol to the symbol table for the named section. See ELFIO
  // documentation for details of the meaning of the parameters.
  absl::Status AddSymbol(const std::string& name, ELFIO::Elf64_Addr value,
                         ELFIO::Elf_Xword size, uint8_t type, uint8_t binding,
                         uint8_t other, const std::string& section_name);
  // Create executable ELF file with the given value as the entry point.
  // The text segment will be laid out starting at base address, followed by
  // the data segment.
  absl::Status CreateExecutable(uint64_t base_address,
                                const std::string& entry_point,
                                ResolverInterface* symbol_resolver = nullptr);
  absl::Status CreateExecutable(uint64_t base_address, uint64_t entry_point,
                                ResolverInterface* symbol_resolver = nullptr);
  // Create a relocatable ELF file.
  absl::Status CreateRelocatable(ResolverInterface* symbol_resolver = nullptr);
  // Write the ELF file to the given output stream.
  absl::Status Write(std::ostream& os);
  // Access the ELF writer.
  ELFIO::elfio& writer() { return writer_; }

  // Add a symbol reference to the symbol table if it is not already defined.
  void SimpleAddSymbol(absl::string_view name);

  // Getters and setters.
  absl::flat_hash_map<std::string, ELFIO::Elf_Word>& symbol_indices() {
    return symbol_indices_;
  }
  ELFIO::section* symtab() { return symtab_; }
  unsigned data_address_unit() { return data_address_unit_; }
  void set_data_address_unit(unsigned data_address_unit) {
    data_address_unit_ = data_address_unit;
  }

 private:
  // Helper function to update the symbol table entries.
  template <typename SymbolType>
  void UpdateSymbolsForExecutable(uint64_t text_segment_start,
                                  uint64_t data_segment_start,
                                  uint64_t bss_segment_start);
  template <typename SymbolType>
  void UpdateSymbolsForRelocatable();
  template <typename SymbolType>
  void UpdateSymtabHeaderInfo();
  // Perform second pass of parsing.
  absl::Status ParsePassTwo(std::vector<RelocationInfo>& relo_vector,
                            ResolverInterface* symbol_resolver);
  // Parse and process an assembly directive.
  absl::Status ParseAsmDirective(absl::string_view line, uint64_t address,
                                 ResolverInterface* resolver,
                                 std::vector<uint8_t>& byte_values,
                                 std::vector<RelocationInfo>& relocations);
  // Parse and process and assembly statement.
  absl::Status ParseAsmStatement(absl::string_view line, uint64_t address,
                                 ResolverInterface* resolver,
                                 std::vector<uint8_t>& byte_values,
                                 std::vector<RelocationInfo>& relocations);
  // Add the symbol to the symbol table.
  absl::Status AddSymbol(const std::string& name, ELFIO::Elf64_Addr value,
                         ELFIO::Elf_Xword size, uint8_t type, uint8_t binding,
                         uint8_t other, ELFIO::section* section);
  // Append the data to the current section.
  absl::Status AppendData(const char* data, size_t size);

  // Set the the given section as the current section. Create if it has not
  // already been created.
  void SetTextSection(const std::string& name);
  void SetDataSection(const std::string& name);
  void SetBssSection(const std::string& name);

  // ELF file class.
  int elf_file_class_ = 0;
  // Elf file top level object.
  ELFIO::elfio writer_;
  // The current section being processed.
  ELFIO::section* current_section_ = nullptr;
  // Map from section index to section pointer.
  absl::flat_hash_map<uint16_t, ELFIO::section*> section_index_map_;
  // Interface used to parse and encode assembly statements.
  OpcodeAssemblerInterface* opcode_assembler_if_ = nullptr;
  // Interface used to access strings in the string table.
  ELFIO::string_section_accessor* string_accessor_ = nullptr;
  // Interface used to access symbols in the symbol table.
  ELFIO::symbol_section_accessor* symbol_accessor_ = nullptr;
  // ELF symbol table section.
  ELFIO::section* symtab_ = nullptr;
  // Elf string table section.
  ELFIO::section* strtab_ = nullptr;
  // Map that tracks the current address of each section.
  absl::flat_hash_map<ELFIO::section*, uint64_t> section_address_map_;

  std::vector<std::string> lines_;
  // Section pointers.
  ELFIO::section* text_section_ = nullptr;
  ELFIO::section* data_section_ = nullptr;
  ELFIO::section* bss_section_ = nullptr;
  // Regular expressions used to parse the assembly source.
  RE2 comment_re_;
  RE2 asm_line_re_;
  RE2 directive_re_;
  // Set of symbol names declared as global.
  absl::flat_hash_set<std::string> global_symbols_;
  // Map from symbol name to symbol index in the symbol table.
  absl::flat_hash_map<std::string, ELFIO::Elf_Word> symbol_indices_;
  // Set of undefined symbols.
  absl::flat_hash_set<std::string> undefined_symbols_;
  // Data address unit - by default 1 for byte addressable.
  unsigned data_address_unit_ = 1;
};

}  // namespace assembler
}  // namespace util
}  // namespace sim
}  // namespace mpact

#endif  // MPACT_SIM_UTIL_ASM_SIMPLE_ASSEMBLER_H_
