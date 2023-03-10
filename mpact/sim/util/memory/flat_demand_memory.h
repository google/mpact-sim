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

#ifndef SIM_UTIL_MEMORY_FLAT_DEMAND_MEMORY_H_
#define SIM_UTIL_MEMORY_FLAT_DEMAND_MEMORY_H_

#include "absl/container/flat_hash_map.h"
#include "mpact/sim/generic/data_buffer.h"
#include "mpact/sim/generic/instruction.h"
#include "mpact/sim/generic/ref_count.h"
#include "mpact/sim/util/memory/memory_interface.h"

namespace mpact {
namespace sim {
namespace util {

using ::mpact::sim::generic::DataBuffer;
using ::mpact::sim::generic::Instruction;
using ::mpact::sim::generic::ReferenceCount;

// This class models a flat memory that is demand allocated in blocks 16K
// addressable units. This is useful for modeling memory spaces that are large
// and sparsely populated/utilitized. There is an assumption that the minimum
// addressable unit is a power of two and that any memory access smaller than
// the addressable_unit will treat the addressable unit as byte addressable and
// only access the low order bytes. All addresses are in terms of the
// addressable units.
class FlatDemandMemory : public MemoryInterface {
 public:
  FlatDemandMemory(uint64_t memory_size_in_units, uint64_t base_address,
                   unsigned addressable_unit_size, uint8_t fill);
  FlatDemandMemory(uint64_t memory_size_in_units, uint64_t base_address)
      : FlatDemandMemory(memory_size_in_units, base_address, 1, 0) {}
  explicit FlatDemandMemory(uint64_t base_address)
      : FlatDemandMemory(0xffff'ffff'ffff'ffff, base_address, 1, 0) {}
  FlatDemandMemory() : FlatDemandMemory(0xffff'ffff'ffff'ffff, 0, 1, 0) {}
  ~FlatDemandMemory() override;
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
  // Convenience template function that calls the above function with the
  // element size as the sizeof() the template parameter type.
  template <typename T>
  void Load(DataBuffer *address_db, DataBuffer *mask_db, DataBuffer *db,
            Instruction *inst, ReferenceCount *context) {
    Load(address_db, mask_db, sizeof(T), db, inst, context);
  }
  // Stores data from the DataBuffer instance to memory starting at address.
  void Store(uint64_t address, DataBuffer *db) override;
  // Stores data starting at each of the N addresses stored in address_db,
  // (uint64) using mask_db (bool) to mask out stores from taking place (if
  // false). Each store is el_size bytes long. It's the responsibility of the
  // caller to ensure that all DataBuffer instances that are passed in are
  // appropriately sized.
  void Store(DataBuffer *address_db, DataBuffer *mask_db, int el_size,
             DataBuffer *db) override;
  // Convenience template function that calls the above function with the
  // element size as the sizeof() the template parameter type.
  template <typename T>
  void Store(DataBuffer *address_db, DataBuffer *mask_db, DataBuffer *db) {
    Store(address_db, mask_db, sizeof(T), db);
  }
  static constexpr int kAllocationSize = 16 * 1024;  // Power of two.

 private:
  void LoadStoreHelper(uint64_t address, uint8_t *data_ptr, int size_in_units,
                       bool is_load);

  static constexpr int kAllocationShift = 14;
  static constexpr uint64_t kAllocationMask = kAllocationSize - 1;
  uint64_t base_address_;
  uint64_t max_address_;
  uint8_t fill_value_;
  int addressable_unit_shift_;
  int allocation_byte_size_;
  absl::flat_hash_map<uint64_t, uint8_t *> block_map_;
};

}  // namespace util
}  // namespace sim
}  // namespace mpact

#endif  // SIM_UTIL_MEMORY_FLAT_DEMAND_MEMORY_H_
