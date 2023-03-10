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

#include "mpact/sim/util/memory/memory_watcher.h"

#include <utility>

#include "absl/strings/str_cat.h"

namespace mpact {
namespace sim {
namespace util {

MemoryWatcher::MemoryWatcher(MemoryInterface *memory) : memory_(memory) {}

// Methods to insert store and load watch callbacks. The methods check that
// the address range is well formed (start <= end), and that there is no
// overlapping range in the btree map.

absl::Status MemoryWatcher::SetStoreWatchCallback(const AddressRange &range,
                                                  Callback callback) {
  if (range.start > range.end) {
    return absl::InternalError(absl::StrCat("Illegal store watch range: start ",
                                            absl::Hex(range.start), " > end ",
                                            absl::Hex(range.end)));
  }
  if (st_watch_actions_.contains(range)) {
    return absl::InternalError(absl::StrCat(
        "Store watch range [", absl::Hex(range.start), ", ",
        absl::Hex(range.end), "] overlaps with an existing watch range"));
  }
  st_watch_actions_.insert({range, std::move(callback)});
  return absl::OkStatus();
}

absl::Status MemoryWatcher::ClearStoreWatchCallback(uint64_t address) {
  auto ptr = st_watch_actions_.find(AddressRange(address));
  if (ptr == st_watch_actions_.end()) {
    return absl::NotFoundError(absl::StrCat(
        "ClearStoreWatchCallback: Error: No range found that contains: ",
        absl::Hex(address)));
  }
  return absl::OkStatus();
}

absl::Status MemoryWatcher::SetLoadWatchCallback(const AddressRange &range,
                                                 Callback callback) {
  if (range.start > range.end) {
    return absl::InternalError(absl::StrCat("Illegal store watch range: start ",
                                            absl::Hex(range.start), " > end ",
                                            absl::Hex(range.end)));
  }
  if (ld_watch_actions_.contains(range)) {
    return absl::InternalError(absl::StrCat(
        "Load watch range [", absl::Hex(range.start), ", ",
        absl::Hex(range.end), "] overlaps with an existing watch range"));
  }
  ld_watch_actions_.insert({range, std::move(callback)});
  return absl::OkStatus();
}

absl::Status MemoryWatcher::ClearLoadWatchCallback(uint64_t address) {
  auto ptr = ld_watch_actions_.find(AddressRange(address));
  if (ptr == ld_watch_actions_.end()) {
    return absl::NotFoundError(absl::StrCat(
        "ClearLoadWatchCallback: Error: No range found that contains: ",
        absl::Hex(address)));
  }
  return absl::OkStatus();
}

// Each of the overridden methods for Loads and Stores checks if the address
// is in a range that is being watched. If it is, the load/store action callback
// is called before/after the load/store is forwarded to the interface.

// Single address.
void MemoryWatcher::Load(uint64_t address, DataBuffer *db, Instruction *inst,
                         ReferenceCount *context) {
  if (!ld_watch_actions_.empty()) {
    auto [first_match, last] = ld_watch_actions_.equal_range(
        AddressRange(address, address + db->size<uint8_t>() - 1));
    while (first_match != last) {
      first_match->second(address, db->size<uint8_t>());
      first_match++;
    }
  }
  memory_->Load(address, db, inst, context);
}

// Gather load (multiple addresses and a mask vector).
void MemoryWatcher::Load(DataBuffer *address_db, DataBuffer *mask_db,
                         int el_size, DataBuffer *db, Instruction *inst,
                         ReferenceCount *context) {
  if (!ld_watch_actions_.empty()) {
    int num_entries = mask_db->size<bool>();
    auto addresses = address_db->Get<uint64_t>();
    auto masks = mask_db->Get<bool>();
    for (int i = 0; i < num_entries; i++) {
      // Need to check each unmasked address.
      if (!masks[i]) continue;
      uint64_t address = addresses[i];
      auto [first_match, last] = ld_watch_actions_.equal_range(
          AddressRange(address, address + el_size - 1));
      while (first_match != last) {
        first_match->second(address, el_size);
        first_match++;
      }
    }
  }
  memory_->Load(address_db, mask_db, el_size, db, inst, context);
}

// Single address store.
void MemoryWatcher::Store(uint64_t address, DataBuffer *db) {
  memory_->Store(address, db);
  if (!st_watch_actions_.empty()) {
    auto [first_match, last] = st_watch_actions_.equal_range(
        AddressRange(address, address + db->size<uint8_t>() - 1));
    while (first_match != last) {
      first_match->second(address, db->size<uint8_t>());
      first_match++;
    }
  }
}

// Scatter store (multiple addresses and a mask vector).
void MemoryWatcher::Store(DataBuffer *address_db, DataBuffer *mask_db,
                          int el_size, DataBuffer *db) {
  memory_->Store(address_db, mask_db, el_size, db);
  if (!st_watch_actions_.empty()) {
    int num_entries = mask_db->size<bool>();
    auto addresses = address_db->Get<uint64_t>();
    auto masks = mask_db->Get<bool>();
    for (int i = 0; i < num_entries; i++) {
      // Need to check each unmasked address.
      if (!masks[i]) continue;
      uint64_t address = addresses[i];
      auto [first_match, last] = st_watch_actions_.equal_range(
          AddressRange(address, address + el_size - 1));
      while (first_match != last) {
        first_match->second(address, el_size);
        first_match++;
      }
    }
  }
}

}  // namespace util
}  // namespace sim
}  // namespace mpact
