#include "mpact/sim/util/asm/resolver.h"

#include <cstdint>
#include <string>

#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "elfio/elf_types.hpp"
#include "elfio/elfio_section.hpp"

namespace mpact::sim::util::assembler {

absl::StatusOr<uint64_t> ZeroResolver::Resolve(absl::string_view text) {
  // Any symbol name should be added to the symbol table as an undefined
  // symbol if it is not already there. When the symbol is defined, the
  // symbol table will be updated. In the case of generating an executable
  // ELF file, any unresolved symbols will result in an error. When generating
  // an object file, any unresolved symbols will remain in the symbol table
  // and must be handled by the linker.
  add_symbol_fcn_(text);
  // Return 0 for any symbol name.
  return 0;
}

SymbolResolver::SymbolResolver(
    int elf_file_class, ELFIO::section *symtab,
    const absl::flat_hash_map<std::string, ELFIO::Elf_Word> &symbol_indices)
    : elf_file_class_(elf_file_class),
      symtab_(symtab),
      symbol_indices_(symbol_indices) {}

absl::StatusOr<uint64_t> SymbolResolver::Resolve(absl::string_view text) {
  auto iter = symbol_indices_.find(text);
  if (iter == symbol_indices_.end()) {
    return absl::InvalidArgumentError(
        absl::StrCat("SymbolResolver: Symbol '", text, "' not found"));
  }
  auto index = iter->second;
  if (elf_file_class_ == ELFCLASS64) {
    auto *sym = reinterpret_cast<const ELFIO::Elf64_Sym *>(symtab_->get_data());
    return sym[index].st_value;
  } else if (elf_file_class_ == ELFCLASS32) {
    auto *sym = reinterpret_cast<const ELFIO::Elf32_Sym *>(symtab_->get_data());
    return sym[index].st_value;
  }
  return absl::InternalError("Unsupported ELF file class");
}

}  // namespace mpact::sim::util::assembler
