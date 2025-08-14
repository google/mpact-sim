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

#ifndef MPACT_SIM_UTIL_MEMORY_MEMORY_INTERFACE_H_
#define MPACT_SIM_UTIL_MEMORY_MEMORY_INTERFACE_H_

#include <cstdint>

#include "mpact/sim/generic/data_buffer.h"
#include "mpact/sim/generic/instruction.h"
#include "mpact/sim/generic/ref_count.h"

namespace mpact {
namespace sim {
namespace util {

using ::mpact::sim::generic::DataBuffer;
using ::mpact::sim::generic::Instruction;
using ::mpact::sim::generic::ReferenceCount;

// This class defines the interface to perform load/store from memory.
// There are two base methods each for Loads and Stores, as well as a
// pair of templated convience methods.
//
// Load operations take part in two steps: Mem and Writeback. The Load call
// itself constitutes the Mem step, which is when the values in memory are
// read and copied to the DataBuffer. Once done, the Mem step is responsible
// for scheduling the Instruction to be executed with the given context with
// the latency associated with the DataBuffer.
//
// This interface does not do any inter-access ordering, for instance performing
// loads before stores. That is the responsibility of the calling entity.
class MemoryInterface {
 public:
  // Default destructor.
  virtual ~MemoryInterface() = default;

  // Load data from address into the DataBuffer, then schedule the Instruction
  // inst (if not nullptr) to be executed (using the function delay line) with
  // context. The size of the data access is based on size of the data buffer.
  virtual void Load(uint64_t address, DataBuffer* db, Instruction* inst,
                    ReferenceCount* context) = 0;
  // Load data from 1 or N addresses stored in address_db (uint64), using
  // mask_db (bool) to mask out the corresponding loads from taking place (if
  // false). Each access is el_size bytes long, and is stored into the
  // DataBuffer. Once done, the Instruction inst (if not nullptr) is scheduled
  // to be executed (using the function delay line) with context. It's the
  // responsibility of the caller to ensure that all DataBuffer instances passed
  // in are appropriately sized. Use the DataBuffer size<uint64_t> to determine
  // the number of addresses availalble. The following summarizes the parameter
  // requirements:
  virtual void Load(DataBuffer* address_db, DataBuffer* mask_db, int el_size,
                    DataBuffer* db, Instruction* inst,
                    ReferenceCount* context) = 0;
  // Convenience template function that calls the above function with the
  // element size as the sizeof() the template parameter type.
  template <typename T>
  void Load(DataBuffer* address_db, DataBuffer* mask_db, DataBuffer* db,
            Instruction* inst, ReferenceCount* context) {
    Load(address_db, mask_db, sizeof(T), db, inst, context);
  }

  // Stores data from the DataBuffer instance to memory starting at address.
  virtual void Store(uint64_t address, DataBuffer* db) = 0;
  // Stores data starting at each of the 1 or N addresses stored in address_db,
  // (uint64) using mask_db (bool) to mask out stores from taking place (if
  // false). Each store is el_size bytes long. It's the responsibility of the
  // caller to ensure that all DataBuffer instances that are passed in are
  // appropriately sized. The following summarizes the parameter
  // requirements:
  //   address_db->size<uint64_t>)() is either 1 or N
  //   mask_db->size<bool>() is N
  //   db->size<uint8_t>()/el_size = N.
  virtual void Store(DataBuffer* address, DataBuffer* mask, int el_size,
                     DataBuffer* db) = 0;
  // Convenience template function that calls the above function with the
  // element size as the sizeof() the template parameter type.
  template <typename T>
  void Store(DataBuffer* address_db, DataBuffer* mask_db, DataBuffer* db) {
    Store(address_db, mask_db, sizeof(T), db);
  }
};

// This an additional memory interface that can be used to to perform atomic
// operations in memory. It supports LL/SC as well as atomic operations
// performed in (or near) memory. No implementation is required, or even
// expected to support all the operations. For unsupported operations the
// implementation should return absl::UnimplementedError().
class AtomicMemoryOpInterface {
 public:
  // Default destructor.
  virtual ~AtomicMemoryOpInterface() = default;

  enum class Operation {
    // Load linked and store conditional combines to implement an atomic op.
    kLoadLinked,
    kStoreConditional,
    // Atomically swap MEM[address] and db->Get<T>(0).
    kAtomicSwap,
    // Individual atomic operations implemented in/near memory. More may be
    // added in the future. The exact implementation of the operations is up
    // to the implementation. One example is RiscV atomic memory operations
    // that operatle like the following:
    //
    //   tmp = MEM[address]
    //   MEM[address] = op(MEM[ADDRESS], db->Get<T>(0))
    //   db->Set<T>(tmp)
    //
    // where T corresponds to the integer size corresponding to the element
    // size.
    kAtomicAdd,
    kAtomicSub,
    kAtomicAnd,
    kAtomicOr,
    kAtomicXor,
    kAtomicMax,
    kAtomicMaxu,
    kAtomicMin,
    kAtomicMinu,
  };

  // Perform atomic memory operation 'op' at location 'address', using the value
  // in 'db', and returning the result in DataBuffer 'db'. The element size for
  // is inferred from the size of 'db' and is the same for both the source value
  // as well as the result of the atomic operation. The size of the data buffer
  // db->size<uint8_t>() must be equal to sizeof(T) for some integer type, i.e.,
  // 1, 2, 4, or 8.
  virtual absl::Status PerformMemoryOp(uint64_t address, Operation op,
                                       DataBuffer* db, Instruction* inst,
                                       ReferenceCount* context) = 0;
};

}  // namespace util
}  // namespace sim
}  // namespace mpact

#endif  // MPACT_SIM_UTIL_MEMORY_MEMORY_INTERFACE_H_
