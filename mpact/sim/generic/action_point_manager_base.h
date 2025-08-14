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

#ifndef MPACT_SIM_GENERIC_ACTION_POINT_MANAGER_BASE_H_
#define MPACT_SIM_GENERIC_ACTION_POINT_MANAGER_BASE_H_

#include <cstdint>
#include <utility>

#include "absl/container/btree_map.h"
#include "absl/functional/any_invocable.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"

namespace mpact::sim::generic {

// This file defines the ActionPointManager base class which provides the low
// level functionality required to implement breakpoints and other 'actions'
// that need to be performed when an instruction executes. It relies on the
// presence of a software breakpoint instruction in the program to stop
// execution. A handler will check whether an executed software breakpoint
// instruction is due to an action point, or if it is part of a program. If it
// is an action point, the handler will call into this class to execute all
// enabled action points at that address.
//
// The ebreak is only written to memory if there is at least one enabled action
// and is replaced with the original instruction when all actions are disabled.

// The ActionPointManagerBase class depends on an interface to read/write
// appropriate software interrupt instructions to memory, e.g., ebreak for the
// RiscV ISA. This class defines the interface to that class. Instances of this
// class must be able to replace the instruction at the given address with a
// breakpoint instruction, save the original instruction, and then restore it
// when requested. The ActionPointMemoryInterface must also invalidate any
// cached decoding of the instruction at the given address, so the next use of
// the instruction will be decoded with the most recent version stored to
// memory. Each ISA should derive an appropriate implementation of this class.
class ActionPointMemoryInterface {
 public:
  virtual ~ActionPointMemoryInterface() = default;
  // This restores the original instruction in memory, and allows it to be
  // decoded and executed, provided the address is an action point. If not,
  // no action is taken.
  virtual absl::Status WriteOriginalInstruction(uint64_t address) = 0;
  // Store breakpoint instruction, provided the address is an action point.
  // Otherwise no action is taken.
  virtual absl::Status WriteBreakpointInstruction(uint64_t address) = 0;
};

class ActionPointManagerBase {
 public:
  // Function type for actions - void function that takes a 64 bit address.
  using ActionFcn = absl::AnyInvocable<void(uint64_t address, int id)>;

  // The constructor takes an instance of the ActionPointMemoryInterface class.
  ActionPointManagerBase(ActionPointMemoryInterface* ap_mem_interface);
  virtual ~ActionPointManagerBase();

  // Returns true if the given address has an action point, regardless of
  // whether it is active or not.
  bool HasActionPoint(uint64_t address) const;
  // Set action_fcn to be executed when reaching address. There may be multiple
  // actions on an instruction so an id is returned on successfully setting an
  // action point.
  absl::StatusOr<int> SetAction(uint64_t address, ActionFcn action_fcn);
  // Remove the action point with the given id.
  absl::Status ClearAction(uint64_t address, int id);
  // Enable/Disable the action point with the given id.
  absl::Status EnableAction(uint64_t address, int id);
  absl::Status DisableAction(uint64_t address, int id);
  // Return true if there is at least one enabled 'action' at this address.
  bool IsActionPointActive(uint64_t address) const;
  // Return true if the given 'action' is enabled.
  bool IsActionEnabled(uint64_t address, int id) const;
  // Remove all action points.
  void ClearAllActionPoints();
  // Perform all enabled actions for the given address. If address is not an
  // action point, not action is performed.
  void PerformActions(uint64_t address);

  // Accessor.
  ActionPointMemoryInterface* ap_memory_interface() const {
    return ap_memory_interface_;
  }

 private:
  // Struct to store information about an individual 'action'. It contains a
  // callable and a flag that indicates whether the action is enabled or not.
  struct ActionInfo {
    ActionFcn action_fcn;
    bool is_enabled;
    ActionInfo(ActionFcn action_fcn, bool is_enabled)
        : action_fcn(::std::move(action_fcn)), is_enabled(is_enabled) {}
  };

  struct ActionPointInfo {
    // Address of the action point.
    uint64_t address;
    // Id to use for the next added 'action'.
    int next_id = 0;
    // Number of 'actions' that are enabled.
    int num_active = 0;
    // Map from id to action function struct.
    absl::btree_map<int, ActionInfo*> action_map;
    ActionPointInfo(uint64_t address) : address(address) {}
  };

  // Interface to program memory.
  ActionPointMemoryInterface* ap_memory_interface_;
  // Map from address to action info struct.
  absl::btree_map<uint64_t, ActionPointInfo*> action_point_map_;
};

}  // namespace mpact::sim::generic

#endif  // MPACT_SIM_GENERIC_ACTION_POINT_MANAGER_BASE_H_
