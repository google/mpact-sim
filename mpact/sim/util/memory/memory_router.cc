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

#include "mpact/sim/util/memory/memory_router.h"

#include <cstdint>
#include <string>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "mpact/sim/util/memory/memory_interface.h"
#include "mpact/sim/util/memory/single_initiator_router.h"
#include "mpact/sim/util/memory/tagged_memory_interface.h"

namespace mpact {
namespace sim {
namespace util {

MemoryRouter::MemoryRouter() {}

MemoryRouter::~MemoryRouter() {
  for (auto &[unused, initiator] : initiators_) delete initiator;

  initiators_.clear();
}

// Templated helper method used in implementing the three methods to add named
// initiators to the router (one for each type of initiator interface).
template <typename Interface>
Interface *AddInitiator(MemoryRouter::InitiatorMap &initiators,
                        const std::string &name) {
  // See if it already exists.
  auto it = initiators.find(name);
  if (it != initiators.end()) {
    return static_cast<Interface *>(it->second);
  }
  auto initiator = new SingleInitiatorRouter(name);
  initiators.emplace(std::string(name), initiator);
  return static_cast<Interface *>(initiator);
}

MemoryInterface *MemoryRouter::AddMemoryInitiator(const std::string &name) {
  return AddInitiator<MemoryInterface>(initiators_, name);
}

TaggedMemoryInterface *MemoryRouter::AddTaggedInitiator(
    const std::string &name) {
  return AddInitiator<TaggedMemoryInterface>(initiators_, name);
}

AtomicMemoryOpInterface *MemoryRouter::AddAtomicInitiator(
    const std::string &name) {
  return AddInitiator<AtomicMemoryOpInterface>(initiators_, name);
}

// Templated helper method used in implementing the next three methods that
// add named targets to the router (one for each type of target interface).
template <typename Interface>
absl::Status AddTarget(MemoryRouter::TargetMap<Interface> &target_interface_map,
                       absl::flat_hash_set<std::string> &target_names,
                       const std::string &name, Interface *memory) {
  // Only one instance of each target name can exist.
  if (target_names.contains(name)) {
    return absl::AlreadyExistsError(
        absl::StrCat("Target: ", name, " already exists"));
  }
  target_names.insert(name);
  target_interface_map.emplace(std::string(name), memory);
  return absl::OkStatus();
}

absl::Status MemoryRouter::AddTarget(const std::string &name,
                                     MemoryInterface *memory) {
  return AddTarget<MemoryInterface>(memory_targets_, target_names_, name,
                                    memory);
}

absl::Status MemoryRouter::AddTarget(const std::string &name,
                                     TaggedMemoryInterface *tagged_memory) {
  return AddTarget<TaggedMemoryInterface>(tagged_targets_, target_names_, name,
                                          tagged_memory);
}

absl::Status MemoryRouter::AddTarget(const std::string &name,
                                     AtomicMemoryOpInterface *atomic_memory) {
  return AddTarget<AtomicMemoryOpInterface>(atomic_targets_, target_names_,
                                            name, atomic_memory);
}

// Add a mapping between 'initiator_name' and 'target_name' for the given
// address range (inclusive).
absl::Status MemoryRouter::AddMapping(const std::string &initiator_name,
                                      const std::string &target_name,
                                      uint64_t base, uint64_t top) {
  // Return error if the initiator doesn't exist.
  auto it = initiators_.find(initiator_name);
  if (it == initiators_.end()) {
    return absl::NotFoundError(
        absl::StrCat("Initiator: ", initiator_name, " not found"));
  }
  auto *initiator = it->second;

  // Check each type of target and add the one found to the initiator with the
  // given address range.

  auto memory_it = memory_targets_.find(target_name);
  if (memory_it != memory_targets_.end()) {
    return initiator->AddTarget(memory_it->second, base, top);
  }
  auto tagged_it = tagged_targets_.find(target_name);
  if (tagged_it != tagged_targets_.end()) {
    return initiator->AddTarget(tagged_it->second, base, top);
  }
  auto atomic_it = atomic_targets_.find(target_name);
  if (atomic_it != atomic_targets_.end()) {
    return initiator->AddTarget(atomic_it->second, base, top);
  }
  // Return error if there is no such target.
  return absl::NotFoundError(
      absl::StrCat("Target: ", target_name, " not found"));
}

}  // namespace util
}  // namespace sim
}  // namespace mpact
