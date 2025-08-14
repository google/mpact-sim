// Copyright 2024 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "mpact/sim/util/memory/memory_use_profiler.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <ostream>
#include <utility>

#include "absl/container/btree_map.h"
#include "absl/log/log.h"
#include "absl/numeric/bits.h"
#include "absl/strings/str_format.h"
#include "mpact/sim/util/memory/memory_interface.h"
#include "mpact/sim/util/memory/memory_watcher.h"
#include "mpact/sim/util/memory/tagged_memory_interface.h"

namespace mpact::sim::util {

namespace internal {

MemoryUseTracker::~MemoryUseTracker() {
  for (auto& [unused, bits] : memory_use_map_) {
    delete[] bits;
  }
  memory_use_map_.clear();
}

// Static helper function.
static inline void MarkUsedBits(uint64_t byte_offset, int mask, uint8_t* bits) {
  // Shift right by two, so that every byte offset becomes a word offset.
  uint64_t word_offset = byte_offset >> 2;
  // Byte offs
  uint64_t byte_index = word_offset >> 3;
  int bit_index = word_offset & 0b111;

  bits[byte_index] |= (mask << bit_index);
}

void MemoryUseTracker::MarkUsed(uint64_t address, int size) {
  // The profiling is done on a word boundary, so word or smaller is marked by
  // a single bit, double words by 2 bits.
  if (size > 8) {
    LOG(INFO) << "MemoryUseTracker::MarkUsed: not profiling accesses > 8 bytes";
    return;
  }
  size = std::min(size, 4);
  uint8_t mask = size > 4 ? 0b11 : 0b1;
  if ((address >= last_start_) && (address + size - 1 <= last_end_)) {
    return MarkUsedBits(address - last_start_, mask, last_used_);
  }
  auto it = memory_use_map_.find(
      MemoryWatcher::AddressRange{address, address + size - 1});
  if (it == memory_use_map_.end()) {
    // Compute new base and top addresses.
    uint64_t start = address & ~kBaseMask;
    uint64_t end = start + kSegmentSize - 1;
    auto* bits = new uint8_t[kBitsSize];
    std::memset(bits, 0, kBitsSize);

    it = memory_use_map_
             .insert(
                 std::make_pair(MemoryWatcher::AddressRange{start, end}, bits))
             .first;
  }
  last_start_ = it->first.start;
  last_end_ = it->first.end;
  last_used_ = it->second;
  MarkUsedBits(address - last_start_, mask, it->second);
}

// Write out ranges of words that have been used.
void MemoryUseTracker::WriteUseProfile(std::ostream& os) const {
  // Current range info.
  uint64_t range_start = 0;
  uint64_t range_end = 0;
  bool range_started = false;
  for (auto const& [range, bits] : memory_use_map_) {
    auto base = range.start;
    int byte_index = 0;
    uint8_t byte = bits[byte_index];
    while (byte_index < kBitsSize) {
      if (range_started) {
        // If we have a range started and the accesses are contiguous, increment
        // the byte index and continue.
        if (byte == 0xff) {
          byte_index++;
          if (byte_index < kBitsSize) byte = bits[byte_index];
          continue;
        }
        // Compute the end of the current range by counting the number of
        // consecutive ones starting from the lsb.
        int bit_indx = absl::countr_one(byte) - 1;
        range_end = base + (byte_index * 8 + bit_indx) * kGranularity;
        // Output range.
        os << absl::StrFormat("0x%llx,0x%llx,%llu\n", range_start, range_end,
                              range_end - range_start + 4);
        // Clear those bits from the current byte, then we start looking for
        // a new range.
        byte &= ~((1 << (bit_indx + 1)) - 1);
        range_started = false;
      }
      // If we are here, range_started is false.
      if (byte == 0) {
        byte_index++;
        if (byte_index < kBitsSize) byte = bits[byte_index];
        continue;
      }
      // At this point byte != 0, and we need to start a new range.
      int bit_indx = absl::countr_zero(byte);
      range_start = base + (byte_index * 8 + bit_indx) * kGranularity;
      // Set the low bits in the current byte so that we can determine the end
      // of this range, in case it is contained in this byte.
      byte |= ((1 << bit_indx) - 1);
      range_started = true;
    }
  }
}

}  // namespace internal

MemoryUseProfiler::MemoryUseProfiler() : MemoryUseProfiler(nullptr) {}

MemoryUseProfiler::MemoryUseProfiler(MemoryInterface* memory)
    : memory_(memory) {}

void MemoryUseProfiler::Load(uint64_t address, DataBuffer* db,
                             Instruction* inst, ReferenceCount* context) {
  if (is_enabled_) tracker_.MarkUsed(address, db->size<uint8_t>());
  if (memory_) memory_->Load(address, db, inst, context);
}

void MemoryUseProfiler::Load(DataBuffer* address_db, DataBuffer* mask_db,
                             int el_size, DataBuffer* db, Instruction* inst,
                             ReferenceCount* context) {
  if (is_enabled_) {
    for (int i = 0; i < address_db->size<uint64_t>(); ++i) {
      if (mask_db->Get<uint8_t>(i)) {
        tracker_.MarkUsed(address_db->Get<uint64_t>(i), el_size);
      }
    }
  }
  if (memory_) memory_->Load(address_db, mask_db, el_size, db, inst, context);
}

void MemoryUseProfiler::Store(uint64_t address, DataBuffer* db) {
  if (is_enabled_) tracker_.MarkUsed(address, db->size<uint8_t>());
  if (memory_) memory_->Store(address, db);
}

void MemoryUseProfiler::Store(DataBuffer* address_db, DataBuffer* mask_db,
                              int el_size, DataBuffer* db) {
  if (is_enabled_) {
    for (int i = 0; i < address_db->size<uint64_t>(); ++i) {
      if (mask_db->Get<uint8_t>(i)) {
        tracker_.MarkUsed(address_db->Get<uint64_t>(i), el_size);
      }
    }
  }
  if (memory_) memory_->Store(address_db, mask_db, el_size, db);
}

TaggedMemoryUseProfiler::TaggedMemoryUseProfiler()
    : TaggedMemoryUseProfiler(nullptr) {}

TaggedMemoryUseProfiler::TaggedMemoryUseProfiler(
    TaggedMemoryInterface* tagged_memory)
    : tagged_memory_(tagged_memory) {}

void TaggedMemoryUseProfiler::Load(uint64_t address, DataBuffer* db,
                                   Instruction* inst, ReferenceCount* context) {
  if (is_enabled_) tracker_.MarkUsed(address, db->size<uint8_t>());
  if (tagged_memory_) tagged_memory_->Load(address, db, inst, context);
}

void TaggedMemoryUseProfiler::Load(DataBuffer* address_db, DataBuffer* mask_db,
                                   int el_size, DataBuffer* db,
                                   Instruction* inst, ReferenceCount* context) {
  if (is_enabled_) {
    for (int i = 0; i < address_db->size<uint64_t>(); ++i) {
      if (mask_db->Get<uint8_t>(i)) {
        tracker_.MarkUsed(address_db->Get<uint64_t>(i), el_size);
      }
    }
  }
  if (tagged_memory_)
    tagged_memory_->Load(address_db, mask_db, el_size, db, inst, context);
}

void TaggedMemoryUseProfiler::Load(uint64_t address, DataBuffer* db,
                                   DataBuffer* tags, Instruction* inst,
                                   ReferenceCount* context) {
  if ((db != nullptr) && is_enabled_)
    tracker_.MarkUsed(address, db->size<uint8_t>());
  if (tagged_memory_) tagged_memory_->Load(address, db, tags, inst, context);
}

void TaggedMemoryUseProfiler::Store(uint64_t address, DataBuffer* db) {
  if (is_enabled_) tracker_.MarkUsed(address, db->size<uint8_t>());
  if (tagged_memory_) tagged_memory_->Store(address, db);
}

void TaggedMemoryUseProfiler::Store(DataBuffer* address_db, DataBuffer* mask_db,
                                    int el_size, DataBuffer* db) {
  if (is_enabled_) {
    for (int i = 0; i < address_db->size<uint64_t>(); ++i) {
      if (mask_db->Get<uint8_t>(i)) {
        tracker_.MarkUsed(address_db->Get<uint64_t>(i), el_size);
      }
    }
  }
  if (tagged_memory_) tagged_memory_->Store(address_db, mask_db, el_size, db);
}

void TaggedMemoryUseProfiler::Store(uint64_t address, DataBuffer* db,
                                    DataBuffer* tags) {
  if ((db != nullptr) && is_enabled_)
    tracker_.MarkUsed(address, db->size<uint8_t>());
  if (tagged_memory_) tagged_memory_->Store(address, db, tags);
}

}  // namespace mpact::sim::util
