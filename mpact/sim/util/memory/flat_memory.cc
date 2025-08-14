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

#include "mpact/sim/util/memory/flat_memory.h"

#include <cstdint>
#include <cstring>

#include "absl/base/macros.h"
#include "absl/numeric/bits.h"

namespace mpact {
namespace sim {
namespace util {

void FlatMemory::Load(uint64_t address, DataBuffer* db, Instruction* inst,
                      ReferenceCount* context) {
  ABSL_HARDENING_ASSERT(address >= base_);
  uint64_t offset = (address - base_) << shift_;
  ABSL_HARDENING_ASSERT(offset + db->size<uint8_t>() <= size_);
  uint8_t* ptr = &memory_buffer_[offset];
  db->CopyFrom(ptr);
  if (nullptr != inst) {
    if (db->latency() > 0) {
      // If the context is non-null, need to IncRef it before storing it in the
      // lambda.
      if (context != nullptr) context->IncRef();
      inst->state()->function_delay_line()->Add(db->latency(),
                                                [inst, context]() {
                                                  inst->Execute(context);
                                                  if (context != nullptr)
                                                    context->DecRef();
                                                });
    } else {
      inst->Execute(context);
    }
  }
}

void FlatMemory::Load(DataBuffer* address_db, DataBuffer* mask_db, int el_size,
                      DataBuffer* db, Instruction* inst,
                      ReferenceCount* context) {
  int max = mask_db->size<bool>();
  switch (el_size) {
    case 1:
      LoadData<uint8_t>(address_db, mask_db, max, db);
      break;
    case 2:
      LoadData<uint16_t>(address_db, mask_db, max, db);
      break;
    case 4:
      LoadData<uint32_t>(address_db, mask_db, max, db);
      break;
    case 8:
      LoadData<uint64_t>(address_db, mask_db, max, db);
      break;
    default:
      // Error if the default case is hit.
      ABSL_HARDENING_ASSERT(false);
      break;
  }
  if (nullptr != inst) {
    if (db->latency() > 0) {
      // If the context is non-null, need to IncRef it before storing it in the
      // lambda.
      if (context != nullptr) context->IncRef();
      inst->state()->function_delay_line()->Add(db->latency(),
                                                [inst, context]() {
                                                  inst->Execute(context);
                                                  if (context != nullptr)
                                                    context->DecRef();
                                                });
    } else {
      inst->Execute(context);
    }
  }
}

void FlatMemory::Store(uint64_t address, DataBuffer* db) {
  ABSL_HARDENING_ASSERT(address >= base_);
  uint64_t offset = (address - base_) << shift_;
  ABSL_HARDENING_ASSERT(offset + db->size<uint8_t>() <= size_);
  uint8_t* ptr = &memory_buffer_[offset];
  db->CopyTo(ptr);
}

void FlatMemory::Store(DataBuffer* address_db, DataBuffer* mask_db, int el_size,
                       DataBuffer* db) {
  int max = mask_db->size<bool>();

  switch (el_size) {
    case 1:
      StoreData<uint8_t>(address_db, mask_db, max, db);
      break;
    case 2:
      StoreData<uint16_t>(address_db, mask_db, max, db);
      break;
    case 4:
      StoreData<uint32_t>(address_db, mask_db, max, db);
      break;
    case 8:
      StoreData<uint64_t>(address_db, mask_db, max, db);
      break;
    default:
      // Error if the default case is hit.
      ABSL_HARDENING_ASSERT(false);
      break;
  }
}

FlatMemory::FlatMemory(int64_t memory_size_in_units, uint64_t base_address,
                       uint32_t addressable_unit_size, uint8_t fill)
    : size_(memory_size_in_units * addressable_unit_size),
      base_(base_address),
      memory_buffer_(nullptr) {
  ABSL_HARDENING_ASSERT(absl::has_single_bit(addressable_unit_size));
  shift_ = absl::countr_zero(addressable_unit_size);
  memory_buffer_ = new uint8_t[size_];
  if (nullptr == memory_buffer_) {
    size_ = 0;
  }
  memset(memory_buffer_, fill, size_);
}

FlatMemory::~FlatMemory() { delete[] memory_buffer_; }

}  // namespace util
}  // namespace sim
}  // namespace mpact
