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
#include <istream>
#include <ostream>
#include <string>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "elfio/elf_types.hpp"
#include "elfio/elfio_section.hpp"
#include "elfio/elfio_segment.hpp"
#include "elfio/elfio_strings.hpp"
#include "elfio/elfio_symbols.hpp"
#include "mpact/sim/util/asm/opcode_assembler_interface.h"
#include "mpact/sim/util/asm/resolver_interface.h"
#include "re2/re2.h"

namespace mpact {
namespace sim {
namespace util {
namespace assembler {

// A symbol resolver that always returns 0 for any symbol name. This is used
// for the first pass of parsing the assembly code, when we are just creating
// the symbols and computing the sizes of the sections.
class ZeroResolver : public ResolverInterface {
 public:
  absl::StatusOr<uint64_t> Resolve(absl::string_view text) override {
    return 0;
  }
};

// A symbol resolver that uses the symbol table and the symbol indices to
// resolve symbol names to values.
class SymbolResolver : public ResolverInterface {
 public:
  explicit SymbolResolver(
      ELFIO::section *symtab,
      const absl::flat_hash_map<std::string, ELFIO::Elf_Xword> &symbol_indices)
      : symtab_(symtab), symbol_indices_(symbol_indices) {}
  absl::StatusOr<uint64_t> Resolve(absl::string_view text) override {
    auto iter = symbol_indices_.find(text);
    if (iter == symbol_indices_.end()) {
      return absl::InvalidArgumentError(
          absl::StrCat("Symbol '", text, "' not found"));
    }
    auto index = iter->second;
    auto *sym = reinterpret_cast<const ELFIO::Elf64_Sym *>(symtab_->get_data());
    return sym[index].st_value;
  }

