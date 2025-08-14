// Copyright 2025 Google LLC
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

#ifndef MPACT_SIM_UTIL_ASM_RESOLVER_H_
#define MPACT_SIM_UTIL_ASM_RESOLVER_H_

#include <cstdint>
#include <string>

#include "absl/container/flat_hash_map.h"
#include "absl/functional/any_invocable.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "elfio/elf_types.hpp"
#include "elfio/elfio.hpp"  // IWYU pragma: keep
#include "elfio/elfio_section.hpp"
#include "mpact/sim/util/asm/resolver_interface.h"

namespace mpact::sim::util::assembler {

// A symbol resolver that always returns 0 for any symbol name. This is used
// for the first pass of parsing the assembly code, when we are just creating
// the symbols and computing the sizes of the sections.
class ZeroResolver : public ResolverInterface {
 public:
  // Constructor takes a callback function that will be called for each symbol
  // name encountered so that it can be added to the symbol table.
  template <typename T>
  ZeroResolver(T add_symbol_fcn) : add_symbol_fcn_(add_symbol_fcn) {}
  absl::StatusOr<uint64_t> Resolve(absl::string_view text) override;

 private:
  absl::AnyInvocable<void(absl::string_view)> add_symbol_fcn_;
};

// A symbol resolver that uses the symbol table and the symbol indices to
// resolve symbol names to values.
class SymbolResolver : public ResolverInterface {
 public:
  SymbolResolver(
      int elf_file_class, ELFIO::section* symtab,
      const absl::flat_hash_map<std::string, ELFIO::Elf_Word>& symbol_indices);
  absl::StatusOr<uint64_t> Resolve(absl::string_view text) override;

 private:
  // Elf file class.
  int elf_file_class_ = 0;
  // The symbol table ELF section.
  ELFIO::section* symtab_;
  // Map from symbol name to symbol index in the symbol table.
  const absl::flat_hash_map<std::string, ELFIO::Elf_Word>& symbol_indices_;
};

}  // namespace mpact::sim::util::assembler

#endif  // MPACT_SIM_UTIL_ASM_RESOLVER_H_
