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

#include "mpact/sim/generic/action_point_manager_base.h"

#include <cstdint>
#include <utility>

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"

namespace mpact::sim::generic {

ActionPointManagerBase::ActionPointManagerBase(
    ActionPointMemoryInterface *ap_mem_interface)
    : ap_memory_interface_(ap_mem_interface) {}

ActionPointManagerBase::~ActionPointManagerBase() {
  for (auto &[unused, ap_ptr] : action_point_map_) {
    for (auto &[unused, ai_ptr] : ap_ptr->action_map) {
      ai_ptr->action_fcn = nullptr;
      delete ai_ptr;
    }
    ap_ptr->action_map.clear();
    delete ap_ptr;
  }
  action_point_map_.clear();
}

bool ActionPointManagerBase::HasActionPoint(uint64_t address) const {
  return action_point_map_.contains(address);
}

absl::StatusOr<int> ActionPointManagerBase::SetAction(uint64_t address,
                                                      ActionFcn action_fcn) {
  auto it = action_point_map_.find(address);
  ActionPointInfo *ap = nullptr;
  if (it == action_point_map_.end()) {
    // If the action point doesn't exist, create a new one.
    // First set the breakpoint instruction.
    auto status = ap_memory_interface_->WriteBreakpointInstruction(address);
    if (!status.ok()) return status;
    ap = new ActionPointInfo(address);
    action_point_map_.insert(std::make_pair(address, ap));
  } else {
    ap = it->second;
    // If there are no active actions, write out a breakpoint instruction.
    if (ap->num_active == 0) {
      auto status = ap_memory_interface_->WriteBreakpointInstruction(address);
      if (!status.ok()) return status;
    }
  }
  // Add the function as an enabled action.
  int id = ap->next_id++;
  auto *action_info =
      new ActionInfo(std::move(action_fcn), /*is_enabled=*/true);
  ap->action_map.insert(std::make_pair(id, action_info));
  ap->num_active++;
  return id;
}

absl::Status ActionPointManagerBase::ClearAction(uint64_t address, int id) {
  // Find the action point.
  auto it = action_point_map_.find(address);
  if (it == action_point_map_.end()) {
    return absl::NotFoundError(
        absl::StrCat("No action point found at: ", absl::Hex(address)));
  }
  // Find the action.
  auto *ap = it->second;
  auto action_it = ap->action_map.find(id);
  if (action_it == ap->action_map.end()) {
    return absl::NotFoundError(
        absl::StrCat("No action ", id, "found at: ", absl::Hex(address)));
  }
  // Remove the action.
  auto *action_info = action_it->second;
  ap->action_map.erase(action_it);
  ap->num_active -= action_info->is_enabled ? 1 : 0;
  delete action_info;
  // If there are no other active actions, write back the original instruction.
  if (ap->num_active == 0) {
    auto status = ap_memory_interface_->WriteOriginalInstruction(address);
    if (!status.ok()) return status;
  }
  return absl::OkStatus();
}

absl::Status ActionPointManagerBase::EnableAction(uint64_t address, int id) {
  // Find the action point.
  auto it = action_point_map_.find(address);
  if (it == action_point_map_.end()) {
    return absl::NotFoundError(
        absl::StrCat("No action point found at: ", absl::Hex(address)));
  }
  auto *ap = it->second;
  // Find the action.
  auto action_it = ap->action_map.find(id);
  if (action_it == ap->action_map.end()) {
    return absl::NotFoundError(
        absl::StrCat("No action ", id, "found at: ", absl::Hex(address)));
  }
  // If it is already enabled, return ok.
  if (action_it->second->is_enabled) return absl::OkStatus();
  action_it->second->is_enabled = true;
  ap->num_active++;
  // If there is only one active action (this), write a breakpoint instruction.
  if (ap->num_active == 1) {
    auto status = ap_memory_interface_->WriteBreakpointInstruction(address);
    if (!status.ok()) return status;
  }
  return absl::OkStatus();
}

absl::Status ActionPointManagerBase::DisableAction(uint64_t address, int id) {
  // Find the action point.
  auto it = action_point_map_.find(address);
  if (it == action_point_map_.end()) {
    return absl::NotFoundError(
        absl::StrCat("No action point found at: ", absl::Hex(address)));
  }
  // Find the action.
  auto *ap = it->second;
  auto action_it = ap->action_map.find(id);
  if (action_it == ap->action_map.end()) {
    return absl::NotFoundError(
        absl::StrCat("No action ", id, "found at: ", absl::Hex(address)));
  }
  // If it is already disabled, return ok.
  if (!action_it->second->is_enabled) return absl::OkStatus();
  // Disable the action.
  action_it->second->is_enabled = false;
  ap->num_active--;
  // If there are no active actions, write back the original instruction.
  if (ap->num_active == 0) {
    auto status = ap_memory_interface_->WriteOriginalInstruction(address);
    if (!status.ok()) return status;
  }
  return absl::OkStatus();
}

bool ActionPointManagerBase::IsActionPointActive(uint64_t address) const {
  // Find the action point.
  auto it = action_point_map_.find(address);
  if (it == action_point_map_.end()) return false;

  auto *ap = it->second;
  return ap->num_active > 0;
}

bool ActionPointManagerBase::IsActionEnabled(uint64_t address, int id) const {
  // Find the action point.
  auto it = action_point_map_.find(address);
  if (it == action_point_map_.end()) return false;

  auto *ap = it->second;
  // Find the action.
  auto action_it = ap->action_map.find(id);
  if (action_it == ap->action_map.end()) return false;
  return action_it->second->is_enabled;
}

void ActionPointManagerBase::ClearAllActionPoints() {
  for (auto &[address, ap_ptr] : action_point_map_) {
    for (auto &[unused, action_ptr] : ap_ptr->action_map) {
      delete action_ptr;
    }
    ap_ptr->action_map.clear();
    (void)ap_memory_interface_->WriteOriginalInstruction(ap_ptr->address);
    delete ap_ptr;
  }
  action_point_map_.clear();
}

void ActionPointManagerBase::PerformActions(uint64_t address) {
  auto it = action_point_map_.find(address);
  if (it == action_point_map_.end()) {
    LOG(ERROR) << absl::StrCat("No action point found at: ",
                               absl::Hex(address));
  }

  for (auto &[id, action_ptr] : it->second->action_map) {
    if (action_ptr->is_enabled) action_ptr->action_fcn(address, id);
  }
}

}  // namespace mpact::sim::generic