 private:
  // The symbol table ELF section.
  ELFIO::section *symtab_;
  // Map from symbol name to symbol index in the symbol table.
  const absl::flat_hash_map<std::string, ELFIO::Elf_Xword> &symbol_indices_;
};

SimpleAssembler::SimpleAssembler(int os_abi, int type, int machine,
                                 uint64_t base_address,
                                 OpcodeAssemblerInterface *opcode_assembler_if)
    : opcode_assembler_if_(opcode_assembler_if),
      base_address_(base_address),
      comment_re_("^\\s*(?:;(.*))?$"),
      asm_line_re_("^(?:(?:(\\S+)\\s*:)?|\\s)\\s*([^;]*?)?\\s*(?:;(.*))?$"),
      directive_re_(
          "^\\.(align|bss|bytes|char|cstring|data|entry|global|long|sect"
          "|short|space|string|type|text|uchar|ulong|ushort|uword|word)(?:\\s+("
          ".*)"
          ")?\\s*"
          "$") {
  // Configure the ELF file writer.
  writer_.create(ELFCLASS64, ELFDATA2LSB);
  writer_.set_os_abi(os_abi);
  writer_.set_type(ET_EXEC);
  writer_.set_machine(machine);
  // Create the symbol table section.
  symtab_ = writer_.sections.add(".symtab");
  symtab_->set_type(SHT_SYMTAB);
  symtab_->set_entry_size(sizeof(ELFIO::Elf64_Sym));
  // Create the string table section.
  strtab_ = writer_.sections.add(".strtab");
  strtab_->set_type(SHT_STRTAB);
  // Link the symbol table to the string table.
  symtab_->set_link(strtab_->get_index());
  // Create the symbol and string table accessors.
  symbol_accessor_ = new ELFIO::symbol_section_accessor(writer_, symtab_);
  string_accessor_ =
      new ELFIO::string_section_accessor(writer_.sections[".strtab"]);
}

SimpleAssembler::~SimpleAssembler() {
  delete symbol_resolver_;
  delete symbol_accessor_;
  delete string_accessor_;
}

absl::Status SimpleAssembler::Parse(std::istream &is) {
  // A trivial symbol resolver that always returns 0.
  ZeroResolver zero_resolver;
  // Create the sections we will need: .text, .data, and .bss.
  ELFIO::section *text_section = writer_.sections.add(".text");
  text_section->set_type(SHT_PROGBITS);
  text_section->set_flags(SHF_ALLOC | SHF_EXECINSTR);
  text_section->set_addr_align(0x10);
  ELFIO::section *data_section = writer_.sections.add(".data");
  data_section->set_type(SHT_PROGBITS);
  data_section->set_flags(SHF_ALLOC | SHF_WRITE);
  data_section->set_addr_align(0x10);
  ELFIO::section *bss_section = writer_.sections.add(".bss");
  bss_section->set_type(SHT_NOBITS);
  bss_section->set_flags(SHF_ALLOC | SHF_WRITE);
  bss_section->set_addr_align(0x10);

  // First pass of parsing the input stream. This will add symbols to the symbol
  // table and compute the sizes of all instructions and the sections. The
  // section_address_map_ will keep track of the current location within each
  // section (i.e., the offset within the section of the next
  // instruction/object).
  std::string line;
  std::string label;
  std::string statement;
  while (is.good() && !is.eof()) {
    getline(is, line);
    if (RE2::FullMatch(line, comment_re_)) continue;
    if (RE2::FullMatch(line, asm_line_re_, &label, &statement)) {
      std::vector<uint8_t> byte_vector;
      auto *section = current_section_;
      uint64_t address =
          (section == nullptr) ? 0 : section_address_map_[section];
      if (!statement.empty()) {
        absl::Status status;
        if (statement[0] == '.') {
          status = ParseAsmDirective(statement, &zero_resolver, byte_vector);
        } else {
          status = ParseAsmStatement(statement, &zero_resolver, byte_vector);
        }
        if (!status.ok()) return status;
        // Save the statements for processing in pass two.
        lines_.push_back(statement);
      }

      if (!label.empty()) {
        // When initially adding symbols, the address is relative to the start
        // of the containing section. This will be corrected later.
        if (section == nullptr) {
          return absl::InvalidArgumentError(absl::StrCat(
              "Label: '", label, "' defined outside of a section"));
        }
        auto size = section_address_map_[section] - address;
        auto status =
            AddSymbol(label, address, size, STT_NOTYPE, STB_LOCAL, 0, section);
      }
      continue;
    }
    return absl::AbortedError(absl::StrCat("Parse failure: '", line, "'"));
  }
  if (!is.eof()) return absl::InternalError("Input stream entered bad state");

  // Section sizes are now known. So let's compute the layout and update all
  // the symbol values/addresses before the next pass.
  // The layout is:
  //   text segment starting at base address + any alignment.
  //   data segment starting at the end of the text segment + any alignment.
  // The bss section is added to the end of the data segment + any alignment.

  auto text_segment_start = base_address_ & ~4095ULL;
  ELFIO::segment *text_segment = writer_.segments.add();
  text_segment->set_type(PT_LOAD);
  text_segment->set_virtual_address(text_segment_start);
  text_segment->set_physical_address(text_segment_start);
  text_segment->set_flags(PF_X | PF_R);
  text_segment->set_align(4096);

  uint64_t data_segment_start = (text_segment->get_virtual_address() +
                                 section_address_map_[text_section] + 4095) &
                                ~4095ULL;

  ELFIO::segment *data_segment = writer_.segments.add();
  data_segment->set_type(PT_LOAD);
  data_segment->set_virtual_address(data_segment_start);
  data_segment->set_physical_address(data_segment_start);
  data_segment->set_flags(PF_W | PF_R);
  data_segment->set_align(4096);

  uint64_t bss_size = section_address_map_[bss_section];
  uint64_t bss_align = bss_section->get_addr_align() - 1;
  uint64_t bss_segment_start =
      (data_segment_start + section_address_map_[data_section] + bss_align) &
      ~bss_align;

  // Now we can update the symbol table based on the new section sizes.

  // Copy the symbol table from the section data.
  auto num_symbols = symbol_accessor_->get_symbols_num();
  auto size = symtab_->get_size();
  auto *symbols = new ELFIO::Elf64_Sym[num_symbols];
  std::memcpy(symbols, symtab_->get_data(), size);
  // Convert the section offsets to the absolute addresses.
  for (int i = 0; i < num_symbols; ++i) {
    auto &sym = symbols[i];
    auto shndx = sym.st_shndx;
    auto sym_name = string_accessor_->get_string(sym.st_name);
    if (global_symbols_.contains(sym_name)) {
      sym.st_info = ELF_ST_INFO(STB_GLOBAL, ELF_ST_TYPE(sym.st_info));
    }
    if (shndx == text_section->get_index()) {
      sym.st_value += text_segment_start;
    } else if (shndx == data_section->get_index()) {
      sym.st_value += data_segment_start;
    } else if (shndx == bss_section->get_index()) {
      sym.st_value += bss_segment_start;
    }
  }
  // Update the symbol table section data with the updated symbols.
  symtab_->set_data(reinterpret_cast<char *>(symbols), size);
  delete[] symbols;

  // For the second pass, we need a symbol resolver that uses the symbol table
  // and the symbol indices.
  symbol_resolver_ = new SymbolResolver(symtab_, symbol_indices_);

  // Update the section address map so that each section starts at the right
  // address, i.e., it no longer tracks the offset within each section, but the
  // absolute address.
  section_address_map_[text_section] = text_segment_start;
  section_address_map_[data_section] = data_segment_start;
  section_address_map_[bss_section] = bss_segment_start;

  // Now fill in the sections. Parse each of the lines saved in the first pass.
  for (auto const &line : lines_) {
    std::vector<uint8_t> byte_vector;
    absl::Status status;
    auto *section = current_section_;
    if (line[0] == '.') {
      auto status = ParseAsmDirective(line, symbol_resolver_, byte_vector);
    } else {
      auto status = ParseAsmStatement(line, symbol_resolver_, byte_vector);
    }
    if (!status.ok()) return status;
    if (byte_vector.empty()) continue;
    // Add data to the section, but first make sure it's not bss.
    if (section != bss_section) {
      section->append_data(reinterpret_cast<const char *>(byte_vector.data()),
                           byte_vector.size());
    }
  }

  bss_section->set_size(bss_size);

  // Add sections to the segments. First segment gets the text section. The
  // second segment gets the data and bss sections.
  text_segment->add_section_index(text_section->get_index(),
                                  text_section->get_addr_align());
  data_segment->add_section_index(data_section->get_index(),
                                  data_section->get_addr_align());
  data_segment->add_section_index(bss_section->get_index(),
                                  bss_section->get_addr_align());

  return absl::OkStatus();
}

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

absl::Status SimpleAssembler::SetEntryPoint(const std::string &value) {
  auto res = SimpleTextToInt<uint64_t>(value, symbol_resolver_);
  if (!res.ok()) return res.status();
  entry_point_ = res.value();
  return absl::OkStatus();
}

absl::Status SimpleAssembler::SetEntryPoint(uint64_t value) {
  entry_point_ = value;
  return absl::OkStatus();
}

// Top level function that writes the ELF file out to disk.
absl::Status SimpleAssembler::Write(std::ostream &os) {
  if (entry_point_.empty()) return absl::NotFoundError("Entry point not set");
  auto res = SimpleTextToInt<uint64_t>(entry_point_, symbol_resolver_);
  if (!res.ok()) return res.status();
  symbol_accessor_->arrange_local_symbols();
  writer_.set_entry(res.value());
  writer_.save(os);
  return absl::OkStatus();
}

// Parse and process an assembly directive. The assembly directive is expected
// to be in the form of a line starting with a period followed by a directive
// name and an optional argument. The argument is a string of tokens separated
// by spaces. The argument is parsed using regular expressions. The byte values
// are appended to the given vector.
absl::Status SimpleAssembler::ParseAsmDirective(
    absl::string_view directive, ResolverInterface *resolver,
    std::vector<uint8_t> &byte_values) {
  std::string match;
  std::string remainder;
  ELFIO::section *section = current_section_;
  uint64_t size = 0;
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
  } else if (match == "entry") {
    // .entry <name>|<address>
    entry_point_ = remainder;
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
  } else if (match == "section") {
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
  return absl::OkStatus();
}

// Parse and process an assembly statement. The assembly statement is expected
// to be a single line of text. The byte values are appended to the given
// vector.
absl::Status SimpleAssembler::ParseAsmStatement(
    absl::string_view statement, ResolverInterface *resolver,
    std::vector<uint8_t> &byte_values) {
  // Call the target specific assembler to encode the statement.
  auto status = opcode_assembler_if_->Encode(
      section_address_map_[current_section_], statement, resolver, byte_values);
  if (!status.ok()) return status;
  section_address_map_[current_section_] += byte_values.size();
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
  section->set_type(SHT_PROGBITS);
  section->set_flags(SHF_ALLOC | SHF_EXECINSTR);
  section->set_addr_align(0x10);
  // Should probably add the section symbol to the symbol table.
  current_section_ = section;
}

void SimpleAssembler::SetDataSection(const std::string &name) {
  // First check if the section already exists.
  auto *section = writer_.sections[name];
  if (section != nullptr) {
    current_section_ = section;
    return;
  }
  section = writer_.sections.add(name);
  section->set_type(SHT_PROGBITS);
  section->set_flags(SHF_ALLOC | SHF_WRITE);
  section->set_addr_align(0x10);
  // Should probably add the section symbol to the symbol table.
  current_section_ = section;
}

void SimpleAssembler::SetBssSection(const std::string &name) {
  // First check if the section already exists.
  auto *section = writer_.sections[name];
  if (section != nullptr) {
    current_section_ = section;
    return;
  }
  section = writer_.sections.add(name);
  section->set_type(SHT_NOBITS);
  section->set_flags(SHF_ALLOC);
  section->set_addr_align(0x10);
}

absl::Status SimpleAssembler::AddSymbol(const std::string &name,
                                        ELFIO::Elf64_Addr value,
                                        ELFIO::Elf_Xword size, uint8_t type,
                                        uint8_t binding, uint8_t other,
                                        ELFIO::section *section) {
  if (symbol_indices_.contains(name)) {
    return absl::AlreadyExistsError(
        absl::StrCat("Symbol '", name, "' already exists"));
  }
  auto res =
      symbol_accessor_->add_symbol(*string_accessor_, name.c_str(), value, size,
                                   binding, type, other, section->get_index());
  symbol_indices_.insert({name, res});
  return absl::OkStatus();
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
