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

#ifndef MPACT_SIM_UTIL_MEMORY_FLAT_MEMORY_H_
#define MPACT_SIM_UTIL_MEMORY_FLAT_MEMORY_H_

#include <cstdint>

#include "mpact/sim/generic/instruction.h"
#include "mpact/sim/generic/ref_count.h"
#include "mpact/sim/util/memory/memory_interface.h"

namespace mpact {
namespace sim {
namespace util {

using ::mpact::sim::generic::DataBuffer;
using ::mpact::sim::generic::Instruction;
using ::mpact::sim::generic::ReferenceCount;

// This class models a flat, finite memory of a given size and location. The
// data is allocated all at once, not on demand. This class is best suited for
// modeling memories that are dense with respect to data that is read and
// written. There is an assumption that the minimum addressable unit is a power
// of two and that any memory access smaller than the addressable_unit will
// treat the addressable unit as byte addressable and only access the low order
// bytes. All addresses are in terms of the addressable units.
class FlatMemory : public MemoryInterface {
 public:
  // The constructor takes the size of the memory (in terms of addressable
  // units), the base address, the size of the minimum addressable unit, and a
  // byte value to fill the memory with. Only addresses in the range
  // [base_address : base_address + memory_size - 1]
  // will be served. It is a fatal error to access memory outside this range.
  FlatMemory(int64_t memory_size_in_units, uint64_t base_address,
             uint32_t addressable_unit_size, uint8_t fill);
  FlatMemory(int64_t memory_size_in_units, uint32_t addressable_unit_size,
             uint8_t fill)
      : FlatMemory(memory_size_in_units, 0, addressable_unit_size, fill) {}
  FlatMemory() = delete;
  ~FlatMemory() override;

  // Load a DataBuffer instance db from a single address, calling the Execute
  // method of inst (if inst is not equal to nullptr) with context when the data
  // has been written to db.
  void Load(uint64_t address, DataBuffer* db, Instruction* inst,
            ReferenceCount* context) override;
  // Load a DataBuffer instance db with el_size sized memory accesses using the
  // addresses stored in the address_db DataBuffer instance subject to the
  // masking of the boolean masks stored in the mask_db DataBuffer instance.
  // This is in effect a vector gather load. The DataBuffer instances must all
  // be of the same length, though the element sizes are: address_db
  // sizeof(uint64), mask_db  sizeof(bool), and db is el_size. Once the data has
  // been written to db, the Execute method of inst is called (if inst is not
  // equal to nullptr) with the given context.
  void Load(DataBuffer* address_db, DataBuffer* mask_db, int el_size,
            DataBuffer* db, Instruction* inst,
            ReferenceCount* context) override;
  // Convenience template function that calls the above function with the
  // element size as the sizeof() the template parameter type.
  template <typename T>
  void Load(DataBuffer* address_db, DataBuffer* mask_db, DataBuffer* db,
            Instruction* inst, ReferenceCount* context) {
    Load(address_db, mask_db, sizeof(T), db, inst, context);
  }

  // Store the contents of the DataBuffer instance db to memory starting at the
  // given address.
  void Store(uint64_t address, DataBuffer* db) override;
  // Store the contents of the DataBuffer instance db to memory with el_size
  // sized accesses based using the addresses stored in the address_db
  // DataBuffer instance subject to the masking of the boolean masks stored in
  // the mask_db DataBuffer instance. This is in effect a vector scatter store.
  // The DataBuffer instances must all have the same number of elements, though
  // the element sizes vary (thus the overall byte size of the DataBuffer
  // instances vary). The elements sizes are: address_db sizeof(uint64), mask_db
  // sizeof(bool), and db is el_size.
  void Store(DataBuffer* address, DataBuffer* mask, int el_size,
             DataBuffer* db) override;
  // Convenience template function that calls the above function with the
  // element size as the sizeof() the template parameter type.
  template <typename T>
  void Store(DataBuffer* address_db, DataBuffer* mask_db, DataBuffer* db) {
    Store(address_db, mask_db, sizeof(T), db);
  }

  // Accessors
  int64_t size() const { return size_; }
  uint64_t base() const { return base_; }
  int shift() const { return shift_; }

 private:
  // Private helper function used for copying data from memory to the
  // databuffer for the vector gather load.
  template <typename T>
  void LoadData(const DataBuffer* address_db, const DataBuffer* mask_db,
                int max, DataBuffer* db) {
    auto masks = mask_db->Get<bool>();
    auto addresses = address_db->Get<uint64_t>();
    bool gather = addresses.size() > 1;
    for (int entry = 0; entry < max; entry++) {
      if (masks[entry]) {
        uint64_t address =
            gather ? addresses[entry] : addresses[0] + entry * sizeof(T);
        ABSL_HARDENING_ASSERT(address >= base_);
        uint64_t offset = (address - base_) << shift_;
        ABSL_HARDENING_ASSERT(offset + sizeof(T) <= size_);
        db->Set<T>(entry, *reinterpret_cast<T*>(&memory_buffer_[offset]));
      }
    }
  }

  // Private helper function used for copying data from the databuffer to
  // memory for the vector scatter store.
  template <typename T>
  void StoreData(const DataBuffer* address_db, const DataBuffer* mask_db,
                 int max, const DataBuffer* db) {
    auto masks = mask_db->Get<bool>();
    auto addresses = address_db->Get<uint64_t>();
    bool gather = addresses.size() > 1;
    for (int entry = 0; entry < max; entry++) {
      if (masks[entry]) {
        uint64_t address =
            gather ? addresses[entry] : addresses[0] + entry * sizeof(T);
        ABSL_HARDENING_ASSERT(address >= base_);
        uint64_t offset = (address - base_) << shift_;
        ABSL_HARDENING_ASSERT(offset + sizeof(T) <= size_);
        *reinterpret_cast<T*>(&memory_buffer_[offset]) = db->Get<T>(entry);
      }
    }
  }
  int64_t size_;
  uint64_t base_;
  int shift_;
  uint8_t* memory_buffer_;
};

}  // namespace util
}  // namespace sim
}  // namespace mpact

#endif  // MPACT_SIM_UTIL_MEMORY_FLAT_MEMORY_H_
