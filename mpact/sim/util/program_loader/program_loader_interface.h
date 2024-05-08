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

#ifndef LEARNING_BRAIN_RESEARCH_MPACT_SIM_UTIL_PROGRAM_LOADER_PROGRAM_LOADER_INTERFACE_H_
#define LEARNING_BRAIN_RESEARCH_MPACT_SIM_UTIL_PROGRAM_LOADER_PROGRAM_LOADER_INTERFACE_H_

#include <cstdint>
#include <string>

#include "absl/status/statusor.h"

namespace mpact {
namespace sim {
namespace util {

class ProgramLoaderInterface {
 public:
  virtual ~ProgramLoaderInterface() = default;
  // Load the executable into the program loader, but don't write segments to
  // memory. Return the entry point.
  virtual absl::StatusOr<uint64_t> LoadSymbols(
      const std::string &file_name) = 0;
  // Write program segments to memories and return the entry point.
  virtual absl::StatusOr<uint64_t> LoadProgram(
      const std::string &file_name) = 0;
};

}  // namespace util
}  // namespace sim
}  // namespace mpact

#endif  // LEARNING_BRAIN_RESEARCH_MPACT_SIM_UTIL_PROGRAM_LOADER_PROGRAM_LOADER_INTERFACE_H_
