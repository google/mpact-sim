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

#include "mpact/sim/util/asm/simple_assembler.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <istream>
#include <ostream>
#include <string>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/functional/bind_front.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "elfio/elf_types.hpp"
#include "elfio/elfio.hpp"  // IWYU pragma: keep
#include "elfio/elfio_section.hpp"
#include "elfio/elfio_segment.hpp"
#include "elfio/elfio_strings.hpp"
#include "elfio/elfio_symbols.hpp"
#include "mpact/sim/util/asm/opcode_assembler_interface.h"
#include "mpact/sim/util/asm/resolver.h"
#include "mpact/sim/util/asm/resolver_interface.h"
#include "re2/re2.h"

namespace mpact {
namespace sim {
namespace util {
namespace assembler {

// Helper functions for parsing the assembly code.
namespace {

// This template is used to convert the given type to the smallest valid type
// that absl Atoi functions can handle.
template <typename T>
struct AtoIType {
  using type = T;
};

template <>
struct AtoIType<char> {
  using type = int32_t;
};

template <>
struct AtoIType<uint8_t> {
  using type = uint32_t;
};

template <>
struct AtoIType<uint16_t> {
  using type = uint32_t;
};

template <>
struct AtoIType<int16_t> {
  using type = int32_t;
};

template <>
struct AtoIType<int8_t> {
  using type = int32_t;
};

// Convert the text to an integer. Checks for a leading 0x and then converts
// using absl::SimpleHexAtoi. If the text does not start with 0x, then it
// converts using absl::SimpleAtoi. If the text is not a valid integer, then
// it calls the resolver to see if it is a symbol name, in which case it returns
// the value of the symbol. If the text is not a valid integer or symbol name,
// then it returns an error.
template <typename T>
absl::StatusOr<T> SimpleTextToInt(absl::string_view text,
                                  ResolverInterface *resolver = nullptr) {
  T value;
  if (text.substr(0, 2) == "0x") {
    if (absl::SimpleHexAtoi(text.substr(2), &value)) return value;
    return absl::InvalidArgumentError(
        absl::StrCat("Invalid immediate: ", text));
  }
  if (absl::SimpleAtoi(text, &value)) return value;
  if (resolver == nullptr) {
    return absl::InvalidArgumentError(absl::StrCat("Invalid argument: ", text));
  }
  auto result = resolver->Resolve(text);
  if (!result.ok()) {
    return absl::InvalidArgumentError(absl::StrCat("Invalid argument: ", text));
  }
  return static_cast<T>(result.value());
}

// Expand escaped characters in the given text. This is for use in parsing
// .string, .char, and .cstring directives.
std::string ExpandEscapes(absl::string_view text) {
  std::string result;
  bool in_escape = false;
  for (auto c : text) {
    if (in_escape) {
      switch (c) {
        case 'n':
          result.push_back('\n');
          break;
        case 'r':
          result.push_back('\r');
          break;
        case 'v':
          result.push_back('\v');
          break;
        case 'f':
          result.push_back('\f');
          break;
        case 'a':
          result.push_back('\a');
          break;
        case 'b':
          result.push_back('\b');
          break;
        case 't':
          result.push_back('\t');
          break;
        case '\\':
          result.push_back('\\');
          break;
        case '\'':
          result.push_back('\'');
          break;
        case '"':
          result.push_back('"');
          break;
        case '\?':
          result.push_back('?');
          break;
        default:
          result.push_back('\\');
          result.push_back(c);
          break;
      }
      in_escape = false;
      continue;
    }
    if (c == '\\') {
      in_escape = true;
      continue;
    }
    result.push_back(c);
  }
  if (in_escape) result.push_back('\\');
  return result;
}

// This function is used to parse a list of values from the remainder of an
// assembly directive. The values are separated by commas. The type T is the
// type of the values, and must be an integer type or char. The resolver
// interface is optional and is used to resolve any symbol names in the text.
template <typename T>
absl::StatusOr<std::vector<T>> GetValues(
    absl::string_view remainder, ResolverInterface *resolver = nullptr) {
  std::vector<T> values;
  static RE2 value_re("(0x[0-9a-fA-F]+|-?[0-9]+)\\s*(?:,|$)");
  std::string match;
  while (RE2::Consume(&remainder, value_re, &match)) {
    auto result = SimpleTextToInt<typename AtoIType<T>::type>(match);
    if (!result.ok()) return result.status();
    T value = static_cast<T>(result.value());
    values.push_back(value);
  }
  return values;
}

// Specialization of the above that handles char values.
template <>
absl::StatusOr<std::vector<char>> GetValues<char>(absl::string_view remainder,
                                                  ResolverInterface *resolver) {
  std::vector<char> values;
  static RE2 value_re("'(.{1,2})'\\s*(?:,|$)");
  std::string match;
  while (RE2::Consume(&remainder, value_re, &match)) {
    auto expanded = ExpandEscapes(match);
    if (expanded.size() != 1)
      return absl::InvalidArgumentError(
          absl::StrCat("Invalid character: '", match, "'"));
    values.push_back(expanded[0]);
  }
  return values;
}

// Specialization of the above that handles double quoted string values.
template <>
absl::StatusOr<std::vector<std::string>> GetValues<std::string>(
    absl::string_view remainder, ResolverInterface *resolver) {
  std::vector<std::string> values;
  std::string match;
  static RE2 value_re("\"([^\"]*)\"\\s*(?:,|$)");
  while (RE2::Consume(&remainder, value_re, &match)) {
    values.push_back(ExpandEscapes(match));
  }
  return values;
}

// Specialization of the above that handles labels (string values without
// quotes).
absl::StatusOr<std::vector<std::string>> GetLabels(
    absl::string_view remainder) {
  std::vector<std::string> values;
  std::string match;
  static RE2 label_re("([a-zA-Z_][a-zA-Z0-9_]*)\\s*(?:,|$)");
  while (RE2::Consume(&remainder, label_re, &match)) {
    values.push_back(match);
  }
  return values;
}

// Helper that converts a vector of integer values to a vector of bytes.
template <typename T>
inline void ConvertToBytes(const std::vector<T> &values,
                           std::vector<uint8_t> &bytes) {
  union {
    T i;
    uint8_t b[sizeof(T)];
  } u;
  for (auto value : values) {
    u.i = value;
    for (int i = sizeof(T) - 1; i >= 0; i--) {
      bytes.push_back(u.b[i]);
    }
  }
}

}  // namespace

SimpleAssembler::SimpleAssembler(absl::string_view comment, int elf_file_class,
                                 OpcodeAssemblerInterface *opcode_assembler_if)
    : elf_file_class_(elf_file_class),
      opcode_assembler_if_(opcode_assembler_if),
      comment_re_(absl::StrCat("^(.*?)(?:", comment, ".*?)?(\\\\)?$")),
      asm_line_re_("^(?:(?:(\\S+)\\s*:)?|\\s)\\s*(.*)\\s*$"),
      directive_re_(
          "^\\.(align|bss|bytes|char|cstring|data|global|long|sect"
          "|short|space|string|type|text|uchar|ulong|ushort|uword|word)(?:\\s+("
          ".*)"
          ")?\\s*"
          "$") {
  // Configure the ELF file writer.
  writer_.create(elf_file_class_, ELFDATA2LSB);
  writer_.set_os_abi(ELFOSABI_NONE);
  writer_.set_machine(EM_NONE);
  // Create the symbol table section.
  symtab_ = writer_.sections.add(".symtab");
  section_index_map_.insert({symtab_->get_index(), symtab_});
  symtab_->set_type(SHT_SYMTAB);
  symtab_->set_addr_align(0x8);
  symtab_->set_entry_size(elf_file_class_ == ELFCLASS64
                              ? sizeof(ELFIO::Elf64_Sym)
                              : sizeof(ELFIO::Elf32_Sym));
  // Create the string table section.
  strtab_ = writer_.sections.add(".strtab");
  section_index_map_.insert({strtab_->get_index(), strtab_});
  strtab_->set_type(SHT_STRTAB);
  strtab_->set_addr_align(0x1);
  // Link the symbol table to the string table.
  symtab_->set_link(strtab_->get_index());
  // Create the symbol and string table accessors.
  symbol_accessor_ = new ELFIO::symbol_section_accessor(writer_, symtab_);
  string_accessor_ =
      new ELFIO::string_section_accessor(writer_.sections[".strtab"]);
  // Create .text, .data. and .bss sections.
  SetTextSection(".text");
  SetDataSection(".data");
  SetBssSection(".bss");
  // Clear the current section.
  current_section_ = nullptr;
}

SimpleAssembler::~SimpleAssembler() {
  delete symbol_accessor_;
  symbol_accessor_ = nullptr;
  delete string_accessor_;
  string_accessor_ = nullptr;
}

absl::Status SimpleAssembler::Parse(std::istream &is,
                                    ResolverInterface *zero_resolver) {
  // A trivial symbol resolver that always returns 0.
  bool own_zero_resolver = false;
  std::function<void()> cleanup = []() {};
  if (zero_resolver == nullptr) {
    zero_resolver = new ZeroResolver(
        absl::bind_front(&SimpleAssembler::SimpleAddSymbol, this));
    own_zero_resolver = true;
    cleanup = [zero_resolver]() { delete zero_resolver; };
  }
  // First pass of parsing the input stream. This will add symbols to the symbol
  // table and compute the sizes of all instructions and the sections. The
  // section_address_map_ will keep track of the current location within each
  // section (i.e., the offset within the section of the next
  // instruction/object).
  std::string label;
  std::string statement;
  while (is.good() && !is.eof()) {
    std::string line;
    while (true) {
      std::string tmp;
      if (!is.good() || is.eof()) break;
      getline(is, tmp);
      std::string prefix;
      std::string suffix;
      // Remove comments from the input line.
      if (!RE2::FullMatch(tmp, comment_re_, &prefix, &suffix)) {
        return absl::InternalError("Failed to parse comment");
      }
      tmp = absl::StrCat(prefix, suffix);
      int len = tmp.length();
      // If there is an escaped newline then append the line, up to the  '\',
      // and continue.
      if ((len >= 1) && (tmp[len - 1] == '\\')) {
        // Insert the escaped newline that getline removed.
        absl::StrAppend(&line, tmp, "\n");
        continue;
      }
      absl::StrAppend(&line, tmp);
      break;
    }
    if (line.empty()) continue;
    // Parse the line into a label and a statement. This is done to determine if
    // the line contains a label, only a label, or if the statement is directive
    // or not.
    if (RE2::FullMatch(line, asm_line_re_, &label, &statement)) {
      std::vector<uint8_t> byte_vector;
      std::vector<RelocationInfo> relo_vector;
      auto *section = current_section_;
      uint64_t address =
          (section == nullptr) ? 0 : section_address_map_[section];
      if (!statement.empty()) {
        absl::Status status;
        // Pass the full line into the parse functions, they are responsible
        // for handling the labels in pass one.
        if (statement[0] == '.') {
          status = ParseAsmDirective(line, address, zero_resolver, byte_vector,
                                     relo_vector);
        } else {
          status = ParseAsmStatement(line, address, zero_resolver, byte_vector,
                                     relo_vector);
        }
        if (!status.ok()) return status;
        // Save the statements for processing in pass two (labels are all
        // processed in pass one).
        lines_.push_back(statement);
      } else if (!label.empty()) {
        // This is just a single label definition. Add it to the symbol table.
        auto status =
            AddSymbolToCurrentSection(label, address, 0, STT_NOTYPE, 0, 0);
        if (!status.ok()) return status;
      }
      continue;
    }
    // Parse failure.
    cleanup();
    return absl::AbortedError(absl::StrCat("Parse failure: '", line, "'"));
  }

  if (!is.eof()) {
    cleanup();
    return absl::InternalError("Input stream entered bad state");
  }

  // Add undefined symbols to the symbol table.
  for (auto const &symbol : undefined_symbols_) {
    auto status = AddSymbol(symbol, 0, 0, STT_NOTYPE, 0, 0, nullptr);
    if (!status.ok()) {
      cleanup();
      return absl::InternalError(absl::StrCat(
          "Failed to add undefined symbol '", symbol, "': ", status.message()));
    }
  }
  undefined_symbols_.clear();

  if (bss_section_ != nullptr) {
    bss_section_->set_size(section_address_map_[bss_section_]);
  }
  cleanup();
  return absl::OkStatus();
}

absl::Status SimpleAssembler::CreateExecutable(
    uint64_t base_address, uint64_t entry_point,
    ResolverInterface *symbol_resolver) {
  return CreateExecutable(base_address, absl::StrCat(entry_point),
                          symbol_resolver);
}

// Helper function to update the symbol table entries for an executable file.
template <typename SymbolType>
void SimpleAssembler::UpdateSymbolsForExecutable(uint64_t text_segment_start,
                                                 uint64_t data_segment_start,
                                                 uint64_t bss_segment_start) {
  auto num_symbols = symtab_->get_size() / sizeof(SymbolType);
  auto size = num_symbols * sizeof(SymbolType);
  auto *symbols = new SymbolType[num_symbols];
  std::memcpy(symbols, symtab_->get_data(), size);
  for (int i = 0; i < num_symbols; ++i) {
    auto &sym = symbols[i];
    auto shndx = sym.st_shndx;
    std::string name = string_accessor_->get_string(sym.st_name);
    if (global_symbols_.contains(name)) {
      sym.st_info = ELF_ST_INFO(STB_GLOBAL, ELF_ST_TYPE(sym.st_info));
    }
    if ((text_section_ != nullptr) && (shndx == text_section_->get_index())) {
      sym.st_value += text_segment_start;
    } else if ((data_section_ != nullptr) &&
               (shndx == data_section_->get_index())) {
      sym.st_value += data_segment_start;
    } else if ((bss_section_ != nullptr) &&
               (shndx == bss_section_->get_index())) {
      sym.st_value += bss_segment_start;
    }
  }
  symtab_->set_data(reinterpret_cast<char *>(symbols), size);
  delete[] symbols;
}

template <typename SymbolType>
void SimpleAssembler::UpdateSymbolsForRelocatable() {
  auto num_symbols = symtab_->get_size() / sizeof(SymbolType);
  auto size = num_symbols * sizeof(SymbolType);
  auto *symbols = new SymbolType[num_symbols];
  std::memcpy(symbols, symtab_->get_data(), size);
  for (int i = 0; i < num_symbols; ++i) {
    auto &sym = symbols[i];
    std::string name = string_accessor_->get_string(sym.st_name);
    if (global_symbols_.contains(name)) {
      sym.st_info = ELF_ST_INFO(STB_GLOBAL, ELF_ST_TYPE(sym.st_info));
    }
  }
  symtab_->set_data(reinterpret_cast<char *>(symbols), size);
  delete[] symbols;
}

absl::Status SimpleAssembler::CreateExecutable(
    uint64_t base_address, const std::string &entry_point,
    ResolverInterface *symbol_resolver) {
  LOG(INFO) << "CreateExecutable";
  if (!undefined_symbols_.empty()) {
    std::string message;
    absl::StrAppend(
        &message,
        "Cannot create executable with the following undefined symbols: ");
    for (auto const &symbol : undefined_symbols_) {
      absl::StrAppend(&message, "    ", symbol, "\n");
    }
    return absl::InvalidArgumentError(message);
  }
  LOG(INFO) << "set type to ET_EXEC";
  writer_.set_type(ET_EXEC);
  // Section sizes are now known. So let's compute the layout and update all
  // the symbol values/addresses before the next pass.
  // The layout is:
  //   text segment starting at base address + any alignment.
  //   data segment starting at the end of the text segment + any alignment.
  // The bss section is added to the end of the data segment + any alignment.

  ELFIO::segment *text_segment = nullptr;
  uint64_t text_segment_start = 0;
  if (text_section_ != nullptr) {
    text_segment_start = base_address & ~4095ULL;
    text_segment = writer_.segments.add();
    if (text_segment == nullptr) {
      return absl::InternalError("Failed to create elf segment for text");
    }
    text_segment->set_type(PT_LOAD);
    text_segment->set_virtual_address(text_segment_start);
    text_segment->set_physical_address(text_segment_start);
    text_segment->set_flags(PF_X | PF_R);
    text_segment->set_align(4096);
  }

  ELFIO::segment *data_segment = nullptr;
  uint64_t data_segment_start = 0;
  uint64_t bss_segment_start = 0;
  if ((data_section_ != nullptr) || (bss_section_ != nullptr)) {
    data_segment_start =
        (text_segment_start + section_address_map_[text_section_] + 4095) &
        ~4095ULL;

    ELFIO::segment *data_segment = writer_.segments.add();
    if (data_segment == nullptr) {
      return absl::InternalError("Failed to create elf segment for data");
    }
    data_segment->set_type(PT_LOAD);
    data_segment->set_virtual_address(data_segment_start);
    data_segment->set_physical_address(data_segment_start);
    data_segment->set_flags(PF_W | PF_R);
    data_segment->set_align(4096);

    uint64_t bss_align = bss_section_->get_addr_align() - 1;
    bss_segment_start =
        (data_segment_start + section_address_map_[data_section_] + bss_align) &
        ~bss_align;
  }

  // Now we can update the symbol table based on the new section sizes.

  // Different size symbol table entries for 32 and 64 bit ELF files.
  if (elf_file_class_ == ELFCLASS64) {
    UpdateSymbolsForExecutable<ELFIO::Elf64_Sym>(
        text_segment_start, data_segment_start, bss_segment_start);
  } else if (elf_file_class_ == ELFCLASS32) {
    UpdateSymbolsForExecutable<ELFIO::Elf32_Sym>(
        text_segment_start, data_segment_start, bss_segment_start);
  } else {
    return absl::InternalError(
        absl::StrCat("Unsupported ELF file class: ", elf_file_class_));
  }

  // Update the section address map so that each section starts at the right
  // address, i.e., it no longer tracks the offset within each section, but the
  // absolute address.
  section_address_map_[text_section_] = text_segment_start;
  section_address_map_[data_section_] = data_segment_start;
  section_address_map_[bss_section_] = bss_segment_start;

  std::function<void()> cleanup = []() {};
  if (symbol_resolver == nullptr) {
    symbol_resolver =
        new SymbolResolver(elf_file_class_, symtab_, symbol_indices_);
    cleanup = [symbol_resolver]() { delete symbol_resolver; };
  }
  // Pass in the relocation vector to the second pass of parsing, but ignore
  // the values, since we are creating an executable file, and all the symbols
  // are resolved.
  std::vector<RelocationInfo> relo_vector;
  auto status = ParsePassTwo(relo_vector, symbol_resolver);
  if (!status.ok()) {
    cleanup();
    return status;
  }

  // Add sections to the segments. First segment gets the text section. The
  // second segment gets the data and bss sections.
  if (text_segment != nullptr) {
    text_segment->add_section_index(text_section_->get_index(),
                                    text_section_->get_addr_align());
  }
  if (data_segment != nullptr) {
    data_segment->add_section_index(data_section_->get_index(),
                                    data_section_->get_addr_align());
    data_segment->add_section_index(bss_section_->get_index(),
                                    bss_section_->get_addr_align());
  }

  auto res = SimpleTextToInt<uint64_t>(entry_point, symbol_resolver);
  if (!res.ok()) {
    cleanup();
    return res.status();
  }
  uint64_t entry_point_value = res.value();

  symbol_accessor_->arrange_local_symbols();
  writer_.set_entry(entry_point_value);
  cleanup();
  return absl::OkStatus();
}

namespace {

// Helper function to add a relocation entry to a relocation section.
template <typename RelocaType>
absl::Status AddRelocationEntries(
    const std::vector<RelocationInfo> &relo_vector,
    absl::flat_hash_map<std::string, ELFIO::Elf_Word> &symbol_indices,
    ELFIO::section *reloca_section) {
  for (auto const &relo : relo_vector) {
    RelocaType rela;
    rela.r_offset = relo.offset;
    rela.r_addend = relo.addend;
    auto iter = symbol_indices.find(relo.symbol);
    if (iter == symbol_indices.end()) {
      return absl::InvalidArgumentError(
          absl::StrCat("Symbol '", relo.symbol, "' not found"));
    }
    if (sizeof(RelocaType) == sizeof(ELFIO::Elf64_Rela)) {
      rela.r_info = ELF64_R_INFO(iter->second, relo.type);
    } else {
      rela.r_info = ELF32_R_INFO(iter->second, relo.type);
    }
    reloca_section->append_data(reinterpret_cast<const char *>(&rela),
                                sizeof(RelocaType));
  }
  return absl::OkStatus();
}

}  // namespace

template <typename SymbolType>
void SimpleAssembler::UpdateSymtabHeaderInfo() {
  int last_local = 0;
  auto syms =
      absl::MakeSpan(reinterpret_cast<const SymbolType *>(symtab_->get_data()),
                     symtab_->get_size() / sizeof(SymbolType));
  for (int i = 0; i < syms.size(); ++i) {
    auto name = string_accessor_->get_string(syms[i].st_name);
    symbol_indices_.insert({name, i});
    if (ELF_ST_BIND(syms[i].st_info) == STB_LOCAL) last_local = i;
  }
  symtab_->set_info(last_local + 1);
}

absl::Status SimpleAssembler::CreateRelocatable(
    ResolverInterface *symbol_resolver) {
  writer_.set_type(ET_REL);
  // Reset the section address map to zero since we are creating a relocatable
  // file.
  section_address_map_[text_section_] = 0;
  section_address_map_[data_section_] = 0;
  section_address_map_[bss_section_] = 0;

  // Since the symbols now are rearranged, we need to set global symbols flag
  // for those in the global_symbols_ set.
  // Different size symbol table entries for 32 and 64 bit ELF files.
  if (elf_file_class_ == ELFCLASS64) {
    UpdateSymbolsForRelocatable<ELFIO::Elf64_Sym>();
  } else if (elf_file_class_ == ELFCLASS32) {
    UpdateSymbolsForRelocatable<ELFIO::Elf32_Sym>();
  } else {
    return absl::InternalError(
        absl::StrCat("Unsupported ELF file class: ", elf_file_class_));
  }
  // Rearrange local symbols in the symbol table so that they are at the
  // beginning (ELF requirement).
  symbol_accessor_->arrange_local_symbols(nullptr);
  // Find the last local symbol and set the section header info for symbtab
  // to point to 1 past that. Update the symbol_indices_ map.
  symbol_indices_.clear();
  if (elf_file_class_ == ELFCLASS64) {
    UpdateSymtabHeaderInfo<ELFIO::Elf64_Sym>();
  } else {
    UpdateSymtabHeaderInfo<ELFIO::Elf32_Sym>();
  }

  std::function<void()> cleanup = []() {};
  if (symbol_resolver == nullptr) {
    symbol_resolver =
        new SymbolResolver(elf_file_class_, symtab_, symbol_indices_);
    cleanup = [symbol_resolver]() { delete symbol_resolver; };
  }
  // Parse the source again, collect relocations.
  std::vector<RelocationInfo> relo_vector;
  auto status = ParsePassTwo(relo_vector, symbol_resolver);
  if (!status.ok()) {
    cleanup();
    return status;
  }

  // Handle relocations if there are any.
  if (!relo_vector.empty()) {
    // First scan through the entries relocation vector and group them by
    // the section in which the relocation is to be applied.
    absl::flat_hash_map<uint16_t, std::vector<RelocationInfo>> relo_map;
    for (auto const &relo : relo_vector) {
      relo_map[relo.section_index].push_back(relo);
    }
    for (auto const &[section_index, relo_vec] : relo_map) {
      if (section_index == 0) {
        cleanup();
        return absl::InternalError(
            "Relocation entry with section index 0 not supported");
      }
      if (!section_index_map_.contains(section_index)) {
        cleanup();
        return absl::InternalError(
            absl::StrCat("Section index not found: ", section_index));
      }
      // Now, create a relocation section for each key in the map.
      std::string name =
          absl::StrCat(".rela", section_index_map_[section_index]->get_name());
      auto *rela_section = writer_.sections.add(name);
      rela_section->set_type(SHT_RELA);
      rela_section->set_flags(SHF_INFO_LINK);
      rela_section->set_entry_size(elf_file_class_ == ELFCLASS64
                                       ? sizeof(ELFIO::Elf64_Rela)
                                       : sizeof(ELFIO::Elf32_Rela));
      rela_section->set_link(symtab_->get_index());
      rela_section->set_info(text_section_->get_index());
      rela_section->set_addr_align(8);
      // Process the relocation vector entries.
      absl::Status status;
      if (elf_file_class_ == ELFCLASS64) {
        status = AddRelocationEntries<ELFIO::Elf64_Rela>(
            relo_vec, symbol_indices_, rela_section);
      } else if (elf_file_class_ == ELFCLASS32) {
        status = AddRelocationEntries<ELFIO::Elf32_Rela>(
            relo_vec, symbol_indices_, rela_section);
      } else {
        cleanup();
        return absl::InternalError(
            absl::StrCat("Unsupported ELF file class: ", elf_file_class_));
      }
      if (!status.ok()) {
        cleanup();
        return status;
      }
    }
  }
  cleanup();
  return absl::OkStatus();
}

absl::Status SimpleAssembler::ParsePassTwo(
    std::vector<RelocationInfo> &relo_vector,
    ResolverInterface *symbol_resolver) {
  // Now fill in the sections. Parse each of the lines saved in the first
  // pass.
  for (auto const &line : lines_) {
    std::vector<uint8_t> byte_vector;
    absl::Status status;
    auto *section = current_section_;
    auto relo_size = relo_vector.size();
    auto address = section_address_map_[section];
    if (line[0] == '.') {
      auto status = ParseAsmDirective(line, address, symbol_resolver,
                                      byte_vector, relo_vector);
    } else {
      auto status = ParseAsmStatement(line, address, symbol_resolver,
                                      byte_vector, relo_vector);
    }
    if (!status.ok()) return status;
    // Update section information in the relocation vector.
    for (int i = relo_size; i < relo_vector.size(); ++i) {
      relo_vector[i].section_index = section->get_index();
      relo_vector[i].offset = address;
    }
    // Go to the next line if there is no data to add to the section.
    if (byte_vector.empty()) continue;
    // Add data to the section, but first make sure it's not bss.
    if (section != bss_section_) {
      if (section == nullptr) {
        return absl::InternalError("Data is added to a null section");
      }
      section->append_data(reinterpret_cast<const char *>(byte_vector.data()),
                           byte_vector.size());
    }
  }
  return absl::OkStatus();
}

// Top level function that writes the ELF file out to disk.
absl::Status SimpleAssembler::Write(std::ostream &os) {
  writer_.save(os);
  return absl::OkStatus();
}

// Parse and process an assembly directive. The assembly directive is
// expected to be in the form of a line starting with a period followed by a
// directive name and an optional argument. The argument is a string of
// tokens separated by spaces. The argument is parsed using regular
// expressions. The byte values are appended to the given vector.
absl::Status SimpleAssembler::ParseAsmDirective(
    absl::string_view line, uint64_t address, ResolverInterface *resolver,
    std::vector<uint8_t> &byte_values,
    std::vector<RelocationInfo> &relocations) {
  std::string match;
  std::string remainder;
  ELFIO::section *section = current_section_;
  uint64_t size = 0;
  std::string directive;
  std::string label;
  if (!RE2::FullMatch(line, asm_line_re_, &label, &directive)) {
    return absl::InvalidArgumentError(
        absl::StrCat("Invalid assembly line: '", line, "'"));
  }
  if (!RE2::FullMatch(directive, directive_re_, &match, &remainder)) {
    return absl::InvalidArgumentError(
        absl::StrCat("Invalid directive: '", directive, "'"));
  }
  if (match == "align") {
    // .align <n>
    if (section == nullptr) {
      return absl::InvalidArgumentError(
          absl::StrCat("No section for directive: '", directive, "'"));
    }
    auto res = SimpleTextToInt<uint64_t>(remainder);
    if (!res.ok()) return res.status();
    uint64_t align = res.value();
    // Verify that the alignment is a power of two.
    if ((align & (align - 1)) != 0) {
      return absl::InvalidArgumentError(
          absl::StrCat("Invalid alignment: '", directive, "'"));
    }
    uint64_t address = section_address_map_[section];
    size = ((address + align - 1) & ~(align - 1)) - address;
  } else if (match == "bss") {
    // .bss
    SetBssSection(".bss");
  } else if (match == "bytes") {
    // .bytes
    auto res = GetValues<uint8_t>(remainder, resolver);
    if (!res.ok()) return res.status();
    auto values = res.value();
    size = values.size();
    for (auto const &value : values) byte_values.push_back(value);
  } else if (match == "char") {
    // .char
    auto res = GetValues<char>(remainder, resolver);
    if (!res.ok()) return res.status();
    auto values = res.value();
    size = values.size();
    for (auto const &value : values) byte_values.push_back(value);
  } else if (match == "cstring") {
    // .cstring
    auto res = GetValues<std::string>(remainder, resolver);
    if (!res.ok()) return res.status();
    auto values = res.value();
    size = 0;
    for (auto const &value : values) {
      for (auto const &c : value) byte_values.push_back(c);
      byte_values.push_back('\0');
      size += value.size() + 1;
    }
  } else if (match == "data") {
    // .data
    SetDataSection(".data");
  } else if (match == "global") {
    // .global <name>
    auto res = GetLabels(remainder);
    if (!res.ok()) return res.status();
    auto values = res.value();
    for (auto const &value : values) {
      global_symbols_.insert(value);
    }
  } else if (match == "long") {
    // .long
    auto res = GetValues<int64_t>(remainder);
    if (!res.ok()) return res.status();
    auto values = res.value();
    size = values.size() * sizeof(int64_t);
    ConvertToBytes<int64_t>(values, byte_values);
  } else if (match == "sect") {
    // .section <name>,<type>
    // TODO(torerik): Implement.
    return absl::UnimplementedError("Section directive not implemented");
  } else if (match == "short") {
    // .short
    auto res = GetValues<int16_t>(remainder);
    if (!res.ok()) return res.status();
    auto values = res.value();
    size = values.size() * sizeof(int16_t);
    ConvertToBytes<int16_t>(values, byte_values);
  } else if (match == "space") {
    // .space <n>
    auto res = SimpleTextToInt<uint64_t>(remainder);
    if (!res.ok()) return res.status();
    size = res.value();
  } else if (match == "string") {
    // .string
    auto res = GetValues<std::string>(remainder);
    if (!res.ok()) return res.status();
    auto values = res.value();
    size = 0;
    for (auto const &value : values) {
      for (auto const &c : value) byte_values.push_back(c);
      size += value.size();
    }
  } else if (match == "text") {
    // .text
    SetTextSection(".text");
  } else if (match == "uchar") {
    // .uchar
    auto res = GetValues<uint8_t>(remainder);
    if (!res.ok()) return res.status();
    auto values = res.value();
    size = values.size();
    for (auto const &value : values) byte_values.push_back(value);
  } else if (match == "ulong") {
    // .ulong
    auto res = GetValues<uint64_t>(remainder);
    if (!res.ok()) return res.status();
    auto values = res.value();
    size = values.size() * sizeof(uint64_t);
    ConvertToBytes<uint64_t>(values, byte_values);
  } else if (match == "ushort") {
    // .ushort
    auto res = GetValues<uint16_t>(remainder);
    if (!res.ok()) return res.status();
    auto values = res.value();
    size = values.size() * sizeof(uint16_t);
    ConvertToBytes<uint16_t>(values, byte_values);
  } else if (match == "uword") {
    // .uword
    auto res = GetValues<uint32_t>(remainder);
    if (!res.ok()) return res.status();
    auto values = res.value();
    size = values.size() * sizeof(uint32_t);
    ConvertToBytes<uint32_t>(values, byte_values);
  } else if (match == "word") {
    // .word
    auto res = GetValues<int32_t>(remainder);
    if (!res.ok()) return res.status();
    auto values = res.value();
    size = values.size() * sizeof(int32_t);
    ConvertToBytes<int32_t>(values, byte_values);
  } else {
    return absl::InvalidArgumentError(
        absl::StrCat("Unsupported directive: '", directive, "'"));
  }
  if ((size > 0) && (section != nullptr)) {
    if (!section_address_map_.contains(section)) {
      return absl::InternalError(
          absl::StrCat("No address for section '", section->get_name(), "'"));
    }
    section_address_map_[section] += size;
  }

  if (!label.empty()) {
    // When initially adding symbols, the address is relative to the start
    // of the containing section. This will be corrected later.
    if (section == nullptr) {
      return absl::InvalidArgumentError(
          absl::StrCat("Label: '", label, "' defined outside of a section"));
    }
    auto status =
        AddSymbol(label, address, size, STT_NOTYPE, STB_LOCAL, 0, section);
  }
  return absl::OkStatus();
}

// Parse and process an assembly statement. The assembly statement is
// expected to be a single line of text. The byte values are appended to the
// given vector.
absl::Status SimpleAssembler::ParseAsmStatement(
    absl::string_view line, uint64_t address, ResolverInterface *resolver,
    std::vector<uint8_t> &byte_values,
    std::vector<RelocationInfo> &relocations) {
  // Call the target specific assembler to encode the statement.
  auto result = opcode_assembler_if_->Encode(
      address, line,
      absl::bind_front(&SimpleAssembler::AddSymbolToCurrentSection, this),
      resolver, byte_values, relocations);
  if (!result.ok()) return result.status();
  section_address_map_[current_section_] += result.value();
  return absl::OkStatus();
}

void SimpleAssembler::SetTextSection(const std::string &name) {
  // First check if the section already exists.
  auto *section = writer_.sections[name];
  if (section != nullptr) {
    current_section_ = section;
    return;
  }
  section = writer_.sections.add(name);
  auto status = AddSymbol(name, 0, 0, STT_SECTION, STB_LOCAL, 0, section);
  if (!status.ok()) {
    LOG(ERROR) << "Failed to add symbol for data section: " << status.message();
  }
  section->set_type(SHT_PROGBITS);
  section->set_flags(SHF_ALLOC | SHF_EXECINSTR);
  section->set_addr_align(0x10);
  // Should probably add the section symbol to the symbol table.
  current_section_ = section;
  text_section_ = section;
  section_index_map_.insert({section->get_index(), text_section_});
}

void SimpleAssembler::SetDataSection(const std::string &name) {
  // First check if the section already exists.
  auto *section = writer_.sections[name];
  if (section != nullptr) {
    current_section_ = section;
    return;
  }
  section = writer_.sections.add(name);
  auto status = AddSymbol(name, 0, 0, STT_SECTION, STB_LOCAL, 0, section);
  if (!status.ok()) {
    LOG(ERROR) << "Failed to add symbol for data section: " << status.message();
  }
  section->set_type(SHT_PROGBITS);
  section->set_flags(SHF_ALLOC | SHF_WRITE);
  section->set_addr_align(0x10);
  // Should probably add the section symbol to the symbol table.
  current_section_ = section;
  data_section_ = section;
  section_index_map_.insert({section->get_index(), data_section_});
}

void SimpleAssembler::SetBssSection(const std::string &name) {
  // First check if the section already exists.
  auto *section = writer_.sections[name];
  if (section != nullptr) {
    current_section_ = section;
    return;
  }
  section = writer_.sections.add(name);
  auto status = AddSymbol(name, 0, 0, STT_SECTION, STB_LOCAL, 0, section);
  if (!status.ok()) {
    LOG(ERROR) << "Failed to add symbol for bss section: " << status.message();
  }
  section->set_type(SHT_NOBITS);
  section->set_flags(SHF_ALLOC | SHF_WRITE);
  section->set_addr_align(0x10);
  // Should probably add the section symbol to the symbol table.
  current_section_ = section;
  bss_section_ = section;
  section_index_map_.insert({section->get_index(), bss_section_});
}
absl::Status SimpleAssembler::AddSymbolToCurrentSection(
    const std::string &name, ELFIO::Elf64_Addr value, ELFIO::Elf_Xword size,
    uint8_t type, uint8_t binding, uint8_t other) {
  return AddSymbol(name, value, size, type, binding, other, current_section_);
}

absl::Status SimpleAssembler::AddSymbol(const std::string &name,
                                        ELFIO::Elf64_Addr value,
                                        ELFIO::Elf_Xword size, uint8_t type,
                                        uint8_t binding, uint8_t other,
                                        const std::string &section_name) {
  ELFIO::section *section = nullptr;
  if (!section_name.empty()) {
    section = writer_.sections[section_name];
    if (section == nullptr) {
      return absl::InvalidArgumentError(
          absl::StrCat("Section '", section_name, "' not found"));
    }
  }
  return AddSymbol(name, value, size, type, binding, other, section);
}

absl::Status SimpleAssembler::AddSymbol(const std::string &name,
                                        ELFIO::Elf64_Addr value,
                                        ELFIO::Elf_Xword size, uint8_t type,
                                        uint8_t binding, uint8_t other,
                                        ELFIO::section *section) {
  auto iter = symbol_indices_.find(name);
  if (iter != symbol_indices_.end()) {
    return absl::AlreadyExistsError(
        absl::StrCat("Symbol '", name, "' already exists"));
  }
  auto index = symbol_accessor_->add_symbol(
      *string_accessor_, name.c_str(), value, size, binding, type, other,
      section == nullptr ? SHN_UNDEF : section->get_index());
  symbol_indices_.insert({name, index});
  // If this is not an undefined symbol reference, then see if the symbol name
  // is part of the "current" undefined symbols, and if so, remove it.
  if (section != nullptr) {
    auto iter = undefined_symbols_.find(name);
    if (iter != undefined_symbols_.end()) {
      undefined_symbols_.erase(iter);
    }
  }
  return absl::OkStatus();
}

void SimpleAssembler::SimpleAddSymbol(absl::string_view name) {
  // If the symbol exists, then just return.
  if (symbol_indices_.contains(name)) return;
  if (undefined_symbols_.contains(name)) return;
  std::string name_str(name);
  undefined_symbols_.insert(name_str);
}

absl::Status SimpleAssembler::AppendData(const char *data, size_t size) {
  if (current_section_ == nullptr) {
    return absl::FailedPreconditionError("No current section");
  }
  current_section_->append_data(data, size);
  return absl::OkStatus();
}

}  // namespace assembler
}  // namespace util
}  // namespace sim
}  // namespace mpact
