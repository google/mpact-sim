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

#include "mpact/sim/util/program_loader/elf_program_loader.h"

#include <sys/stat.h>

#include <cstdint>
#include <cstring>
#include <string>
#include <utility>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "elfio/elf_types.hpp"
#include "elfio/elfio_segment.hpp"
#include "elfio/elfio_symbols.hpp"
#include "mpact/sim/generic/core_debug_interface.h"
#include "mpact/sim/generic/data_buffer.h"
#include "mpact/sim/util/memory/memory_interface.h"

namespace mpact {
namespace sim {
namespace util {

constexpr uint64_t kPtGnuStack = 0x6474e551;

ElfProgramLoader::ElfProgramLoader(util::MemoryInterface *code_memory,
                                   util::MemoryInterface *data_memory)
    : code_memory_(code_memory), data_memory_(data_memory) {}

ElfProgramLoader::ElfProgramLoader(util::MemoryInterface *memory)
    : code_memory_(memory), data_memory_(memory) {}

ElfProgramLoader::ElfProgramLoader(generic::CoreDebugInterface *dbg_if)
    : dbg_if_(dbg_if) {}

ElfProgramLoader::~ElfProgramLoader() {
  for (auto *symtab : symbol_accessors_) {
    delete symtab;
  }
  symbol_accessors_.clear();
}

// Only load the program into the elf reader so that symbols can be looked up.
absl::StatusOr<uint64_t> ElfProgramLoader::LoadSymbols(
    const std::string &file_name) {
  struct stat buffer;
  auto result = stat(file_name.c_str(), &buffer);
  if (result == -1) {
    return absl::NotFoundError(
        absl::StrCat("Unable to open elf file: '", file_name, "'"));
  }
  if (!elf_reader_.load(file_name)) {
    return absl::InternalError(
        absl::StrCat("Elf loading error for '", file_name, "'"));
  }
  std::string msg = elf_reader_.validate();
  if (!msg.empty()) {
    return absl::InternalError(
        absl::StrCat("Validation error for '", file_name, "': ", msg));
  }
  loaded_ = true;
  // Now look up any symbol sections.
  for (auto const &section : elf_reader_.sections) {
    if (section->get_type() == SHT_SYMTAB) {
      symbol_accessors_.push_back(
          new ELFIO::symbol_section_accessor(elf_reader_, section));
    }
  }
  std::string name;
  ELFIO::Elf_Xword size;
  unsigned char bind;
  unsigned char type;
  ELFIO::Elf_Half section_index;
  unsigned char other;
  // Scan symbol table. Place function names in a map for easy lookup.
  for (auto *symtab : symbol_accessors_) {
    ELFIO::Elf64_Addr value;
    for (unsigned i = 0; i < symtab->get_symbols_num(); i++) {
      symtab->get_symbol(i, name, value, size, bind, type, section_index,
                         other);
      if (type == STT_FUNC) {
        fcn_symbol_map_.emplace(value, name);
        function_range_map_.insert(
            std::make_pair(AddressRange(value, size), name));
      }
    }
  }
  return elf_reader_.get_entry();
}

// This is the main method of the class. It reads in the elf file, validates it
// and iterates over the segments. For each segment it writes it to the
// appropriate location in the given memories.
absl::StatusOr<uint64_t> ElfProgramLoader::LoadProgram(
    const std::string &file_name) {
  auto load_symbols_res = LoadSymbols(file_name);
  if (!load_symbols_res.ok()) return load_symbols_res.status();

  generic::DataBufferFactory db_factory;

  for (auto const &segment : elf_reader_.segments) {
    if (segment->get_type() == kPtGnuStack) {
      stack_size_ = segment->get_memory_size();
      has_stack_size_ = (stack_size_ > 0);
      continue;
    }
    // If the section isn 't loadable, continue.
    if (segment->get_type() != PT_LOAD) continue;
    if (segment->get_file_size() == 0) continue;
    // Read the data from the elf file.
    if (dbg_if_ == nullptr) {  // Use memory interfaces.
      auto *db = db_factory.Allocate(segment->get_file_size());
      std::memcpy(db->raw_ptr(), segment->get_data(), segment->get_file_size());

      if (segment->get_flags() &
          PF_X) {  // Executable, so write to code memory.
        code_memory_->Store(segment->get_virtual_address(), db);
      } else {  // Write to data memory.
        data_memory_->Store(segment->get_virtual_address(), db);
      }
      db->DecRef();
      continue;
    }
    // Use debug interface.
    auto res =
        dbg_if_->WriteMemory(segment->get_virtual_address(),
                             segment->get_data(), segment->get_file_size());
    if (!res.ok() || (res.value() != segment->get_file_size())) {
      return absl::InternalError("Write error while loading elf segment");
    }
  }

  return load_symbols_res.value();
}

absl::StatusOr<std::pair<uint64_t, uint64_t>> ElfProgramLoader::GetSymbol(
    const std::string &name) const {
  if (!loaded_) return absl::InternalError("No program loaded");
  if (symbol_accessors_.empty())
    return absl::NotFoundError("Symbol table not found");

  ELFIO::Elf64_Addr value;
  ELFIO::Elf_Xword size;
  unsigned char bind;
  unsigned char type;
  ELFIO::Elf_Half section_index;
  unsigned char other;
  for (auto *symtab : symbol_accessors_) {
    if (symtab->get_symbol(name, value, size, bind, type, section_index,
                           other)) {
      return std::make_pair(static_cast<uint64_t>(value),
                            static_cast<uint64_t>(size));
    }
  }

  return absl::NotFoundError(absl::StrCat("Symbol '", name, "' not found."));
}

absl::StatusOr<std::string> ElfProgramLoader::GetFcnSymbolName(
    uint64_t address) const {
  if (!loaded_) return absl::InternalError("No program loaded");
  if (fcn_symbol_map_.empty())
    return absl::NotFoundError("Symbol information not found");
  auto iter = fcn_symbol_map_.find(address);
  if (iter != fcn_symbol_map_.end()) return iter->second;

  return absl::NotFoundError("Function symbol not found");
}

absl::StatusOr<uint64_t> ElfProgramLoader::GetStackSize() const {
  if (!has_stack_size_) return absl::NotFoundError("Stack size not found");
  return stack_size_;
}

absl::StatusOr<std::string> ElfProgramLoader::GetFunctionName(
    uint64_t address) const {
  if (!loaded_) return absl::InternalError("No program loaded");
  if (fcn_symbol_map_.empty())
    return absl::NotFoundError("Symbol information not found");
  auto iter = function_range_map_.find(AddressRange(address));
  if (iter != function_range_map_.end()) return iter->second;
  return absl::NotFoundError("Function not found");
}

}  // namespace util
}  // namespace sim
}  // namespace mpact
