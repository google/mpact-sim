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

#include "mpact/sim/util/renode/renode_memory_access.h"

#include <cstdint>
#include <cstring>

#include "absl/log/log.h"
#include "mpact/sim/generic/data_buffer.h"
#include "mpact/sim/generic/instruction.h"
#include "mpact/sim/generic/ref_count.h"

namespace mpact::sim::util::renode {

using ::mpact::sim::generic::DataBuffer;
using ::mpact::sim::generic::Instruction;
using ::mpact::sim::generic::ReferenceCount;

// Process the load using the interface to the ReNode system bus to fetch data.
void RenodeMemoryAccess::Load(uint64_t address, DataBuffer* db,
                              Instruction* inst, ReferenceCount* context) {
  if (read_fcn_ == nullptr) {
    LOG(WARNING) << "RenodeMemoryAccess: read_fcn_ is null";
    std::memset(db->raw_ptr(), 0, db->size<uint8_t>());
    FinishLoad(db->latency(), inst, context);
    return;
  }
  int32_t size = db->size<uint8_t>();
  // Call the C# function delegate.
  auto bytes_read = read_fcn_(address, static_cast<char*>(db->raw_ptr()), size);
  if (size != bytes_read) {
    LOG(ERROR) << "Failed to read " << size - bytes_read << " bytes of "
               << size;
  }
  FinishLoad(db->latency(), inst, context);
}

// TODO(torerik): add vector load functionality.
void RenodeMemoryAccess::Load(DataBuffer* address_db, DataBuffer* mask_db,
                              int el_size, DataBuffer* db, Instruction* inst,
                              ReferenceCount* context) {
  LOG(ERROR) << "RenodeMemoryAccess: Vector loads are not supported";
}

// Complete the load by writing back (or scheduling the write back of) the
// fetched data.
void RenodeMemoryAccess::FinishLoad(int latency, Instruction* inst,
                                    ReferenceCount* context) {
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

// Process the store using the interface to the ReNode system bus to store data.
void RenodeMemoryAccess::Store(uint64_t address, DataBuffer* db) {
  if (write_fcn_ == nullptr) {
    LOG(WARNING) << "RenodeMemoryAccess: write_fcn_ is null";
    return;
  }
  int32_t size = db->size<uint8_t>();
  // Call the C# function delegate.
  auto bytes_written =
      write_fcn_(address, static_cast<char*>(db->raw_ptr()), size);
  if (size != bytes_written) {
    LOG(ERROR) << "Failed to write " << size - bytes_written << " bytes of "
               << size;
  }
}

// TODO(torerik): add vector store functionality.
void RenodeMemoryAccess::Store(DataBuffer* address, DataBuffer* mask,
                               int el_size, DataBuffer* db) {
  LOG(ERROR) << "RenodeMemoryAccess: Vector stores are not supported";
}

}  // namespace mpact::sim::util::renode
