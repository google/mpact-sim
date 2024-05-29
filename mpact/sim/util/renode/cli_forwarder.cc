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

#include "mpact/sim/util/renode/cli_forwarder.h"

#include <cstddef>
#include <cstdint>
#include <string>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "mpact/sim/generic/core_debug_interface.h"
#include "mpact/sim/generic/data_buffer.h"
#include "mpact/sim/util/renode/renode_cli_top.h"

namespace mpact {
namespace sim {
namespace util {
namespace renode {

using ::mpact::sim::generic::DataBuffer;
using RunStatus = ::mpact::sim::generic::CoreDebugInterface::RunStatus;
using HaltReasonValueType =
    ::mpact::sim::generic::CoreDebugInterface::HaltReasonValueType;

CLIForwarder::CLIForwarder(RenodeCLITop *top) : top_(top) {}

// Forward the calls to the CheriotRenodeCLITop class - CLI methods.

absl::Status CLIForwarder::Halt() { return top_->CLIHalt(); }

absl::StatusOr<int> CLIForwarder::Step(int num) { return top_->CLIStep(num); }

absl::Status CLIForwarder::Run() { return top_->CLIRun(); }

absl::Status CLIForwarder::Wait() { return top_->CLIWait(); }

absl::StatusOr<RunStatus> CLIForwarder::GetRunStatus() {
  return top_->CLIGetRunStatus();
}

absl::StatusOr<HaltReasonValueType> CLIForwarder::GetLastHaltReason() {
  return top_->CLIGetLastHaltReason();
}

absl::StatusOr<uint64_t> CLIForwarder::ReadRegister(const std::string &name) {
  return top_->CLIReadRegister(name);
}

absl::Status CLIForwarder::WriteRegister(const std::string &name,
                                         uint64_t value) {
  return top_->CLIWriteRegister(name, value);
}

absl::StatusOr<DataBuffer *> CLIForwarder::GetRegisterDataBuffer(
    const std::string &name) {
  return top_->CLIGetRegisterDataBuffer(name);
}

absl::StatusOr<size_t> CLIForwarder::ReadMemory(uint64_t address, void *buf,
                                                size_t length) {
  return top_->CLIReadMemory(address, buf, length);
}

absl::StatusOr<size_t> CLIForwarder::WriteMemory(uint64_t address,
                                                 const void *buf,
                                                 size_t length) {
  return top_->CLIWriteMemory(address, buf, length);
}

bool CLIForwarder::HasBreakpoint(uint64_t address) {
  return top_->CLIHasBreakpoint(address);
}

absl::Status CLIForwarder::SetSwBreakpoint(uint64_t address) {
  return top_->CLISetSwBreakpoint(address);
}

absl::Status CLIForwarder::ClearSwBreakpoint(uint64_t address) {
  return top_->CLIClearSwBreakpoint(address);
}

absl::Status CLIForwarder::ClearAllSwBreakpoints() {
  return top_->CLIClearAllSwBreakpoints();
}

absl::StatusOr<Instruction *> CLIForwarder::GetInstruction(uint64_t address) {
  return top_->CLIGetInstruction(address);
}

absl::StatusOr<std::string> CLIForwarder::GetDisassembly(uint64_t address) {
  return top_->CLIGetDisassembly(address);
}

}  // namespace renode
}  // namespace util
}  // namespace sim
}  // namespace mpact
