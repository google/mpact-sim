/*
 * Copyright 2024 Google LLC
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef MPACT_SIM_UTIL_MEMORY_MEMORY_USE_PROFILER_H_
#define MPACT_SIM_UTIL_MEMORY_MEMORY_USE_PROFILER_H_

#include <cstdint>
#include <ostream>

#include "absl/container/btree_map.h"
#include "mpact/sim/generic/data_buffer.h"
#include "mpact/sim/generic/instruction.h"
#include "mpact/sim/generic/ref_count.h"
#include "mpact/sim/util/memory/memory_interface.h"
#include "mpact/sim/util/memory/memory_watcher.h"
#include "mpact/sim/util/memory/tagged_memory_interface.h"

namespace mpact::sim::util {

using generic::DataBuffer;
using generic::Instruction;
using generic::ReferenceCount;
using util::MemoryInterface;
using util::TaggedMemoryInterface;

namespace internal {

// This class is used to track the use of word addresses. It dynamically
// allocates tracking memory as needed, and marks a bit for each word that is
// accessed.
class MemoryUseTracker {
 public:
  MemoryUseTracker() = default;
  ~MemoryUseTracker();

  // Memory use is tracked on word granularity.
  static constexpr int kGranularity = sizeof(uint32_t);
  // The segment size is the size of the address range for each segment.
  static constexpr int kSegmentSize = 128 * 1024;
  static constexpr int kBaseMask = kSegmentSize - 1;
  // Size of each 'use' bit store.
  static constexpr int kBitsSize = kSegmentSize / (kGranularity * 8);
  void MarkUsed(uint64_t address, int size);
  void WriteUseProfile(std::ostream &os) const;

 private:
  uint64_t last_start_ = 0;
  uint64_t last_end_ = 0;
  uint8_t *last_used_ = nullptr;
  absl::btree_map<MemoryWatcher::AddressRange, uint8_t *,
                  MemoryWatcher::AddressRangeLess>
      memory_use_map_;
};

}  // namespace internal

// Use profiler for the MemoryInterface.
class MemoryUseProfiler : public MemoryInterface {
 public:
  // The default constructor does not set up memory forwarding.
  MemoryUseProfiler();
  explicit MemoryUseProfiler(MemoryInterface *memory);
  ~MemoryUseProfiler() override = default;

  // Inherited from memory interfaces.
  void Load(uint64_t address, DataBuffer *db, Instruction *inst,
            ReferenceCount *context) override;
  void Load(DataBuffer *address_db, DataBuffer *mask_db, int el_size,
            DataBuffer *db, Instruction *inst,
            ReferenceCount *context) override;
  void Store(uint64_t address, DataBuffer *db) override;
  void Store(DataBuffer *address_db, DataBuffer *mask_db, int el_size,
             DataBuffer *db) override;

  void WriteProfile(std::ostream &os) const { tracker_.WriteUseProfile(os); }

  // Accessor.
  void set_is_enabled(bool is_enabled) { is_enabled_ = is_enabled; }
  bool is_enabled() const { return is_enabled_; }

 private:
  bool is_enabled_ = false;
  MemoryInterface *memory_;
  internal::MemoryUseTracker tracker_;
};

// Use profiler for the TaggedMemoryInterface.
class TaggedMemoryUseProfiler : public TaggedMemoryInterface {
 public:
  // The default constructor does not set up memory forwarding.
  TaggedMemoryUseProfiler();
  explicit TaggedMemoryUseProfiler(TaggedMemoryInterface *tagged_memory);
  ~TaggedMemoryUseProfiler() override = default;

  // Inherited from memory interfaces.
  void Load(uint64_t address, DataBuffer *db, Instruction *inst,
            ReferenceCount *context) override;
  void Load(DataBuffer *address_db, DataBuffer *mask_db, int el_size,
            DataBuffer *db, Instruction *inst,
            ReferenceCount *context) override;
  void Load(uint64_t address, DataBuffer *db, DataBuffer *tags,
            Instruction *inst, ReferenceCount *context) override;
  void Store(uint64_t address, DataBuffer *db) override;
  void Store(DataBuffer *address_db, DataBuffer *mask_db, int el_size,
             DataBuffer *db) override;
  void Store(uint64_t address, DataBuffer *db, DataBuffer *tags) override;

  void WriteProfile(std::ostream &os) const { tracker_.WriteUseProfile(os); }

  // Accessor.
  void set_is_enabled(bool is_enabled) { is_enabled_ = is_enabled; }

 private:
  bool is_enabled_ = false;
  TaggedMemoryInterface *tagged_memory_;
  internal::MemoryUseTracker tracker_;
};

}  // namespace mpact::sim::util

#endif  // MPACT_SIM_UTIL_MEMORY_MEMORY_USE_PROFILER_H_
