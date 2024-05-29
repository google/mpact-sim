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

#ifndef MPACT_SIM_UTIL_RENODE_RENODE_MEMORY_ACCESS_H_
#define MPACT_SIM_UTIL_RENODE_RENODE_MEMORY_ACCESS_H_

#include <cstdint>
#include <utility>

#include "absl/functional/any_invocable.h"
#include "mpact/sim/generic/data_buffer.h"
#include "mpact/sim/generic/instruction.h"
#include "mpact/sim/generic/ref_count.h"
#include "mpact/sim/util/memory/memory_interface.h"

// This file defines a class that acts as a shim between MemoryInterface and
// ReNode memory access functions.

namespace mpact::sim::util::renode {

using ::mpact::sim::generic::DataBuffer;
using ::mpact::sim::generic::Instruction;
using ::mpact::sim::generic::ReferenceCount;
using ::mpact::sim::util::MemoryInterface;

class RenodeMemoryAccess : public MemoryInterface {
 public:
  // Function call signature for the sysbus read/write methods.
  using RenodeMemoryFunction =
      absl::AnyInvocable<int32_t(uint64_t, char *, int32_t)>;

  RenodeMemoryAccess(RenodeMemoryFunction read_fcn,
                     RenodeMemoryFunction write_fcn)
      : read_fcn_(std::move(read_fcn)), write_fcn_(std::move(write_fcn)) {}

  // MemoryInterface methods.
  // Single item load.
  void Load(uint64_t address, DataBuffer *db, Instruction *inst,
            ReferenceCount *context) override;
  // Vector load.
  void Load(DataBuffer *address_db, DataBuffer *mask_db, int el_size,
            DataBuffer *db, Instruction *inst,
            ReferenceCount *context) override;
  // Single item store.
  void Store(uint64_t address, DataBuffer *db) override;
  // Vector store.
  void Store(DataBuffer *address, DataBuffer *mask, int el_size,
             DataBuffer *db) override;

  bool has_read_fcn() const { return read_fcn_ != nullptr; }
  bool has_write_fcn() const { return write_fcn_ != nullptr; }
  void set_read_fcn(RenodeMemoryFunction read_fcn) {
    read_fcn_ = std::move(read_fcn);
  }
  void set_write_fcn(RenodeMemoryFunction write_fcn) {
    write_fcn_ = std::move(write_fcn);
  }

 private:
  // Method that is responsible to write back the load data to the appropriate
  // destination.
  void FinishLoad(int latency, Instruction *inst, ReferenceCount *context);

  // System bus read/write function pointers (point to C# delegates).
  RenodeMemoryFunction read_fcn_;
  RenodeMemoryFunction write_fcn_;
};

}  // namespace mpact::sim::util::renode

#endif  // MPACT_SIM_UTIL_RENODE_RENODE_MEMORY_ACCESS_H_
