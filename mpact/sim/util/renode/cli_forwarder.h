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

#ifndef MPACT_SIM_UTIL_RENODE_CLI_FORWARDER_H_
#define MPACT_SIM_UTIL_RENODE_CLI_FORWARDER_H_

#include <cstddef>
#include <cstdint>
#include <string>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "mpact/sim/generic/core_debug_interface.h"
#include "mpact/sim/generic/data_buffer.h"
#include "mpact/sim/generic/instruction.h"
#include "mpact/sim/util/renode/renode_cli_top.h"

namespace mpact {
namespace sim {
namespace util {
namespace renode {

using ::mpact::sim::generic::CoreDebugInterface;
using ::mpact::sim::generic::DataBuffer;
using ::mpact::sim::generic::Instruction;

class CLIForwarder : public CoreDebugInterface {
 public:
  explicit CLIForwarder(RenodeCLITop *top);
  CLIForwarder() = delete;
  CLIForwarder(const CLIForwarder &) = delete;
  CLIForwarder &operator=(const CLIForwarder &) = delete;

  ~CLIForwarder() override = default;

  // Request that core stop running.
  absl::Status Halt() override;
  // Step the core by num instructions.
  absl::StatusOr<int> Step(int num) override;
  // Allow the core to free-run. The loop to run the instructions should be
  // in a separate thread so that this method can return. This allows a user
  // interface built on top of this interface to handle multiple cores running
  // at the same time.
  absl::Status Run() override;
  // Wait until the current core halts execution.
  absl::Status Wait() override;

  // Returns the current run status.
  absl::StatusOr<RunStatus> GetRunStatus() override;
  // Returns the reason for the most recent halt.
  absl::StatusOr<HaltReasonValueType> GetLastHaltReason() override;

  // Read/write the named registers.
  absl::StatusOr<uint64_t> ReadRegister(const std::string &name) override;
  absl::Status WriteRegister(const std::string &name, uint64_t value) override;

  // Some registers, including vector registers, have values that exceed the
  // 64 bits supported in the Read/Write register API calls. This function
  // obtains the DataBuffer structure for such registers, provided they use one.
  // The data in the DataBuffer instance can be written as well as read.
  // Note (1): DataBuffer instances are reference counted. If the simulator is
  // advanced after obtaining the instance, it may become invalid if it isn't
  // IncRef'ed appropriately (see data_buffer.h).
  // Note (2): In some cases, a register write may replace the DataBuffer
  // instance within a register so that any stored references to it become
  // stale.
  absl::StatusOr<DataBuffer *> GetRegisterDataBuffer(
      const std::string &name) override;

  // Read/write the buffers to memory.
  absl::StatusOr<size_t> ReadMemory(uint64_t address, void *buf,
                                    size_t length) override;
  absl::StatusOr<size_t> WriteMemory(uint64_t address, const void *buf,
                                     size_t length) override;

  // Test to see if there's a breakpoint at the given address.
  bool HasBreakpoint(uint64_t address) override;
  // Set/Clear software breakpoints at the given addresses.
  absl::Status SetSwBreakpoint(uint64_t address) override;
  absl::Status ClearSwBreakpoint(uint64_t address) override;
  // Remove all software breakpoints.
  absl::Status ClearAllSwBreakpoints() override;

  // Return the instruction object for the instruction at the given address.
  absl::StatusOr<Instruction *> GetInstruction(uint64_t address) override;
  // Return the string representation for the instruction at the given address.
  absl::StatusOr<std::string> GetDisassembly(uint64_t address) override;

 private:
  RenodeCLITop *top_;
};

}  // namespace renode
}  // namespace util
}  // namespace sim
}  // namespace mpact

#endif  // MPACT_SIM_UTIL_RENODE_CLI_FORWARDER_H_
