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

#ifndef MPACT_SIM_UTIL_MEMORY_TAGGED_TO_UNTAGGED_MEMORY_TRANSACTOR_H_
#define MPACT_SIM_UTIL_MEMORY_TAGGED_TO_UNTAGGED_MEMORY_TRANSACTOR_H_

#include <cstdint>

#include "mpact/sim/generic/data_buffer.h"
#include "mpact/sim/generic/instruction.h"
#include "mpact/sim/generic/ref_count.h"
#include "mpact/sim/util/memory/memory_interface.h"
#include "mpact/sim/util/memory/tagged_memory_interface.h"

namespace mpact {
namespace sim {
namespace util {

using ::mpact::sim::generic::DataBuffer;
using ::mpact::sim::generic::Instruction;
using ::mpact::sim::generic::ReferenceCount;
using ::mpact::sim::util::MemoryInterface;
using ::mpact::sim::util::TaggedMemoryInterface;

// This class provides a transactor to convert from tagged loads/stores
// to untagged loads/stores. This is done by passing all loads/stores to the
// plain memory interface, ignoring tags on stores, and returning zero tags
// on loads.
class TaggedToUntaggedMemoryTransactor : public util::TaggedMemoryInterface {
 public:
  explicit TaggedToUntaggedMemoryTransactor(MemoryInterface* target_mem)
      : target_mem_(target_mem) {}

  void Load(uint64_t address, DataBuffer* db, Instruction* inst,
            ReferenceCount* context) override;
  void Load(DataBuffer* address_db, DataBuffer* mask_db, int el_size,
            DataBuffer* db, Instruction* inst,
            ReferenceCount* context) override;
  void Store(uint64_t address, DataBuffer* db) override;
  void Store(DataBuffer* address, DataBuffer* mask, int el_size,
             DataBuffer* db) override;
  void Load(uint64_t address, DataBuffer* db, DataBuffer* tags,
            Instruction* inst, ReferenceCount* context) override;
  void Store(uint64_t address, DataBuffer* db, DataBuffer* tags) override;

 private:
  MemoryInterface* target_mem_;
};

}  // namespace util
}  // namespace sim
}  // namespace mpact

#endif  // MPACT_SIM_UTIL_MEMORY_TAGGED_TO_UNTAGGED_MEMORY_TRANSACTOR_H_
