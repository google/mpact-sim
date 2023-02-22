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

#ifndef MPACT_SIM_UTIL_MEMORY_ATOMIC_MEMORY_H_
#define MPACT_SIM_UTIL_MEMORY_ATOMIC_MEMORY_H_

#include "absl/container/flat_hash_set.h"
#include "mpact/sim/generic/data_buffer.h"
#include "mpact/sim/generic/instruction.h"
#include "mpact/sim/generic/ref_count.h"
#include "mpact/sim/util/memory/memory_interface.h"

namespace mpact {
namespace sim {
namespace util {

using ::mpact::sim::generic::DataBufferFactory;
using ::mpact::sim::generic::Instruction;
using ::mpact::sim::generic::ReferenceCount;

// This class builds upon the flat demand memory class to provide atomic
// memory operations on top of memory loads/stores.

class AtomicMemory : public MemoryInterface, public AtomicMemoryOpInterface {
 public:
  using Operation = AtomicMemoryOpInterface::Operation;

  AtomicMemory() = delete;
  explicit AtomicMemory(MemoryInterface *memory);
  ~AtomicMemory() override;

  // Load data from address into the DataBuffer, then schedule the Instruction
  // inst (if not nullptr) to be executed (using the function delay line) with
  // context. The size of the data access is based on size of the data buffer.
  void Load(uint64_t address, DataBuffer *db, Instruction *inst,
            ReferenceCount *context) override;
  // Load data from N addresses stored in address_db (uint64), using mask_db
  // (bool) to mask out the corresponding loads from taking place (if false).
  // Each access is el_size bytes long, and is stored into the DataBuffer.
  // Once done, the Instruction inst (if not nullptr) is scheduled to be
  // executed (using the function delay line) with context. It's the
  // responsibility of the caller to ensure that all DataBuffer instances passed
  // in are appropriately sized.
  void Load(DataBuffer *address_db, DataBuffer *mask_db, int el_size,
            DataBuffer *db, Instruction *inst,
            ReferenceCount *context) override;
  // Stores data from the DataBuffer instance to memory starting at address.
  void Store(uint64_t address, DataBuffer *db) override;
  // Stores data starting at each of the N addresses stored in address_db,
  // (uint64) using mask_db (bool) to mask out stores from taking place (if
  // false). Each store is el_size bytes long. It's the responsibility of the
  // caller to ensure that all DataBuffer instances that are passed in are
  // appropriately sized.
  void Store(DataBuffer *address_db, DataBuffer *mask_db, int el_size,
             DataBuffer *db) override;

  absl::Status PerformMemoryOp(uint64_t address, Operation op, DataBuffer *db,
                               Instruction *inst,
                               ReferenceCount *context) override;

 private:
  static constexpr int kTagShift = 3;
  // Write back the result.
  void WriteBack(Instruction *inst, ReferenceCount *context, DataBuffer *db);
  // Returns the db of the given size.
  DataBuffer *GetDb(int size) const;
  MemoryInterface *memory_ = nullptr;
  // Tag store for load linked operations. This is used to track if there is an
  // intervening store between the ll and the sc instruction. The addresses used
  // are the memory address shifted right by three. For byte addressable
  // memories, this means that the address is effectively a uint64_t address,
  // and that the ll/sc tracking granule is 8 bytes.
  absl::flat_hash_set<uint64_t> ll_tag_set_;
  // Support accesses of 1 through 8 byte integer types.
  DataBuffer *db1_;
  DataBuffer *db2_;
  DataBuffer *db4_;
  DataBuffer *db8_;
  DataBufferFactory db_factory_;
};

}  // namespace util
}  // namespace sim
}  // namespace mpact

#endif  // MPACT_SIM_UTIL_MEMORY_ATOMIC_MEMORY_H_
