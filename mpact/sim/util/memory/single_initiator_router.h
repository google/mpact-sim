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

#ifndef MPACT_SIM_UTIL_MEMORY_SINGLE_INITIATOR_ROUTER_H_
#define MPACT_SIM_UTIL_MEMORY_SINGLE_INITIATOR_ROUTER_H_

#include <cstdint>
#include <string>
#include <string_view>

#include "absl/container/btree_map.h"
#include "absl/status/status.h"
#include "mpact/sim/util/memory/memory_interface.h"
#include "mpact/sim/util/memory/tagged_memory_interface.h"

// This file defines a single initiator router class that routes memory accesses
// from an initiator to a set of targets based on memory address. The class
// implements both the Tagged and Atomic memory interfaces, so all memory
// accesses can be routed. Targets are added according to which memory interface
// they support (MemoryInterface, TaggedMemoryInterface, and
// AtomicMemoryOpInterface). A target that supports both a
// (Tagged)MemoryInterface and the AtomicMemoryOpInterface needs to be added
// twice: once for the baseline memory accesses, and then for the atomic memory
// interface.

namespace mpact {
namespace sim {
namespace util {

class SingleInitiatorRouter : public TaggedMemoryInterface,
                              public AtomicMemoryOpInterface {
 public:
  // Class local struct for handling address ranges.
  // Base is the address of the first byte in the range. Top is the address of
  // the last byte in the range.
  struct AddressRange {
    uint64_t base;
    uint64_t top;
  };
  // Comparator for address ranges.
  struct AddressRangeLess {
    bool operator()(const AddressRange &lhs, const AddressRange &rhs) const {
      return (lhs.top < rhs.base);
    }
  };
  // Map type convenience template.
  template <typename Interface>
  using InterfaceMap =
      absl::btree_map<SingleInitiatorRouter::AddressRange, Interface *,
                      SingleInitiatorRouter::AddressRangeLess>;

  // Constructor and destructor.
  explicit SingleInitiatorRouter(std::string name);
  SingleInitiatorRouter(const SingleInitiatorRouter &) = delete;
  SingleInitiatorRouter &operator=(const SingleInitiatorRouter &) = delete;
  ~SingleInitiatorRouter();

  // Add 'memory' target interface with given range. Only three interface types
  // are supported: MemoryInterface, TaggedMemoryInterface, and
  // AtomicMemoryOpInterface.
  template <typename Interface>
  absl::Status AddTarget(Interface *memory, uint64_t base, uint64_t top);

  // Add 'memory' target interface as default target, that is, if no other
  // interface matches, it is the fallback interface. Only three interface types
  // are supported: MemoryInterface, TaggedMemoryInterface, and
  // AtomicMemoryOpInterface.
  template <typename Interface>
  absl::Status AddDefaultTarget(Interface *memory);

  // Memory interface methods.
  // Plain load.
  void Load(uint64_t address, DataBuffer *db, Instruction *inst,
            ReferenceCount *context) override;
  // Vector load.
  void Load(DataBuffer *address_db, DataBuffer *mask_db, int el_size,
            DataBuffer *db, Instruction *inst,
            ReferenceCount *context) override;
  // Tagged load.
  void Load(uint64_t address, DataBuffer *db, DataBuffer *tags,
            Instruction *inst, ReferenceCount *context) override;
  // Plain store.
  void Store(uint64_t address, DataBuffer *db) override;
  // Vector store.
  void Store(DataBuffer *address_db, DataBuffer *mask_db, int el_size,
             DataBuffer *db) override;
  // Tagged store.
  void Store(uint64_t address, DataBuffer *db, DataBuffer *tags) override;
  // Atomic memory operation.
  absl::Status PerformMemoryOp(uint64_t address, Operation op, DataBuffer *db,
                               Instruction *inst,
                               ReferenceCount *context) override;

  // Accessors.
  std::string_view name() const { return name_; }

 private:
  std::string name_;
  // These maps are used to look up target interfaces based on addresses.
  InterfaceMap<MemoryInterface> memory_targets_;
  MemoryInterface *default_memory_target_ = nullptr;
  InterfaceMap<TaggedMemoryInterface> tagged_targets_;
  TaggedMemoryInterface *default_tagged_target_ = nullptr;
  InterfaceMap<AtomicMemoryOpInterface> atomic_targets_;
  AtomicMemoryOpInterface *default_atomic_target_ = nullptr;
};

}  // namespace util
}  // namespace sim
}  // namespace mpact

#endif  // MPACT_SIM_UTIL_MEMORY_SINGLE_INITIATOR_ROUTER_H_
