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

#ifndef MPACT_SIM_UTIL_MEMORY_MEMORY_ROUTER_H_
#define MPACT_SIM_UTIL_MEMORY_MEMORY_ROUTER_H_

#include <cstdint>
#include <string>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "mpact/sim/generic/data_buffer.h"
#include "mpact/sim/generic/instruction.h"
#include "mpact/sim/generic/ref_count.h"
#include "mpact/sim/util/memory/memory_interface.h"
#include "mpact/sim/util/memory/tagged_memory_interface.h"

// This file defines a general memory router class that connects multiple
// initiators to one or more memory targets according to the memory addresses
// used in the load/store/memory op calls. The class uses instances of the
// SingleInitiatorRouter class to achieve this.

namespace mpact {
namespace sim {
namespace util {

using ::mpact::sim::generic::DataBuffer;
using ::mpact::sim::generic::Instruction;
using ::mpact::sim::generic::ReferenceCount;

class SingleInitiatorRouter;

class MemoryRouter {
 public:
  // Convenient map types shorthand.
  using InitiatorMap = absl::flat_hash_map<std::string, SingleInitiatorRouter*>;
  template <typename Interface>
  using TargetMap = absl::flat_hash_map<std::string, Interface*>;

  MemoryRouter();
  MemoryRouter(const MemoryRouter&) = delete;
  MemoryRouter& operator=(const MemoryRouter&) = delete;
  ~MemoryRouter();

  // The following three methods add initiators with a given name. Depending
  // on the method that is called, the returned interface differs. However, the
  // underlying object is the same type for each. Thus if the same name is
  // passed in two different calls, the object is the same instance of
  // SingleInitiatorRouter, but the returned interfaces differ.

  // Add an initiator with name 'name', returning pointer to MemoryInterface.
  MemoryInterface* AddMemoryInitiator(const std::string& name);
  // Add an initiator with name 'name', returning pointer to
  // TaggedMemoryInterface.
  TaggedMemoryInterface* AddTaggedInitiator(const std::string& name);
  // Add an initiator with name 'name', returning pointer to MemoryInterface.
  AtomicMemoryOpInterface* AddAtomicInitiator(const std::string& name);

  // The following three methods add target memory interfaces with the given
  // name and type. Two different interfaces may not use the same name.

  // Add 'memory' interface with name 'name'.
  absl::Status AddTarget(const std::string& name, MemoryInterface* memory);
  // Add 'tagged_memory' interface with name 'name'.
  absl::Status AddTarget(const std::string& name,
                         TaggedMemoryInterface* tagged_memory);
  // Add 'atomic_memory' interface with name 'name'.
  absl::Status AddTarget(const std::string& name,
                         AtomicMemoryOpInterface* atomic_memory);

  // Map a target to an initiator for the given address range (inclusive).
  absl::Status AddMapping(const std::string& initiator_name,
                          const std::string& target_name, uint64_t base,
                          uint64_t top);

 private:
  // Containers of initiators and target interfaces.
  InitiatorMap initiators_;
  absl::flat_hash_set<std::string> target_names_;
  TargetMap<MemoryInterface> memory_targets_;
  TargetMap<TaggedMemoryInterface> tagged_targets_;
  TargetMap<AtomicMemoryOpInterface> atomic_targets_;
};

}  // namespace util
}  // namespace sim
}  // namespace mpact

#endif  // MPACT_SIM_UTIL_MEMORY_MEMORY_ROUTER_H_
