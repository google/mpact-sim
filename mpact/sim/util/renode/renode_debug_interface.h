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

#ifndef MPACT_SIM_UTIL_RENODE_RENODE_DEBUG_INTERFACE_H_
#define MPACT_SIM_UTIL_RENODE_RENODE_DEBUG_INTERFACE_H_

#include <cstdint>
#include <string>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "mpact/sim/generic/core_debug_interface.h"
#include "mpact/sim/generic/type_helpers.h"

namespace mpact {
namespace sim {
namespace util {
namespace renode {

// This structure mirrors the one defined in renode to provide information
// about the target registers. Do not change, as it maps to the marshaling
// structure used by renode.
struct RenodeCpuRegister {
  int index;
  int width;
  bool is_general;
  bool is_read_only;
};

using ::mpact::sim::generic::operator*;

class RenodeDebugInterface : public generic::CoreDebugInterface {
 public:
  // These using declarations are required to tell the compiler that these
  // methods are not overridden nor hidden by the two virtual methods declared
  // in this class.
  using generic::CoreDebugInterface::ReadRegister;
  using generic::CoreDebugInterface::WriteRegister;

  // Load executable or load symbols from executable.
  virtual absl::StatusOr<uint64_t> LoadExecutable(const char *elf_file_name,
                                                  bool for_symbols_only) = 0;

  // Read/write the numeric id registers.
  virtual absl::StatusOr<uint64_t> ReadRegister(uint32_t reg_id) = 0;
  virtual absl::Status WriteRegister(uint32_t reg_id, uint64_t value) = 0;
  // Get register information.
  virtual int32_t GetRenodeRegisterInfoSize() const = 0;
  virtual absl::Status GetRenodeRegisterInfo(int32_t index, int32_t max_len,
                                             char *name,
                                             RenodeCpuRegister &info) = 0;
  // Set configuration item.
  virtual absl::Status SetConfig(const char *config_names[],
                                 const char *config_values[], int size) = 0;
  // Set IRQ.
  virtual absl::Status SetIrqValue(int32_t irq_num, bool irq_value) = 0;

  // The following methods from CoreDebugInterface will not be used by ReNode,
  // so providing trivial "final" implementations for now.
  // TODO(torerik): Maybe consider not inheriting from CoreDebugInterface in the
  // future?

  absl::Status Halt() final {
    return absl::InternalError("Halt: Not implemented");
  }
  absl::Status Wait() final {
    return absl::InternalError("Wait: Not implemented");
  }
  absl::Status Run() final {
    return absl::InternalError("Run: Not implemented");
  }
  absl::StatusOr<RunStatus> GetRunStatus() final {
    return absl::InternalError("GetRunStatus: Not implemented");
  }
  absl::StatusOr<uint64_t> ReadRegister(const std::string &name) final {
    return absl::InternalError("ReadRegister: Not implemented");
  }
  absl::Status WriteRegister(const std::string &name, uint64_t value) final {
    return absl::InternalError("WriteRegister: Not implemented");
  }
  absl::StatusOr<mpact::sim::generic::DataBuffer *> GetRegisterDataBuffer(
      const std::string &) final {
    return absl::InternalError("GetRegisterDataBuffer: Not implemented");
  }
  bool HasBreakpoint(uint64_t address) final { return false; }
  absl::Status SetSwBreakpoint(uint64_t address) final {
    return absl::InternalError("SetSwBreakpoint: Not implemented");
  }
  absl::Status ClearSwBreakpoint(uint64_t address) final {
    return absl::InternalError("ClearSwBreakpoint: Not implemented");
  }
  absl::Status ClearAllSwBreakpoints() final {
    return absl::InternalError("ClearAllSwBreakpoints: Not implemented");
  }
  absl::StatusOr<mpact::sim::generic::Instruction *> GetInstruction(
      uint64_t address) final {
    return absl::InternalError("GetInstruction: Not implemented");
  }
  absl::StatusOr<std::string> GetDisassembly(uint64_t address) final {
    return absl::InternalError("GetDisassembly: Not implemented");
  }
};

}  // namespace renode
}  // namespace util
}  // namespace sim
}  // namespace mpact

#endif  // MPACT_SIM_UTIL_RENODE_RENODE_DEBUG_INTERFACE_H_
