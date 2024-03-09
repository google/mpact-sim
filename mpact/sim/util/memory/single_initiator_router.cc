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

#include "mpact/sim/util/memory/single_initiator_router.h"

#include <cstdint>
#include <string>

#include "absl/container/btree_map.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "mpact/sim/util/memory/memory_interface.h"
#include "mpact/sim/util/memory/tagged_memory_interface.h"

// This file provides the method implementation of the SingleInitiatorRouter
// class.

namespace mpact {
namespace sim {
namespace util {

SingleInitiatorRouter::SingleInitiatorRouter(std::string name) : name_(name) {}

SingleInitiatorRouter::~SingleInitiatorRouter() {}

// This is a convenience helper function used to implement the three AddTarget
// methods, that add memory targets to the router, providing the
// memory interface, base address, and address space size.
template <typename Interface>
static absl::Status AddTarget(
    SingleInitiatorRouter::InterfaceMap<Interface> &map, Interface *interface,
    uint64_t base, uint64_t top) {
  // Make sure the range is valid and makes sense.
  if (base > top) {
    return absl::InvalidArgumentError(
        "Memory range base must be less than the top");
  }
  if (base == top) {
    return absl::InvalidArgumentError("Memory range size must be non-zero");
  }
  // Make sure the range doesn't conflict with an existing range.
  auto it = map.find({base, top});
  if (it != map.end()) {
    // If it is a duplicate, just ignore it and return ok.
    if ((it->first.base == base) && (it->first.top == top)) {
      return absl::OkStatus();
    }
    // Otherwise return an error.
    return absl::AlreadyExistsError(absl::StrCat(
        "Memory range conflicts with existing range: [",
        absl::Hex(it->first.base), "..", absl::Hex(it->first.top), ">"));
  }
  map.emplace(SingleInitiatorRouter::AddressRange{base, top}, interface);
  return absl::OkStatus();
}

absl::Status SingleInitiatorRouter::AddTarget(MemoryInterface *memory,
                                              uint64_t base, uint64_t top) {
  return AddTarget<MemoryInterface>(memory_targets_, memory, base, top);
}

absl::Status SingleInitiatorRouter::AddTarget(
    TaggedMemoryInterface *tagged_memory, uint64_t base, uint64_t top) {
  return AddTarget<TaggedMemoryInterface>(tagged_targets_, tagged_memory, base,
                                          top);
}

absl::Status SingleInitiatorRouter::AddTarget(
    AtomicMemoryOpInterface *atomic_memory, uint64_t base, uint64_t top) {
  return AddTarget<AtomicMemoryOpInterface>(atomic_targets_, atomic_memory,
                                            base, top);
}

// The next methods are the overridden methods of the different memory
// interfaces. These perform lookups to find an appropriate target. Failing that
// they log an error and return.

// Plain memory load.
void SingleInitiatorRouter::Load(uint64_t address, DataBuffer *db,
                                 Instruction *inst, ReferenceCount *context) {
  int size = db->size<uint8_t>();
  auto it = memory_targets_.find({address, address + size - 1});
  if (it != memory_targets_.end()) {
    auto &range = it->first;
    if ((range.base <= address) && (range.top >= address + size - 1)) {
      return it->second->Load(address, db, inst, context);
    }
  }
  // Since the plain memory load can occur in both MemoryInterface and
  // TaggedMemoryInterface, we need to check the tagged memory interface map
  // too.
  auto tagged_it = tagged_targets_.find({address, address});
  if (tagged_it != tagged_targets_.end()) {
    auto &range = tagged_it->first;
    if ((range.base <= address) && (range.top >= address + size - 1)) {
      return tagged_it->second->Load(address, db, inst, context);
    }
  }
  LOG(ERROR) << absl::StrCat("No target found for address: 0x",
                             absl::Hex(address));
}

// Vector memory load.
void SingleInitiatorRouter::Load(DataBuffer *address_db, DataBuffer *mask_db,
                                 int el_size, DataBuffer *db, Instruction *inst,
                                 ReferenceCount *context) {
  // This is a vector load, so check each address that is not masked off.
  // Vector memory loads will not be split across multiple targets.
  int count = address_db->size<uint64_t>();
  MemoryInterface *memory = nullptr;
  for (int i = 0; i < count; i++) {
    // If the mask is true, check this address.
    if (mask_db->Get<uint8_t>(i)) {
      auto address = address_db->Get<uint64_t>(i);
      auto it = memory_targets_.find({address, address + el_size - 1});
      if (it == memory_targets_.end()) break;
      auto &range = it->first;
      if ((range.base > address) || (range.top < address + el_size - 1)) break;
      if (memory != nullptr) {
        if (it->second != memory) {
          LOG(ERROR) << "Multiple targets found for address vector load";
          return;
        }
      } else {
        memory = it->second;
      }
    }
  }
  if (memory != nullptr) {
    return memory->Load(address_db, mask_db, el_size, db, inst, context);
  }
  // Since the vector memory load can occur in both MemoryInterface and
  // TaggedMemoryInterface, we need to check the tagged memory interface map
  // too.
  TaggedMemoryInterface *tagged_memory = nullptr;
  for (int i = 0; i < count; i++) {
    if (mask_db->Get<uint8_t>(i)) {
      auto address = address_db->Get<uint64_t>(i);
      auto it = tagged_targets_.find({address, address + el_size - 1});
      if (it == tagged_targets_.end()) break;
      auto &range = it->first;
      if ((range.base > address) || (range.top < address + el_size - 1)) break;
      if (tagged_memory != nullptr) {
        if (it->second != tagged_memory) {
          LOG(ERROR) << "Multiple targets found for vector load";
          return;
        }
      } else {
        tagged_memory = it->second;
      }
    }
  }
  if (tagged_memory != nullptr) {
    return tagged_memory->Load(address_db, mask_db, el_size, db, inst, context);
  }
  LOG(ERROR) << "No target found for vector load";
}

// Tagged memory load.
void SingleInitiatorRouter::Load(uint64_t address, DataBuffer *db,
                                 DataBuffer *tags, Instruction *inst,
                                 ReferenceCount *context) {
  int size;
  // If db is null, then this is a tag load. The size for routing purposes is
  // the size of tags * 8.
  if (db == nullptr) {
    size = tags->size<uint8_t>() << 3;
  } else {
    size = db->size<uint8_t>();
  }
  auto tagged_it = tagged_targets_.find({address, address + size - 1});
  if (tagged_it != tagged_targets_.end()) {
    auto &range = tagged_it->first;
    if ((range.base <= address) && (range.top >= address + size - 1)) {
      return tagged_it->second->Load(address, db, tags, inst, context);
    }
  }
  LOG(ERROR) << absl::StrCat("No target found for address: 0x",
                             absl::Hex(address));
}

// Plain memory store.
void SingleInitiatorRouter::Store(uint64_t address, DataBuffer *db) {
  int size = db->size<uint8_t>();
  auto it = memory_targets_.find({address, address + size});
  if (it != memory_targets_.end()) {
    auto &range = it->first;
    if ((range.base <= address) && (range.top >= address + size - 1)) {
      return it->second->Store(address, db);
    }
  }
  // Since the plain memory store can occur in both MemoryInterface and
  // TaggedMemoryInterface, we need to check the tagged memory interface map
  // too.
  auto tagged_it = tagged_targets_.find({address, address});
  if (tagged_it != tagged_targets_.end()) {
    auto &range = tagged_it->first;
    if ((range.base <= address) && (range.top >= address + size - 1)) {
      return tagged_it->second->Store(address, db);
    }
  }
  LOG(ERROR) << absl::StrCat("No target found for address: 0x",
                             absl::Hex(address));
}

// Vector memory store.
void SingleInitiatorRouter::Store(DataBuffer *address_db, DataBuffer *mask_db,
                                  int el_size, DataBuffer *db) {
  // This is a vector store, so check each address that is not masked off.
  // Vector memory stores will not be split across multiple targets.
  int count = address_db->size<uint64_t>();
  MemoryInterface *memory = nullptr;
  for (int i = 0; i < count; i++) {
    // If the mask is true, check this address.
    if (mask_db->Get<uint8_t>(i)) {
      auto address = address_db->Get<uint64_t>(i);
      auto it = memory_targets_.find({address, address + el_size - 1});
      if (it == memory_targets_.end()) break;
      auto &range = it->first;
      if ((range.base > address) || (range.top < address + el_size - 1)) break;
      if (memory != nullptr) {
        if (it->second != memory) {
          LOG(ERROR) << "Multiple targets found for vector store";
          return;
        }
      } else {
        memory = it->second;
      }
    }
  }
  if (memory != nullptr) {
    return memory->Store(address_db, mask_db, el_size, db);
  }
  // Since the vector memory store can occur in both MemoryInterface and
  // TaggedMemoryInterface, we need to check the tagged memory interface map
  // too.
  TaggedMemoryInterface *tagged_memory = nullptr;
  for (int i = 0; i < count; i++) {
    if (mask_db->Get<uint8_t>(i)) {
      auto address = address_db->Get<uint64_t>(i);
      auto it = tagged_targets_.find({address, address + el_size});
      if (it == tagged_targets_.end()) break;
      auto &range = it->first;
      if ((range.base > address) || (range.top < address + el_size - 1)) break;
      if (tagged_memory != nullptr) {
        if (it->second != tagged_memory) {
          LOG(ERROR) << "Multiple targets found for vector store";
          return;
        }
      } else {
        tagged_memory = it->second;
      }
    }
  }
  if (tagged_memory != nullptr) {
    return tagged_memory->Store(address_db, mask_db, el_size, db);
  }
  LOG(ERROR) << "No target found for vector store";
}

// Simple tagged memory store.
void SingleInitiatorRouter::Store(uint64_t address, DataBuffer *db,
                                  DataBuffer *tags) {
  int size = db->size<uint8_t>();
  auto tagged_it = tagged_targets_.find({address, address + size - 1});
  if (tagged_it != tagged_targets_.end()) {
    auto &range = tagged_it->first;
    if ((range.base <= address) && (range.top >= address + size - 1)) {
      return tagged_it->second->Store(address, db, tags);
    }
  }
  LOG(ERROR) << absl::StrCat("No target found for address: 0x",
                             absl::Hex(address));
}

// Atomic memory operation.
absl::Status SingleInitiatorRouter::PerformMemoryOp(uint64_t address,
                                                    Operation op,
                                                    DataBuffer *db,
                                                    Instruction *inst,
                                                    ReferenceCount *context) {
  int size = db->size<uint8_t>();
  auto atomic_it = atomic_targets_.find({address, address + size - 1});
  if (atomic_it != atomic_targets_.end()) {
    auto &range = atomic_it->first;
    if ((range.base <= address) && (range.top >= address + size - 1)) {
      return atomic_it->second->PerformMemoryOp(address, op, db, inst, context);
    }
  }
  return absl::NotFoundError(
      absl::StrCat("No target found for address: ", absl::Hex(address)));
}

}  // namespace util
}  // namespace sim
}  // namespace mpact
