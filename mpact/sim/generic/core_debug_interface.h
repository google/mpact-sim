#// Copyright 2023 Google LLC
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

ifndef MPACT_SIM_GENERIC_CORE_DEBUG_INTERFACE_H_
#define MPACT_SIM_GENERIC_CORE_DEBUG_INTERFACE_H_

#include <string>

#include "absl/status/status.h"
#include "absl/status/statusor.h"

namespace mpact {
namespace sim {
namespace generic {

class Instruction;

// This class defines an interface for controlling a simulator. This interface
// should be implemented at the top level of the class hierarchy for a single
// core. The intent is that this interface provides a uniform method for
// controlling invidividual core simualtors, as well as providing a simplified
// interface to help in debugging programs running on these cores.
class CoreDebugInterface {
 public:
  // The current run status of the core.
  enum class RunStatus {
    kHalted = 0,
    kRunning = 1,
    kSingleStep = 2,
    kNone,
  };

  // The reason for the last halt request.
  enum class HaltReason {
    kSoftwareBreakpoint = 0,
    kHardwareBreakpoint = 1,
    kUserRequest = 2,
    kSemihostHaltRequest = 3,
    kNone,
  };

  virtual ~CoreDebugInterface() = default;

  // Request that core stop running.
  virtual absl::Status Halt() = 0;
  // Step the core by num instructions.
  virtual absl::StatusOr<int> Step(int num) = 0;
  // Allow the core to free-run. The loop to run the instructions should be
  // in a separate thread so that this method can return. This allows a user
  // interface built on top of this interface to handle multiple cores running
  // at the same time.
  virtual absl::Status Run() = 0;
  // Wait until the current core halts execution.
  virtual absl::Status Wait() = 0;

  // Returns the current run status.
  virtual absl::StatusOr<RunStatus> GetRunStatus() = 0;
  // Returns the reason for the most recent halt.
  virtual absl::StatusOr<HaltReason> GetLastHaltReason() = 0;

  // Read/write the named registers.
  virtual absl::StatusOr<uint64_t> ReadRegister(const std::string &name) = 0;
  virtual absl::Status WriteRegister(const std::string &name,
                                     uint64_t value) = 0;

  // Read/write the buffers to memory.
  virtual absl::StatusOr<size_t> ReadMemory(uint64_t address, void *buf,
                                            size_t length) = 0;
  virtual absl::StatusOr<size_t> WriteMemory(uint64_t address, const void *buf,
                                             size_t length) = 0;

  // Test to see if there's a breakpoint at the given address.
  virtual bool HasBreakpoint(uint64_t address) = 0;
  // Set/Clear software breakpoints at the given addresses.
  virtual absl::Status SetSwBreakpoint(uint64_t address) = 0;
  virtual absl::Status ClearSwBreakpoint(uint64_t address) = 0;
  // Remove all software breakpoints.
  virtual absl::Status ClearAllSwBreakpoints() = 0;

  // Return the instruction object for the instruction at the given address.
  virtual absl::StatusOr<Instruction *> GetInstruction(uint64_t address) = 0;
  // Return the string representation for the instruction at the given address.
  virtual absl::StatusOr<std::string> GetDisassembly(uint64_t address) = 0;
};

}  // namespace generic
}  // namespace sim
}  // namespace mpact

#endif  // MPACT_SIM_GENERIC_CORE_DEBUG_INTERFACE_H_
