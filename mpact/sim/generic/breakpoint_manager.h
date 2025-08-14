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

#ifndef MPACT_SIM_GENERIC_BREAKPOINT_MANAGER_H_
#define MPACT_SIM_GENERIC_BREAKPOINT_MANAGER_H_

#include <cstdint>

#include "absl/container/btree_map.h"
#include "absl/functional/any_invocable.h"
#include "absl/status/status.h"
#include "mpact/sim/generic/action_point_manager_base.h"
#include "mpact/sim/generic/core_debug_interface.h"

// This file defines a generic class for handling breakpoints. It
// uses the ActionPoint manager to add breakpoint functionality by having
// actions that request software breakpoint halts.
namespace mpact {
namespace sim {
namespace generic {

using HaltReason = ::mpact::sim::generic::CoreDebugInterface::HaltReason;

class BreakpointManager {
 public:
  // Define a function type to use for the breakpoint action to call for a halt.
  using RequestHaltFunction = absl::AnyInvocable<void()>;

  BreakpointManager(ActionPointManagerBase* action_point_manager,
                    RequestHaltFunction req_halt_function);
  ~BreakpointManager();

  bool HasBreakpoint(uint64_t address);
  // Set/Clear breakpoint at address.
  absl::Status SetBreakpoint(uint64_t address);
  absl::Status ClearBreakpoint(uint64_t address);
  // Disable/Enable breakpoint at address. These act as Set/Clear, but the
  // breakpoint information isn't deleted.
  absl::Status DisableBreakpoint(uint64_t address);
  absl::Status EnableBreakpoint(uint64_t address);
  // Clear all breakpoints.
  void ClearAllBreakpoints();
  // Is the address an active breakpoint?
  bool IsBreakpoint(uint64_t address) const;

  // Accessor.
  ActionPointManagerBase* action_point_manager() const {
    return action_point_manager_;
  }

 private:
  // Structure keeping track of breakpoint information.
  struct BreakpointInfo {
    uint64_t address;
    int id;
    bool is_active;
  };

  // Function implementing the breakpoint 'action'.
  RequestHaltFunction req_halt_function_;
  void DoBreakpointAction(uint64_t, int);

  ActionPointManagerBase* action_point_manager_;
  absl::btree_map<uint64_t, BreakpointInfo*> breakpoint_map_;
};

}  // namespace generic
}  // namespace sim
}  // namespace mpact

#endif  // MPACT_SIM_GENERIC_BREAKPOINT_MANAGER_H_
