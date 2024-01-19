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

#ifndef MPACT_SIM_UTIL_MEMORY_TAGGED_FLAT_DEMAND_MEMORY_H_
#define MPACT_SIM_UTIL_MEMORY_TAGGED_FLAT_DEMAND_MEMORY_H_

#include <cstdint>

#include "mpact/sim/generic/data_buffer.h"
#include "mpact/sim/util/memory/flat_demand_memory.h"
#include "mpact/sim/util/memory/tagged_memory_interface.h"

// This file defines a class that models a flat, tagged, memory that is demand
// allocated in blocks of 16K addressable units. This builds upon the
// FlatDemandMemory class, using one instance for the data and one instance for
// the tags.

namespace mpact {
namespace sim {

// Forward declarations.
namespace generic {
class Instruction;
class DataBuffer;
class DataBufferFactory;
class ReferenceCount;
}  // namespace generic

namespace util {

class TaggedFlatDemandMemory : public TaggedMemoryInterface {
 public:
  static constexpr int kAllocationSize = FlatDemandMemory::kAllocationSize;
  // Constructors.

  // Construct an instance with 64 bit address space in units of
  // 'addressable_unit_size' bytes, with base and max address as given, and a
  // tag for every 'tag_granule' bytes.
  TaggedFlatDemandMemory(uint64_t memory_size_in_units, uint64_t base_address,
                         unsigned addressable_unit_size, uint8_t fill,
                         unsigned tag_granule);
  // Construct an instance with 64 bit byte addressable address space with base
  // and max address as given, and a tag for every 'tag_granule' bytes.
  TaggedFlatDemandMemory(uint64_t memory_size_in_units, uint64_t base_address,
                         unsigned tag_granule)
      : TaggedFlatDemandMemory(memory_size_in_units, base_address, 1, 0,
                               tag_granule) {}
  // Construct an instance with 64 bit byte addressable address space with base
  // as given, max address 0xffff'ffff'ffff'ffff, and a tag for every
  // 'tag_granule' bytes.
  TaggedFlatDemandMemory(uint64_t base_address, unsigned tag_granule)
      : TaggedFlatDemandMemory(0xffff'ffff'ffff'ffffULL, base_address, 1, 0,
                               tag_granule) {}
  // Construct an instance with 64 bit byte addressable address space with base
  // zero, max address 0xffff'ffff'ffff'ffff, and a tag for every 'tag_granule'
  // bytes.
  explicit TaggedFlatDemandMemory(unsigned tag_granule)
      : TaggedFlatDemandMemory(0xffff'ffff'ffff'ffffULL, 0, 1, 0, tag_granule) {
  }
  // Destructor.
  ~TaggedFlatDemandMemory() override;
  // Disabled constructors and operator.
  TaggedFlatDemandMemory() = delete;
  TaggedFlatDemandMemory(const TaggedFlatDemandMemory &) = delete;
  TaggedFlatDemandMemory &operator=(const TaggedFlatDemandMemory &) = delete;

  // Methods inherited directly from TaggedMemoryInterface.
  // Single address load. Loads the data into data buffer 'db', and accompanying
  // tags into 'tags'. The size of the load is taken from the size of 'db'. Both
  // the size and the address have to be aligned to the tag granule for the
  // memory. Upon completion, and timed according to the latency of 'db', if
  // non-null, 'inst' is executed with context 'context'.
  void Load(uint64_t address, generic::DataBuffer *db,
            generic::DataBuffer *tags, generic::Instruction *inst,
            generic::ReferenceCount *context) override;
  void Store(uint64_t address, generic::DataBuffer *db,
             generic::DataBuffer *tags) override;
  // Methods inherited from MemoryInterface. These methods do not handle tags.
  // Loads are identical to FlatDemandMemory loads.
  void Load(uint64_t address, generic::DataBuffer *db,
            generic::Instruction *inst,
            generic::ReferenceCount *context) override;
  void Load(generic::DataBuffer *address_db, generic::DataBuffer *mask_db,
            int el_size, generic::DataBuffer *db, Instruction *inst,
            generic::ReferenceCount *context) override;
  // Convenience template function that calls the above function with the
  // element size as the sizeof() the template parameter type.
  template <typename T>
  void Load(DataBuffer *address_db, DataBuffer *mask_db, DataBuffer *db,
            Instruction *inst, ReferenceCount *context) {
    Load(address_db, mask_db, sizeof(T), db, inst, context);
  }
  // Store methods inherited from MemoryInterface. These methods do not handle
  // tags. Instead, tags are cleared for any part of memory that is written to.
  void Store(uint64_t address, DataBuffer *db) override;
  void Store(generic::DataBuffer *address_db, generic::DataBuffer *mask_db,
             int el_size, generic::DataBuffer *db) override;
  // Convenience template function that calls the above function with the
  // element size as the sizeof() the template parameter type.
  template <typename T>
  void Store(DataBuffer *address_db, DataBuffer *mask_db, DataBuffer *db) {
    Store(address_db, mask_db, sizeof(T), db);
  }

 private:
  // Check that the tagged load or store is properly aligned to the tag
  // granule, and that the number of tags provided is correct.
  bool CheckRequest(uint64_t address, const generic::DataBuffer *db,
                    const generic::DataBuffer *tags);
  // Complete the load
  void FinishLoad(int latency, generic::Instruction *inst,
                  generic::ReferenceCount *context);
  // Clear the tags for the specified range of memory.
  void ClearTags(uint64_t address, unsigned size);

  unsigned tag_granule_;
  unsigned tag_granule_shift_;
  FlatDemandMemory *data_memory_ = nullptr;
  FlatDemandMemory *tag_memory_ = nullptr;
  generic::DataBufferFactory *db_factory_;
};

}  // namespace util
}  // namespace sim
}  // namespace mpact

#endif  // MPACT_SIM_UTIL_MEMORY_TAGGED_FLAT_DEMAND_MEMORY_H_
