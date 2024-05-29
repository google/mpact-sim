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

#include "mpact/sim/util/renode/renode_cli_top.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/synchronization/mutex.h"
#include "mpact/sim/generic/core_debug_interface.h"
#include "mpact/sim/generic/data_buffer.h"
#include "mpact/sim/generic/type_helpers.h"

namespace mpact {
namespace sim {
namespace util {
namespace renode {

using ::mpact::sim::generic::CoreDebugInterface;
using ::mpact::sim::generic::operator*;  // NOLINT: used below (clang error).

RenodeCLITop::RenodeCLITop(CoreDebugInterface *top, bool wait_for_cli)
    : top_(top), wait_for_cli_(wait_for_cli) {
  absl::MutexLock lock(&run_control_mutex_);
  cli_status_ = wait_for_cli_ ? RunStatus::kHalted : RunStatus::kRunning;
  cli_steps_taken_ = 0;
  cli_steps_to_take_ = 0;
  renode_steps_taken_ = 0;
  renode_steps_to_take_ = 0;
}

void RenodeCLITop::SetConnected(bool connected) {
  // Only act upon changes in connectivity.
  if (connected == cli_connected_) return;
  cli_connected_ = connected;
  absl::MutexLock lock(&run_control_mutex_);
  cli_status_ = cli_connected_ ? RunStatus::kHalted : RunStatus::kRunning;
}

absl::StatusOr<int> RenodeCLITop::RenodeStep(int num) {
  run_control_mutex_.Lock();
  renode_steps_taken_ = 0;
  renode_steps_to_take_ = num;
  auto renode_take_control = [this]() -> bool {
    return (cli_status_ == RunStatus::kRunning) ||
           (renode_steps_taken_ >= renode_steps_to_take_);
  };
  // CLI was either idle, running, or stepping previously when cli run control
  // was turned off. If it was idle, then don't step, just wait for change to
  // running or stepping. If it was running, step like normal. If it was
  // stepping, step for the smaller of the renode and step count and the
  // remaining cli step count.
  absl::Status status = absl::OkStatus();
  while (true) {
    uint64_t stepped = 0;
    run_control_mutex_.Await(absl::Condition(&renode_take_control));
    // See if there is any stepping left to do now that renode has control
    // again. The steps might have been taken while the CLI was stepping.
    if (renode_steps_to_take_ <= renode_steps_taken_) break;

    auto res = top_->Step(renode_steps_to_take_ - renode_steps_taken_);
    if (res.ok()) {
      stepped = res.value();
      renode_steps_taken_ += stepped;
      // Check halt reason.
      auto halt_result = top_->GetLastHaltReason();
      CHECK_OK(halt_result.status());
      auto halt_reason = halt_result.value();
      // If it is a program halt request that is not ProgramDone, give control
      // to CLI but prepare to continue stepping once control is returned.
      if ((halt_reason != *HaltReason::kProgramDone) &&
          (halt_reason != *HaltReason::kNone)) {
        cli_status_ = RunStatus::kHalted;
        continue;
      }
      // ProgramHalted request halts the simulation. So transfer control and
      // return.
      if (halt_reason == *HaltReason::kProgramDone) {
        LOG(INFO) << "Renode halted kProgramDone";
        cli_status_ = RunStatus::kHalted;
        break;
      }
      // If we have stepped enough, just return.
      if (renode_steps_to_take_ <= renode_steps_taken_) break;
    } else {
      status = res.status();
      break;
    }
  }
  run_control_mutex_.Unlock();
  if (!status.ok()) return status;
  return renode_steps_taken_;
}

// There should be no reason to guard these calls from renode with mutexes,
// as they will only be done while renode has control.
absl::StatusOr<HaltReasonValueType> RenodeCLITop::RenodeGetLastHaltReason() {
  return top_->GetLastHaltReason();
}

absl::StatusOr<uint64_t> RenodeCLITop::RenodeReadRegister(
    const std::string &name) {
  return top_->ReadRegister(name);
}

absl::Status RenodeCLITop::RenodeWriteRegister(const std::string &name,
                                               uint64_t value) {
  return top_->WriteRegister(name, value);
}

absl::StatusOr<size_t> RenodeCLITop::RenodeReadMemory(uint64_t address,
                                                      void *buf,
                                                      size_t length) {
  return top_->ReadMemory(address, buf, length);
}

absl::StatusOr<size_t> RenodeCLITop::RenodeWriteMemory(uint64_t address,
                                                       const void *buf,
                                                       size_t length) {
  return top_->WriteMemory(address, buf, length);
}

// Command line interface handlers.
absl::Status RenodeCLITop::CLIHalt() {
  // Halt the simulator and claim run control.
  auto status = top_->Halt();
  {
    absl::MutexLock lock(&run_control_mutex_);
    cli_status_ = RunStatus::kHalted;
  }
  return status;
}

absl::Status RenodeCLITop::CLIRun() {
  // This predicate is used to determine when the command line interface is
  // in control for the purposes of issuing a run command.
  auto cli_is_not_running = [this] {
    return cli_status_ != RunStatus::kRunning;
  };
  run_control_mutex_.LockWhen(absl::Condition(&cli_is_not_running));
  if (top_->GetLastHaltReason().value() == *HaltReason::kProgramDone) {
    run_control_mutex_.Unlock();
    return absl::UnavailableError("Program terminated");
  }
  cli_status_ = RunStatus::kRunning;
  // Wait for cli to be back in control.
  run_control_mutex_.Await(absl::Condition(&cli_is_not_running));
  // Unlock and return to CLI.
  run_control_mutex_.Unlock();
  return absl::OkStatus();
}

absl::Status RenodeCLITop::CLIWait() {
  // No need to lock for this call.
  return top_->Wait();
}

absl::StatusOr<int> RenodeCLITop::CLIStep(int num) {
  run_control_mutex_.Lock();
  if (top_->GetLastHaltReason().value() == *HaltReason::kProgramDone) {
    run_control_mutex_.Unlock();
    return absl::UnavailableError("Program terminated");
  }
  // Lambda used in Await below.
  auto cli_is_in_control = [this] {
    return (cli_status_ != RunStatus::kRunning) &&
           (renode_steps_to_take_ > renode_steps_taken_);
  };

  cli_steps_to_take_ = num;
  cli_steps_taken_ = 0;
  absl::Status status = absl::OkStatus();
  while (true) {
    // Release the lock and regain the lock when the CLI is in control. This
    // allows control to switch between ReNode and CLI while each makes progress
    // towards their step count.
    run_control_mutex_.Await(absl::Condition(&cli_is_in_control));
    if (cli_steps_to_take_ <= cli_steps_taken_) {
      cli_status_ = RunStatus::kHalted;
      break;
    }
    cli_status_ = RunStatus::kSingleStep;
    auto steps_to_take = std::min(cli_steps_to_take_ - cli_steps_taken_,
                                  renode_steps_to_take_ - renode_steps_taken_);
    auto step_result = top_->Step(steps_to_take);
    if (!step_result.ok()) {
      status = step_result.status();
      break;
    }
    auto steps_taken = step_result.value();
    cli_steps_taken_ += steps_taken;
    renode_steps_taken_ += steps_taken;
    auto halt_result = top_->GetLastHaltReason();
    CHECK_OK(halt_result.status());
    auto halt_reason = halt_result.value();
    // Check if the program is done.
    if (halt_reason == *HaltReason::kProgramDone) {
      // Set cli state as kRunning to give control to ReNode.
      cli_status_ = RunStatus::kRunning;
      status = absl::UnavailableError("Program terminated");
      break;
    }
    // If we're done with renode steps, then go to the top of the loop so
    // that renode can get control and start stepping again.
    if ((halt_reason == *HaltReason::kNone) &&
        (renode_steps_taken_ >= renode_steps_to_take_)) {
      continue;
    }
    cli_status_ = RunStatus::kHalted;
    break;
  }
  run_control_mutex_.Unlock();
  if (!status.ok()) return status;
  return cli_steps_taken_;
}

absl::StatusOr<RunStatus> RenodeCLITop::CLIGetRunStatus() {
  return DoWhenInControl<absl::StatusOr<RunStatus>>(
      [this]() { return top_->GetRunStatus(); });
}

absl::StatusOr<HaltReasonValueType> RenodeCLITop::CLIGetLastHaltReason() {
  return DoWhenInControl<absl::StatusOr<HaltReasonValueType>>(
      [this]() { return top_->GetLastHaltReason(); });
}

absl::StatusOr<uint64_t> RenodeCLITop::CLIReadRegister(
    const std::string &name) {
  return DoWhenInControl<absl::StatusOr<uint64_t>>(
      [this, &name]() { return top_->ReadRegister(name); });
}

absl::Status RenodeCLITop::CLIWriteRegister(const std::string &name,
                                            uint64_t value) {
  return DoWhenInControl<absl::Status>(
      [this, &name, &value]() { return top_->WriteRegister(name, value); });
}

absl::StatusOr<generic::DataBuffer *> RenodeCLITop::CLIGetRegisterDataBuffer(
    const std::string &name) {
  return DoWhenInControl<absl::StatusOr<generic::DataBuffer *>>(
      [this, &name]() { return top_->GetRegisterDataBuffer(name); });
}

absl::StatusOr<size_t> RenodeCLITop::CLIReadMemory(uint64_t address, void *buf,
                                                   size_t length) {
  return DoWhenInControl<absl::StatusOr<size_t>>(
      [this, &address, &buf, &length]() {
        return top_->ReadMemory(address, buf, length);
      });
}

absl::StatusOr<size_t> RenodeCLITop::CLIWriteMemory(uint64_t address,
                                                    const void *buf,
                                                    size_t length) {
  return DoWhenInControl<absl::StatusOr<size_t>>(
      [this, &address, &buf, &length]() {
        return top_->WriteMemory(address, buf, length);
      });
}

bool RenodeCLITop::CLIHasBreakpoint(uint64_t address) {
  return DoWhenInControl<bool>(
      [this, &address]() { return top_->HasBreakpoint(address); });
}

absl::Status RenodeCLITop::CLISetSwBreakpoint(uint64_t address) {
  return DoWhenInControl<absl::Status>(
      [this, &address]() { return top_->SetSwBreakpoint(address); });
}

absl::Status RenodeCLITop::CLIClearSwBreakpoint(uint64_t address) {
  return DoWhenInControl<absl::Status>(
      [this, &address]() { return top_->ClearSwBreakpoint(address); });
}

absl::Status RenodeCLITop::CLIClearAllSwBreakpoints() {
  return DoWhenInControl<absl::Status>(
      [this]() { return top_->ClearAllSwBreakpoints(); });
}

absl::StatusOr<Instruction *> RenodeCLITop::CLIGetInstruction(
    uint64_t address) {
  return DoWhenInControl<absl::StatusOr<Instruction *>>(
      [this, &address]() { return top_->GetInstruction(address); });
}

absl::StatusOr<std::string> RenodeCLITop::CLIGetDisassembly(uint64_t address) {
  return DoWhenInControl<absl::StatusOr<std::string>>(
      [this, &address]() { return top_->GetDisassembly(address); });
}

void RenodeCLITop::CLIRequestHalt(HaltReason halt_reason,
                                  const Instruction *inst) {
  (void)top_->Halt();
}

}  // namespace renode
}  // namespace util
}  // namespace sim
}  // namespace mpact
