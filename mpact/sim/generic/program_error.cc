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

#include "mpact/sim/generic/program_error.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/memory/memory.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"

namespace mpact {
namespace sim {
namespace generic {

constexpr char ProgramErrorController::kInternalErrorName[];

// Construct the controller and add the internal error type without creating
// a ProgramError instance.
ProgramErrorController::ProgramErrorController(absl::string_view name)
    : name_(name) {
  Insert(kInternalErrorName);
}

void ProgramErrorController::Insert(absl::string_view name) {
  int index = program_error_info_.size();
  std::string str_name{name};
  program_error_map_.insert(std::make_pair(str_name, index));
  program_error_info_.push_back(ProgramErrorInfo(str_name));
}

bool ProgramErrorController::AddProgramErrorName(
    absl::string_view program_error_name) {
  if (program_error_map_.contains(program_error_name)) {
    // It's an error if the name already exists.
    Raise(kInternalErrorName,
          "Duplicate program error name in AddProgramError");
    return false;
  }
  Insert(program_error_name);
  return true;
}

bool ProgramErrorController::HasProgramErrorName(
    absl::string_view program_error_name) {
  return program_error_map_.contains(program_error_name);
}

std::unique_ptr<ProgramError> ProgramErrorController::GetProgramError(
    absl::string_view program_error_name) {
  if (program_error_map_.contains(program_error_name)) {
    return absl::WrapUnique(new ProgramError(program_error_name, this));
  }
  return nullptr;
}

void ProgramErrorController::Clear(absl::string_view program_error_name) {
  auto entry = program_error_map_.find(program_error_name);
  if (program_error_map_.end() != entry) {
    // Get the program error index, clear the error messages, and remove any
    // error from the error sets.
    int index = entry->second;
    program_error_info_[index].error_messages.clear();
    unmasked_program_errors_.erase(index);
    masked_program_errors_.erase(index);
  } else {
    Raise(kInternalErrorName, "Unknown program_error_name in Clear()");
  }
}

void ProgramErrorController::ClearAll() {
  for (auto& entry : program_error_info_) {
    entry.error_messages.clear();
  }
  unmasked_program_errors_.clear();
  masked_program_errors_.clear();
}

void ProgramErrorController::Mask(absl::string_view program_error_name) {
  auto entry = program_error_map_.find(program_error_name);
  if (program_error_name == kInternalErrorName) {
    // Cannot mask internal error.
    Raise(kInternalErrorName, "Cannot mask internal simulator error");
    return;
  }
  if (program_error_map_.end() != entry) {
    int index = entry->second;
    program_error_info_[index].is_masked = true;
    auto set_entry = unmasked_program_errors_.find(index);

    // If it is in the unmasked errors, move it to the masked errors.
    if (unmasked_program_errors_.end() != set_entry) {
      unmasked_program_errors_.erase(index);
      masked_program_errors_.insert(index);
    }
  } else {
    Raise(kInternalErrorName, "Unknown program_error_name in Mask()");
  }
}

void ProgramErrorController::Unmask(absl::string_view program_error_name) {
  auto entry = program_error_map_.find(program_error_name);
  if (program_error_map_.end() != entry) {
    int index = entry->second;
    program_error_info_[index].is_masked = false;
    auto set_entry = masked_program_errors_.find(index);

    // If it is in the masked errors, move it to the unmasked errors.
    if (masked_program_errors_.end() != set_entry) {
      masked_program_errors_.erase(index);
      unmasked_program_errors_.insert(index);
    }
  } else {
    Raise(kInternalErrorName, "Unknown program_error_name in Mask()");
  }
}

bool ProgramErrorController::IsMasked(absl::string_view program_error_name) {
  auto entry = program_error_map_.find(program_error_name);
  if (program_error_map_.end() != entry) {
    int index = entry->second;
    return program_error_info_[index].is_masked;
  }
  Raise(kInternalErrorName, "Unknown program_error_name in IsMasked()");
  return false;
}

bool ProgramErrorController::HasError() const {
  return !masked_program_errors_.empty() || !unmasked_program_errors_.empty();
}

bool ProgramErrorController::HasMaskedError() const {
  return !masked_program_errors_.empty();
}

bool ProgramErrorController::HasUnmaskedError() const {
  return !unmasked_program_errors_.empty();
}

std::vector<std::string> ProgramErrorController::GetMaskedErrorNames() const {
  std::vector<std::string> names;
  for (auto index : masked_program_errors_) {
    names.push_back(program_error_info_[index].name);
  }
  return names;
}

std::vector<std::string> ProgramErrorController::GetUnmaskedErrorNames() const {
  std::vector<std::string> names;
  for (auto index : unmasked_program_errors_) {
    names.push_back(program_error_info_[index].name);
  }
  return names;
}

const std::vector<std::string>& ProgramErrorController::GetErrorMessages(
    absl::string_view program_error_name) {
  auto entry = program_error_map_.find(program_error_name);
  if (program_error_map_.end() != entry) {
    int index = entry->second;
    return program_error_info_[index].error_messages;
  }
  Raise(kInternalErrorName, "Unknown program_error_name in GetErrorMessages");
  return GetErrorMessages(kInternalErrorName);
}

void ProgramErrorController::Raise(absl::string_view program_error_name,
                                   absl::string_view error_message) {
  auto entry = program_error_map_.find(program_error_name);
  if (program_error_map_.end() != entry) {
    int index = entry->second;

    // Can call insert even if element is already in the set. It will not be
    // duplicated.
    if (program_error_info_[index].is_masked) {
      masked_program_errors_.insert(index);
    } else {
      unmasked_program_errors_.insert(index);
    }
    program_error_info_[index].error_messages.push_back(
        std::string(error_message));
    return;
  }
  Raise(kInternalErrorName,
        absl::StrCat("Unknown program_error_name in Raise with message: ",
                     error_message));
}

// Constructor and method for class ProgramError.
ProgramError::ProgramError(absl::string_view name,
                           ProgramErrorController* controller)
    : name_(name), controller_(controller) {}

void ProgramError::Raise(absl::string_view error_message) {
  controller_->Raise(name_, error_message);
}

}  // namespace generic
}  // namespace sim
}  // namespace mpact
