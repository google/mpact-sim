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

#ifndef MPACT_SIM_UTIL_PROGRAM_LOADER_ELF_PROGRAM_LOADER_H_
#define MPACT_SIM_UTIL_PROGRAM_LOADER_ELF_PROGRAM_LOADER_H_

#include <map>
#include <string>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/status/statusor.h"
#include "elfio/elfio.hpp"
#include "elfio/elfio_symbols.hpp"
#include "mpact/sim/generic/core_debug_interface.h"
#include "mpact/sim/util/memory/memory_interface.h"
#include "mpact/sim/util/program_loader/program_loader_interface.h"

namespace mpact {
namespace sim {
namespace util {

struct AddressRange {
  explicit AddressRange(uint64_t start) : start(start), end(start + 1) {}
  AddressRange(uint64_t start, uint64_t size)
      : start(start), end(start + size) {}

  uint64_t start;
  uint64_t end;
};

struct AddressRangeComp {
  bool operator()(const AddressRange &lhs, const AddressRange &rhs) const {
    if (lhs.end <= rhs.start) {
      return true;
    }
    return false;
  }
};

// This class wraps the elfio class to provide an easy interface to load the
// segments of an elf executable file into memory. If both code and data
// memories are given, then executable segments are loaded into code memory and
// all other segments into data memory.
//
// TODO(torerik): Allow for a multiple memories to be passed in to allow
// segments to be loaded into different memories, not just one big contiguous
// memory.
class ElfProgramLoader : public ProgramLoaderInterface {
 public:
  ElfProgramLoader(util::MemoryInterface *code_memory,
                   util::MemoryInterface *data_memory);
  explicit ElfProgramLoader(util::MemoryInterface *memory);
  explicit ElfProgramLoader(generic::CoreDebugInterface *dbg_if);
  ElfProgramLoader() = delete;
  ~ElfProgramLoader() override;

  absl::StatusOr<uint64_t> LoadProgram(const std::string &file_name) override;
  // Return the value and size of the symbol 'name' if it exists in the symbol
  // table.
  absl::StatusOr<std::pair<uint64_t, uint64_t>> GetSymbol(
      const std::string &name) const;
  // If there is a function with symbol table value 'address' return its name.
  absl::StatusOr<std::string> GetFcnSymbolName(uint64_t address) const;
  // Looks up to see if the address is in the range of a function symbol, and
  // if so, returns the functions name.
  absl::StatusOr<std::string> GetFunctionName(uint64_t address) const;
  // If the GNU stack size program header exists, return the memory size.
  absl::StatusOr<uint64_t> GetStackSize() const;

 private:
  bool loaded_ = false;
  bool symtab_absent_ = false;
  ELFIO::elfio elf_reader_;
  util::MemoryInterface *code_memory_ = nullptr;
  util::MemoryInterface *data_memory_ = nullptr;
  generic::CoreDebugInterface *dbg_if_ = nullptr;
  std::vector<const ELFIO::symbol_section_accessor *> symbol_accessors_;
  absl::flat_hash_map<uint64_t, std::string> fcn_symbol_map_;
  std::map<AddressRange, std::string, AddressRangeComp> function_range_map_;
  uint64_t has_stack_size_ = false;
  uint64_t stack_size_ = 0;
};

}  // namespace util
}  // namespace sim
}  // namespace mpact

#endif  // MPACT_SIM_UTIL_PROGRAM_LOADER_ELF_PROGRAM_LOADER_H_
