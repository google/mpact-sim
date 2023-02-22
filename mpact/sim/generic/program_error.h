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

#ifndef LEARNING_BRAIN_RESEARCH_MPACT_SIM_GENERIC_PROGRAM_ERROR_H_
#define LEARNING_BRAIN_RESEARCH_MPACT_SIM_GENERIC_PROGRAM_ERROR_H_

#include <memory>
#include <string>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/strings/string_view.h"

// This file provides classes to facilitate modeling program errors/exceptions
// and store appropriate error messages in a "controller". The controller allows
// program errors to be masked or unmasked by name. There is also an internal
// simulation program error name where any detected simulator errors (as opposed
// to simulated architecture errors) can be raised.
//
// The idea is that code that is responsible for instruction issue and control
// uses this code to detect when a program error/exception has been raised and
// based on the type/masking status of the error acts accordingly.
//
// Different simulator constructs use ProgramError instances to signal error
// occurrences to the ProgramErrorController.
//
// Note: The term exception is used to describe the hardware exception or
// program error that may be raised on a simulated target architecture. It does
// not refer to C++ exceptions.

namespace mpact {
namespace sim {
namespace generic {

class ProgramError;

// The ProgramErrorController keeps track of errors that have been raised
// during the simulation, or by the simulator itself (for internal errors).
// It supports masked and unmasked errors. It does not by itself perform any
// actions in response to a reported error.
class ProgramErrorController {
 public:
  // Name for internal simulator errors.
  static constexpr char kInternalErrorName[] = "internal_simulator_error";

  explicit ProgramErrorController(absl::string_view name);

  // Clears the named program error (masked or unmasked).
  void Clear(absl::string_view program_error_name);

  // Clears all errors (masked or unmasked).
  void ClearAll();

  // Masks the named program error.
  void Mask(absl::string_view program_error_name);

  // Unmasks the named program error.
  void Unmask(absl::string_view program_error_name);

  // Returns true if the named program error is masked.
  bool IsMasked(absl::string_view program_error_name);

  // Returns true if any program errors have been set.
  bool HasError() const;

  // Returns true if a masked program error has been set.
  bool HasMaskedError() const;

  // Returns true if an unmasked program error has been set.
  bool HasUnmaskedError() const;

  // Returns a vector of active masked program error names. Active in this
  // context means that they have not been cleared since they were raised.
  std::vector<std::string> GetMaskedErrorNames() const;

  // Returns a vector of active, unmasked program error names.
  std::vector<std::string> GetUnmaskedErrorNames() const;

  // Returns a reference to the vector of error messages associated with the
  // named program error. If there is no such program_error_name the internal
  // error is raised (see kInternalErrorName), and the error messages for that
  // are returned.
  const std::vector<std::string> &GetErrorMessages(
      absl::string_view program_error_name);

  // Raises the named program error and adds the error message to its message
  // queue. This method is intended to be called from a ProgramError instance.
  void Raise(absl::string_view program_error_name,
             absl::string_view error_message);

  // Add a program error name to the controller, return true if it was added
  // successfully. Return false if it already exists.
  bool AddProgramErrorName(absl::string_view program_error_name);
  // Returns true if the program error name has already been added to the
  // controller.
  bool HasProgramErrorName(absl::string_view program_error_name);
  // Return a unique_ptr to a ProgramError instance tied to a program error that
  // already exists in the controller. Returns a nullptr if the program error
  // doesn't exist.
  std::unique_ptr<ProgramError> GetProgramError(
      absl::string_view program_error_name);

  // Accessors.
  absl::string_view name() const { return name_; }

 private:
  // Internal method used to insert a program error into the map and vector.
  void Insert(absl::string_view name);

  std::string name_;
  struct ProgramErrorInfo {
    std::string name;
    bool is_masked;
    std::vector<std::string> error_messages;
    explicit ProgramErrorInfo(absl::string_view error_name)
        : name(error_name), is_masked(false) {}
  };
  absl::flat_hash_map<std::string, int> program_error_map_;
  absl::flat_hash_set<int> unmasked_program_errors_;
  absl::flat_hash_set<int> masked_program_errors_;
  std::vector<ProgramErrorInfo> program_error_info_;
};

// The ProgramError class is used as a handle to report a particular program
// error or exception. Each ProgramError instance is tied to a particular
// error or exception type.
class ProgramError {
  friend class ProgramErrorController;

 private:
  // The constructor is private, as the handle is obtained from a
  // ProgramErrorController instance.
  ProgramError(absl::string_view name, ProgramErrorController *controller);

 public:
  // Raise the error to the ProgramErrorController with the given additional
  // error message.
  void Raise(absl::string_view error_message);
  // Accessor.
  absl::string_view name() const { return name_; }

 private:
  std::string name_;
  ProgramErrorController *controller_;
};

}  // namespace generic
}  // namespace sim
}  // namespace mpact

#endif  // LEARNING_BRAIN_RESEARCH_MPACT_SIM_GENERIC_PROGRAM_ERROR_H_
