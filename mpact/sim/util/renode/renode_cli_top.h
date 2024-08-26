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

#ifndef MPACT_SIM_UTIL_RENODE_RENODE_CLI_TOP_H_
#define MPACT_SIM_UTIL_RENODE_RENODE_CLI_TOP_H_

#include <cstddef>
#include <cstdint>
#include <string>

#include "absl/functional/any_invocable.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/synchronization/mutex.h"
#include "mpact/sim/generic/core_debug_interface.h"
#include "mpact/sim/generic/data_buffer.h"
#include "mpact/sim/generic/instruction.h"

namespace mpact {
namespace sim {
namespace util {
namespace renode {

using ::mpact::sim::generic::AccessType;
using ::mpact::sim::generic::CoreDebugInterface;
using ::mpact::sim::generic::DataBuffer;
using ::mpact::sim::generic::Instruction;
using HaltReason = ::mpact::sim::generic::CoreDebugInterface::HaltReason;
using HaltReasonValueType =
    ::mpact::sim::generic::CoreDebugInterface::HaltReasonValueType;
using RunStatus = ::mpact::sim::generic::CoreDebugInterface::RunStatus;

// This class arbitrates and merges commands from ReNode and the socket command
// line interface and forwards them to the top simulator control interface.
class RenodeCLITop {
 public:
  RenodeCLITop(CoreDebugInterface *top, bool wait_for_cli);
  virtual ~RenodeCLITop() = default;

  // Set the connected status of the command line interface.
  virtual void SetConnected(bool connected);

  // Methods that handle requests from Renode.

  // Step the simulator.
  virtual absl::StatusOr<int> RenodeStep(int num);
  // Get the reason for the last halt.
  virtual absl::StatusOr<HaltReasonValueType> RenodeGetLastHaltReason();
  // Register access by register name.
  virtual absl::StatusOr<uint64_t> RenodeReadRegister(const std::string &name);
  virtual absl::Status RenodeWriteRegister(const std::string &name,
                                           uint64_t value);
  // Read and Write memory methods bypass any semihosting.
  virtual absl::StatusOr<size_t> RenodeReadMemory(uint64_t address, void *buf,
                                                  size_t length);
  virtual absl::StatusOr<size_t> RenodeWriteMemory(uint64_t address,
                                                   const void *buf,
                                                   size_t length);

  // Methods that handle requests from Command Line Interface.

  // Halt the simulator.
  virtual absl::Status CLIHalt();
  // Step the simulator.
  virtual absl::StatusOr<int> CLIStep(int num);
  // Allow the simulator to free run.
  virtual absl::Status CLIRun();
  // Wait for free run to complete.
  virtual absl::Status CLIWait();
  virtual absl::StatusOr<RunStatus> CLIGetRunStatus();
  virtual void CLIRequestHalt(HaltReason halt_reason, const Instruction *inst);
  virtual void CLIRequestHalt(HaltReasonValueType halt_reason,
                              const Instruction *inst);
  virtual absl::StatusOr<HaltReasonValueType> CLIGetLastHaltReason();
  // Register access by register name.
  virtual absl::StatusOr<uint64_t> CLIReadRegister(const std::string &name);
  virtual absl::Status CLIWriteRegister(const std::string &name,
                                        uint64_t value);
  virtual absl::StatusOr<DataBuffer *> CLIGetRegisterDataBuffer(
      const std::string &name);
  // Read and Write memory methods bypass any semihosting.
  virtual absl::StatusOr<size_t> CLIReadMemory(uint64_t address, void *buf,
                                               size_t length);
  virtual absl::StatusOr<size_t> CLIWriteMemory(uint64_t address,
                                                const void *buf, size_t length);

  // Breakpoint and watchpoint management.
  virtual bool CLIHasBreakpoint(uint64_t address);
  virtual absl::Status CLISetSwBreakpoint(uint64_t address);
  virtual absl::Status CLIClearSwBreakpoint(uint64_t address);
  virtual absl::Status CLIClearAllSwBreakpoints();
  virtual absl::StatusOr<Instruction *> CLIGetInstruction(uint64_t address);
  virtual absl::StatusOr<std::string> CLIGetDisassembly(uint64_t address);

 protected:
  // Perform the action after having obtained the lock that depends on the CLI
  // being in control.
  template <typename T>
  T DoWhenInControl(absl::AnyInvocable<T(void)> action);
  // Accessor.
  CoreDebugInterface *top() const { return top_; }

 private:
  bool IsCLIInControl() const;
  CoreDebugInterface *top_ = nullptr;
  // Mutex that determines which of ReNode and the command line interface has
  // control over the simulator control interface.
  absl::Mutex run_control_mutex_;
  // The status of the command line interface is used to determine which of
  // ReNode and the CLI has control. When the cli_status_ is kRunning, control
  // is transferred to ReNode. Any other status implies that the CLI has
  // control.
  RunStatus cli_status_ = RunStatus::kHalted;
  // The following counters are used to track the number of steps requested
  // and taken by ReNode and CLI. In particular, ReNode will call and request
  // a number of steps appropriate for the simulators run quantum. Within that
  // number of steps, the command line interface can advance with step/run up
  // to the total number of steps requested. At that point, the ReNode interface
  // must get control so the step function can return control to ReNode. On the
  // next step call from ReNode, the command line interface can continue in
  // control.
  uint64_t cli_steps_taken_ = 0;
  uint64_t cli_steps_to_take_ = 0;
  uint64_t renode_steps_taken_ = 0;
  uint64_t renode_steps_to_take_ = 0;
  bool cli_connected_ = false;
  bool wait_for_cli_;
  bool program_done_ = false;
};

// Templated definition of DoWhenInControl
template <typename T>
inline T RenodeCLITop::DoWhenInControl(absl::AnyInvocable<T(void)> action) {
  auto cli_is_in_control = [this] {
    return program_done_ || ((cli_status_ != RunStatus::kRunning) &&
                             (renode_steps_to_take_ > renode_steps_taken_));
  };
  run_control_mutex_.LockWhen(absl::Condition(&cli_is_in_control));
  T result = action();
  run_control_mutex_.Unlock();
  return result;
}

// Partial specialization for void return type.
template <>
inline void RenodeCLITop::DoWhenInControl<void>(
    absl::AnyInvocable<void(void)> action) {
  auto cli_is_in_control = [this] {
    return program_done_ || ((cli_status_ != RunStatus::kRunning) &&
                             (renode_steps_to_take_ > renode_steps_taken_));
  };
  run_control_mutex_.LockWhen(absl::Condition(&cli_is_in_control));
  action();
  run_control_mutex_.Unlock();
}

}  // namespace renode
}  // namespace util
}  // namespace sim
}  // namespace mpact

#endif  // MPACT_SIM_UTIL_RENODE_RENODE_CLI_TOP_H_
