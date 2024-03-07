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

#ifndef MPACT_SIM_UTIL_MEMORY_TAGGED_MEMORY_WATCHER_H_
#define MPACT_SIM_UTIL_MEMORY_TAGGED_MEMORY_WATCHER_H_

#include <cstdint>

#include "absl/container/btree_map.h"
#include "absl/functional/any_invocable.h"
#include "absl/status/status.h"
#include "mpact/sim/generic/data_buffer.h"
#include "mpact/sim/generic/instruction.h"
#include "mpact/sim/generic/ref_count.h"
#include "mpact/sim/util/memory/tagged_memory_interface.h"

// This file declares a class that is used to create data watchpoints. Since it
// implements the tagged memory interface, it can easily be inserted between a
// tagged memory requestor and the memory.

namespace mpact {
namespace sim {
namespace util {

using ::mpact::sim::generic::DataBuffer;
using ::mpact::sim::generic::Instruction;
using ::mpact::sim::generic::ReferenceCount;

class TaggedMemoryWatcher : public TaggedMemoryInterface {
 public:
  // Address range struct used as key in maps from range to callback function.
  struct AddressRange {
    uint64_t start;
    uint64_t end;
    explicit AddressRange(uint64_t address) : start(address), end(address) {}
    AddressRange(uint64_t start_address, uint64_t end_address)
        : start(start_address), end(end_address) {}
  };

  // Callback types. The load callback does not pass the load data, as that is
  // not available in the memory Load call. The call passes the address and the
  // data size.
  using Callback = absl::AnyInvocable<void(uint64_t, int)>;

  // Constructor - default constructor is disabled. Use default destructor.
  explicit TaggedMemoryWatcher(TaggedMemoryInterface *memory);
  TaggedMemoryWatcher() = delete;
  TaggedMemoryWatcher(const TaggedMemoryWatcher &) =
      delete;  // no copy constructor.
  TaggedMemoryWatcher &operator=(const TaggedMemoryWatcher &) =
      delete;  // no assignment.
  ~TaggedMemoryWatcher() override = default;

  // Methods to set/clear watch ranges. No new store (load) range can overlap an
  // existing store (load) range. The address range has to have the start < end.
  // Since there cannot be any overlapping ranges, it is only necessary to
  // specify a single address for the clear call, as it will map to the range
  // that contains it.
  absl::Status SetStoreWatchCallback(const AddressRange &range,
                                     Callback callback);
  absl::Status ClearStoreWatchCallback(uint64_t address);
  absl::Status SetLoadWatchCallback(const AddressRange &range,
                                    Callback callback);
  absl::Status ClearLoadWatchCallback(uint64_t address);

  // The memory interface methods.
  void Load(uint64_t address, DataBuffer *db, Instruction *inst,
            ReferenceCount *context) override;
  void Load(DataBuffer *address_db, DataBuffer *mask_db, int el_size,
            DataBuffer *db, Instruction *inst,
            ReferenceCount *context) override;
  void Store(uint64_t address, DataBuffer *db) override;
  void Store(DataBuffer *address_db, DataBuffer *mask_db, int el_size,
             DataBuffer *db) override;
  void Load(uint64_t address, DataBuffer *db, DataBuffer *tags,
            Instruction *inst, ReferenceCount *context) override;
  void Store(uint64_t address, DataBuffer *db, DataBuffer *tags) override;

 private:
  // Comparator used to compare two address ranges. A range is less than another
  // if they do (a) not overlap and (b) the addresses of the first are less than
  // the other.
  struct AddressRangeLess {
    constexpr bool operator()(const AddressRange &lhs,
                              const AddressRange &rhs) const {
      return lhs.end < rhs.start;
    }
  };
  // The memory interface to forward the loads/stores to.
  TaggedMemoryInterface *memory_;
  absl::btree_multimap<AddressRange, Callback, AddressRangeLess>
      ld_watch_actions_;
  absl::btree_multimap<AddressRange, Callback, AddressRangeLess>
      st_watch_actions_;
};

}  // namespace util
}  // namespace sim
}  // namespace mpact

#endif  // MPACT_SIM_UTIL_MEMORY_TAGGED_MEMORY_WATCHER_H_
