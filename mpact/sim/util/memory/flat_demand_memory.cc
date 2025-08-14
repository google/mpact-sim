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

#include "mpact/sim/util/memory/flat_demand_memory.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <utility>

#include "absl/base/macros.h"
#include "absl/numeric/bits.h"

namespace mpact {
namespace sim {
namespace util {

FlatDemandMemory::FlatDemandMemory(uint64_t memory_size_in_units,
                                   uint64_t base_address,
                                   unsigned addressable_unit_size, uint8_t fill)
    : base_address_(base_address),
      max_address_(base_address + memory_size_in_units),
      fill_value_(fill),
      allocation_byte_size_(kAllocationSize * addressable_unit_size) {
  // Compute the addressable unit shift.
  ABSL_HARDENING_ASSERT(
      (addressable_unit_size != 0) &&
      ((addressable_unit_size & (addressable_unit_size - 1)) == 0));
  addressable_unit_shift_ = absl::bit_width(addressable_unit_size) - 1;
}

// Delete all the allocated blocks.
FlatDemandMemory::~FlatDemandMemory() { Clear(); }

void FlatDemandMemory::Load(uint64_t address, DataBuffer* db, Instruction* inst,
                            ReferenceCount* context) {
  int size_in_units = db->size<uint8_t>() >> addressable_unit_shift_;
  uint64_t high = address + size_in_units;
  ABSL_HARDENING_ASSERT((address >= base_address_) && (high <= max_address_));
  ABSL_HARDENING_ASSERT(size_in_units > 0);
  uint8_t* byte_ptr = static_cast<uint8_t*>(db->raw_ptr());
  // Load the data into the data buffer.
  LoadStoreHelper(address, byte_ptr, size_in_units, true);
  // Execute the instruction to process and write back the load data.
  if (nullptr != inst) {
    if (db->latency() > 0) {
      inst->IncRef();
      if (context != nullptr) context->IncRef();
      inst->state()->function_delay_line()->Add(db->latency(),
                                                [inst, context]() {
                                                  inst->Execute(context);
                                                  if (context != nullptr)
                                                    context->DecRef();
                                                  inst->DecRef();
                                                });
    } else {
      inst->Execute(context);
    }
  }
}

void FlatDemandMemory::Load(DataBuffer* address_db, DataBuffer* mask_db,
                            int el_size, DataBuffer* db, Instruction* inst,
                            ReferenceCount* context) {
  auto mask_span = mask_db->Get<bool>();
  auto address_span = address_db->Get<uint64_t>();
  uint8_t* byte_ptr = static_cast<uint8_t*>(db->raw_ptr());
  int size_in_units = el_size >> addressable_unit_shift_;
  ABSL_HARDENING_ASSERT(size_in_units > 0);
  // This is either a gather load, or a unit stride load depending on size of
  // the address span.
  bool gather = address_span.size() > 1;
  for (unsigned i = 0; i < mask_span.size(); i++) {
    if (!mask_span[i]) continue;
    uint64_t address = gather ? address_span[i] : address_span[0] + i * el_size;
    uint64_t high = address + size_in_units;
    ABSL_HARDENING_ASSERT((address >= base_address_) && (high <= max_address_));
    LoadStoreHelper(address, &byte_ptr[el_size * i], size_in_units, true);
  }
  // Execute the instruction to process and write back the load data.
  if (nullptr != inst) {
    if (db->latency() > 0) {
      inst->IncRef();
      if (context != nullptr) context->IncRef();
      inst->state()->function_delay_line()->Add(db->latency(),
                                                [inst, context]() {
                                                  inst->Execute(context);
                                                  if (context != nullptr)
                                                    context->DecRef();
                                                  inst->DecRef();
                                                });
    } else {
      inst->Execute(context);
    }
  }
}

void FlatDemandMemory::Store(uint64_t address, DataBuffer* db) {
  int size_in_units = db->size<uint8_t>() >> addressable_unit_shift_;
  uint64_t high = address + size_in_units;
  ABSL_HARDENING_ASSERT((address >= base_address_) && (high <= max_address_));
  ABSL_HARDENING_ASSERT(size_in_units > 0);
  uint8_t* byte_ptr = static_cast<uint8_t*>(db->raw_ptr());
  LoadStoreHelper(address, byte_ptr, size_in_units, /*is_load*/ false);
}

void FlatDemandMemory::Store(DataBuffer* address_db, DataBuffer* mask_db,
                             int el_size, DataBuffer* db) {
  auto mask_span = mask_db->Get<bool>();
  auto address_span = address_db->Get<uint64_t>();
  uint8_t* byte_ptr = static_cast<uint8_t*>(db->raw_ptr());
  int size_in_units = el_size >> addressable_unit_shift_;
  ABSL_HARDENING_ASSERT(size_in_units > 0);
  // If the address_span.size() > 1, then this is a scatter store, otherwise
  // it's a unit stride store.
  bool scatter = address_span.size() > 1;
  for (unsigned i = 0; i < mask_span.size(); i++) {
    if (!mask_span[i]) continue;
    uint64_t address =
        scatter ? address_span[i] : address_span[0] + i * el_size;
    uint64_t high = address + size_in_units;
    ABSL_HARDENING_ASSERT((address >= base_address_) && (high <= max_address_));
    LoadStoreHelper(address, &byte_ptr[el_size * i], size_in_units,
                    /*is_load*/ false);
  }
}

void FlatDemandMemory::LoadStoreHelper(uint64_t address, uint8_t* data_ptr,
                                       int size_in_units, bool is_load) {
  // Repeat the following while there is data to copy. If it's a big chunk of
  // data it may span across more than one block.
  do {
    // Find the block, allocate a new one if needed.
    uint64_t block_address = address >> kAllocationShift;
    auto iter = block_map_.find(block_address);

    uint8_t* block;
    if (iter == block_map_.end()) {
      block = new uint8_t[allocation_byte_size_];
      block_map_.insert(std::make_pair(block_address, block));
      std::memset(block, fill_value_, allocation_byte_size_);
    } else {
      block = iter->second;
    }

    int block_unit_offset = (address & kAllocationMask);

    // Compute how many addressable units to load/store from/to the current
    // block.
    int store_size_in_units =
        std::min(size_in_units, kAllocationSize - block_unit_offset);

    // Translate from unit size to byte size.
    int store_size_in_bytes = store_size_in_units << addressable_unit_shift_;
    int block_byte_offset = block_unit_offset << addressable_unit_shift_;

    if (is_load) {
      std::memcpy(data_ptr, &block[block_byte_offset], store_size_in_bytes);
    } else {
      std::memcpy(&block[block_byte_offset], data_ptr, store_size_in_bytes);
    }

    // Adjust address, data pointer and the remaining data left to be
    // loaded/stored.
    size_in_units -= store_size_in_units;
    data_ptr += store_size_in_bytes;
    address += store_size_in_units;
  } while (size_in_units > 0);
}

void FlatDemandMemory::Clear() {
  for (auto& [unused, block_ptr] : block_map_) {
    delete[] block_ptr;
  }
  block_map_.clear();
}

}  // namespace util
}  // namespace sim
}  // namespace mpact
