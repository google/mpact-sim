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

#include "mpact/sim/util/memory/tagged_to_untagged_memory_transactor.h"

#include <cstdint>
#include <cstring>

#include "absl/log/log.h"
#include "absl/strings/str_cat.h"
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

// These methods simply forward the call to the target mem  interface.
void TaggedToUntaggedMemoryTransactor::Load(uint64_t address, DataBuffer *db,
                                            Instruction *inst,
                                            ReferenceCount *context) {
  target_mem_->Load(address, db, inst, context);
}
void TaggedToUntaggedMemoryTransactor::Load(DataBuffer *address_db,
                                            DataBuffer *mask_db, int el_size,
                                            DataBuffer *db, Instruction *inst,
                                            ReferenceCount *context) {
  target_mem_->Load(address_db, mask_db, el_size, db, inst, context);
}

void TaggedToUntaggedMemoryTransactor::Store(uint64_t address, DataBuffer *db) {
  target_mem_->Store(address, db);
}

void TaggedToUntaggedMemoryTransactor::Store(DataBuffer *address,
                                             DataBuffer *mask, int el_size,
                                             DataBuffer *db) {
  target_mem_->Store(address, mask, el_size, db);
}

// For tagged loads, zero the tags, and then forward to the target mem
// interface.
void TaggedToUntaggedMemoryTransactor::Load(uint64_t address, DataBuffer *db,
                                            DataBuffer *tags, Instruction *inst,
                                            ReferenceCount *context) {
  std::memset(tags->raw_ptr(), 0, tags->size<uint8_t>());
  target_mem_->Load(address, db, inst, context);
}

// For tagged stores, ignore the tags. just forward to the target mem interface.
void TaggedToUntaggedMemoryTransactor::Store(uint64_t address, DataBuffer *db,
                                             DataBuffer *tags) {
  if (tags != nullptr) {
    for (auto tag : tags->Get<uint8_t>()) {
      if (tag != 0)
        LOG(ERROR) << absl::StrCat(
            "Unexpected valid tag in store to non-tagged memory at address: ",
            absl::Hex(address, absl::kZeroPad8));
    }
  }
  target_mem_->Store(address, db);
}

}  // namespace util
}  // namespace sim
}  // namespace mpact
