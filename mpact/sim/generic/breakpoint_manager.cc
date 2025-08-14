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

#include "mpact/sim/generic/breakpoint_manager.h"

#include <cstdint>
#include <utility>

#include "absl/functional/bind_front.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "mpact/sim/generic/action_point_manager_base.h"

namespace mpact {
namespace sim {
namespace generic {

BreakpointManager::BreakpointManager(
    ActionPointManagerBase* action_point_manager,
    RequestHaltFunction req_halt_function)
    : req_halt_function_(std::move(req_halt_function)),
      action_point_manager_(action_point_manager) {}

BreakpointManager::~BreakpointManager() {
  for (auto const& [unused, bp_ptr] : breakpoint_map_) {
    delete bp_ptr;
  }
  req_halt_function_ = nullptr;
  breakpoint_map_.clear();
}

bool BreakpointManager::HasBreakpoint(uint64_t address) {
  return breakpoint_map_.contains(address);
}

absl::Status BreakpointManager::SetBreakpoint(uint64_t address) {
  if (HasBreakpoint(address))
    return absl::AlreadyExistsError(
        absl::StrCat("Error SetBreakpoint: Breakpoint at ", absl::Hex(address),
                     " already exists"));

  auto result = action_point_manager_->SetAction(
      address, absl::bind_front(&BreakpointManager::DoBreakpointAction, this));
  if (!result.ok()) return result.status();
  auto id = result.value();

  auto* bp = new BreakpointInfo{address, id, /*is_active=*/true};
  breakpoint_map_.insert(std::make_pair(address, bp));

  return absl::OkStatus();
}

absl::Status BreakpointManager::ClearBreakpoint(uint64_t address) {
  // Lookup the breakpoint.
  auto iter = breakpoint_map_.find(address);
  if (iter == breakpoint_map_.end()) {
    auto msg = absl::StrCat("Error ClearBreakpoint: No breakpoint set for ",
                            absl::Hex(address));
    LOG(WARNING) << msg;
    return absl::NotFoundError(msg);
  }
  auto status = action_point_manager_->ClearAction(address, iter->second->id);
  if (!status.ok()) return status;

  auto* bp = iter->second;
  breakpoint_map_.erase(iter);
  delete bp;
  return absl::OkStatus();
}

absl::Status BreakpointManager::DisableBreakpoint(uint64_t address) {
  // Lookup the breakpoint.
  auto iter = breakpoint_map_.find(address);
  if (iter == breakpoint_map_.end()) {
    auto msg = absl::StrCat("Error DisableBreakpoint: No breakpoint set for ",
                            absl::Hex(address));
    LOG(WARNING) << msg;
    return absl::NotFoundError(msg);
  }

  auto status = action_point_manager_->DisableAction(address, iter->second->id);
  return status;
}

absl::Status BreakpointManager::EnableBreakpoint(uint64_t address) {
  auto iter = breakpoint_map_.find(address);
  if (iter == breakpoint_map_.end())
    return absl::NotFoundError(absl::StrCat(
        "Error EnableBreakpoint: No breakpoint set for ", absl::Hex(address)));

  auto status = action_point_manager_->EnableAction(address, iter->second->id);
  return status;
}

void BreakpointManager::ClearAllBreakpoints() {
  for (auto const& [unused, bp_ptr] : breakpoint_map_) {
    (void)action_point_manager_->ClearAction(bp_ptr->address, bp_ptr->id);
    delete bp_ptr;
  }
  breakpoint_map_.clear();
}

bool BreakpointManager::IsBreakpoint(uint64_t address) const {
  auto iter = breakpoint_map_.find(address);
  if (iter == breakpoint_map_.end()) return false;

  return action_point_manager_->IsActionEnabled(address, iter->second->id);
}

void BreakpointManager::DoBreakpointAction(uint64_t, int) {
  req_halt_function_();
}

}  // namespace generic
}  // namespace sim
}  // namespace mpact
