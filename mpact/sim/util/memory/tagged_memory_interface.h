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

#ifndef MPACT_SIM_UTIL_MEMORY_TAGGED_MEMORY_INTERFACE_H_
#define MPACT_SIM_UTIL_MEMORY_TAGGED_MEMORY_INTERFACE_H_

#include <cstdint>

#include "absl/status/status.h"
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

// This interface adds new Load and Store methods to MemoryInterface in order to
// support tagged memory operations.
class TaggedMemoryInterface : public MemoryInterface {
 public:
  using MemoryInterface::Load;
  using MemoryInterface::Store;
  virtual ~TaggedMemoryInterface() = default;
  // For now, only support non-vector loads and stores with tags.
  virtual void Load(uint64_t address, DataBuffer *db, DataBuffer *tags,
                    Instruction *inst, ReferenceCount *context) = 0;
  virtual void Store(uint64_t address, DataBuffer *db, DataBuffer *tags) = 0;
};

class AtomicTaggedMemoryOpInterface : public AtomicMemoryOpInterface {
 public:
  using Operation = ::mpact::sim::util::AtomicMemoryOpInterface::Operation;
  using AtomicMemoryOpInterface::PerformMemoryOp;

  // Default destructor.
  virtual ~AtomicTaggedMemoryOpInterface() = default;

  virtual absl::Status PerformMemoryOp(uint64_t address, Operation op,
                                       DataBuffer *db, DataBuffer *tags,
                                       Instruction *inst,
                                       ReferenceCount *context) = 0;
};

}  // namespace util
}  // namespace sim
}  // namespace mpact

#endif  // MPACT_SIM_UTIL_MEMORY_TAGGED_MEMORY_INTERFACE_H_
