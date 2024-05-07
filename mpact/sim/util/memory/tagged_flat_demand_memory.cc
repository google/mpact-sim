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

#include "mpact/sim/util/memory/tagged_flat_demand_memory.h"

#include <bit>
#include <cstdint>
#include <cstring>

#include "absl/log/log.h"
#include "absl/numeric/bits.h"
#include "absl/strings/str_cat.h"
#include "mpact/sim/generic/data_buffer.h"
#include "mpact/sim/generic/instruction.h"
#include "mpact/sim/generic/ref_count.h"
#include "mpact/sim/util/memory/flat_demand_memory.h"

namespace mpact {
namespace sim {
namespace util {

using ::mpact::sim::generic::DataBuffer;
using ::mpact::sim::generic::DataBufferFactory;
using ::mpact::sim::generic::Instruction;
using ::mpact::sim::generic::ReferenceCount;

TaggedFlatDemandMemory::TaggedFlatDemandMemory(uint64_t memory_size_in_units,
                                               uint64_t base_address,
                                               unsigned addressable_unit_size,
                                               uint8_t fill,
                                               unsigned tag_granule)
    : tag_granule_(tag_granule) {
  if (!absl::has_single_bit(tag_granule_)) return;
  data_memory_ = new FlatDemandMemory(memory_size_in_units, base_address,
                                      addressable_unit_size, fill);
  // 8 tags per byte, so shift by log2(granule).
  tag_granule_shift_ = absl::bit_width(tag_granule_) - 1;
  tag_memory_ = new FlatDemandMemory(memory_size_in_units >> tag_granule_shift_,
                                     base_address >> tag_granule_shift_, 1, 0);
  db_factory_ = new DataBufferFactory();
}

TaggedFlatDemandMemory::~TaggedFlatDemandMemory() {
  delete data_memory_;
  delete tag_memory_;
  delete db_factory_;
  data_memory_ = nullptr;
  tag_memory_ = nullptr;
  db_factory_ = nullptr;
}

void TaggedFlatDemandMemory::Load(uint64_t address, DataBuffer *db,
                                  DataBuffer *tags, Instruction *inst,
                                  ReferenceCount *context) {
  if (!CheckRequest(address, db, tags)) return;

  // Load data with no latency. If db is null, then skip the load, but load the
  // tag bits. If db is null, this is just a load of tag bits.
  if (db != nullptr) {
    data_memory_->Load(address, db, nullptr, nullptr);
  }
  int latency = (db != nullptr) ? db->latency() : tags->latency();
  tag_memory_->Load(address >> tag_granule_shift_, tags, nullptr, nullptr);
  FinishLoad(latency, inst, context);
}

void TaggedFlatDemandMemory::Store(uint64_t address, DataBuffer *db,
                                   DataBuffer *tags) {
  if (!CheckRequest(address, db, tags)) return;
  // Store data and tags.
  data_memory_->Store(address, db);
  tag_memory_->Store(address >> tag_granule_shift_, tags);
}

// Untagged load is passed directly to the data memory.
void TaggedFlatDemandMemory::Load(uint64_t address, DataBuffer *db,
                                  Instruction *inst, ReferenceCount *context) {
  data_memory_->Load(address, db, inst, context);
}

// Untagged vector load is passed directly to the data memory.
void TaggedFlatDemandMemory::Load(DataBuffer *address_db, DataBuffer *mask_db,
                                  int el_size, DataBuffer *db,
                                  Instruction *inst, ReferenceCount *context) {
  data_memory_->Load(address_db, mask_db, el_size, db, inst, context);
}

// Untagged store.
void TaggedFlatDemandMemory::Store(uint64_t address, DataBuffer *db) {
  data_memory_->Store(address, db);
  // Clear any affected tags.
  ClearTags(address, db->size<uint8_t>());
}

// Untagged vector store.
void TaggedFlatDemandMemory::Store(DataBuffer *address_db, DataBuffer *mask_db,
                                   int el_size, DataBuffer *db) {
  unsigned num_stores = address_db->size<uint64_t>();
  unsigned store_size = db->size<uint8_t>() / num_stores;
  auto mask_span = mask_db->Get<bool>();
  // Perform stores.
  data_memory_->Store(address_db, mask_db, el_size, db);
  // With no tag data supplied, clear the tags, however, since this is a non
  // tagged store, we are not guaranteed to have proper alignment. Use the 64
  // bit address version of the Store() call for each set of tags.
  unsigned index = 0;
  for (auto address : address_db->Get<uint64_t>()) {
    // Clear any affected tags.
    if (mask_span[index++]) ClearTags(address, store_size);
  }
}

bool TaggedFlatDemandMemory::CheckRequest(uint64_t address,
                                          const DataBuffer *db,
                                          const DataBuffer *tags) {
  uint64_t mask = (1ULL << tag_granule_shift_) - 1;
  if (address & mask) {
    LOG(ERROR) << absl::StrCat(
        "Tagged load to ", absl::Hex(address, absl::kZeroPad16),
        " not aligned to tag granule (", tag_granule_, ")");
    return false;
  }
  if ((db != nullptr) && (db->size<uint8_t>() & mask)) {
    LOG(ERROR) << absl::StrCat(
        "Size (", db->size<uint8_t>(), ") of tagged load to ",
        absl::Hex(address, absl::kZeroPad16),
        " not a multiple of tag granule (", tag_granule_, ")");
    return false;
  }
  if ((db != nullptr) &&
      (db->size<uint8_t>() / tag_granule_) != tags->size<uint8_t>()) {
    LOG(ERROR) << absl::StrCat("Unexpected number of tags (",
                               tags->size<uint8_t>(), ") for tagged load to ",
                               absl::Hex(address, absl::kZeroPad16),
                               " for tag granule (", tag_granule_, ")");
    return false;
  }
  return true;
}

void TaggedFlatDemandMemory::FinishLoad(int latency, Instruction *inst,
                                        ReferenceCount *context) {
  if (inst == nullptr) return;
  // If the latency is 0, execute the instruction immediately.
  if (latency == 0) {
    inst->Execute(context);
    return;
  }
  // If the latency is not zero, increment the reference counts of the
  // instruction and context
  inst->IncRef();
  if (context != nullptr) context->IncRef();
  // Schedule the instruction to be executed in the future in a lambda.
  inst->state()->function_delay_line()->Add(latency, [inst, context]() {
    inst->Execute(context);
    // Decrement the reference counts.
    if (context != nullptr) context->DecRef();
    inst->DecRef();
  });
}

// Clear the tags for the given range of memory.
void TaggedFlatDemandMemory::ClearTags(uint64_t address, unsigned size) {
  uint64_t lo = address >> tag_granule_shift_;
  uint64_t hi = (address + size - 1) >> tag_granule_shift_;
  int num_tags = hi - lo + 1;
  auto *tag_db = db_factory_->Allocate(num_tags);
  std::memset(tag_db->raw_ptr(), 0, num_tags);
  tag_memory_->Store(lo, tag_db);
  tag_db->DecRef();
}

}  // namespace util
}  // namespace sim
}  // namespace mpact
