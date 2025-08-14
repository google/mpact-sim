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

#ifndef MPACT_SIM_UTIL_RENODE_RENODE_MPACT_H_
#define MPACT_SIM_UTIL_RENODE_RENODE_MPACT_H_

#include <cstddef>
#include <cstdint>

#include "absl/container/flat_hash_map.h"
#include "absl/functional/any_invocable.h"
#include "mpact/sim/util/renode/renode_debug_interface.h"
#include "mpact/sim/util/renode/renode_memory_access.h"

// This file defines the interface that Renode uses to communicate with the
// simulator as well as support classes to implement the interface.

// C interface used by Renode.
extern "C" {
// There are two ways to create debug instances: create and connect. The main
// difference between them is that connect specifies the debug instance id to
// use (and thus must be managed by the caller). This also also allows multiple
// connections to the debug instance, which is useful if the debug instance
// is used both as a bus initiator as well as a bus target in a system
// simulation.

// Create a debug instance, returning its id. A return value of zero indicates
// and error. The sysbus variant of the call provides methods to perform loads
// and stores from a memory space managed by the caller.
int32_t construct(char* cpu_type, int32_t max_name_length);
int32_t construct_with_sysbus(char* cpu_type, int32_t max_name_length,
                              int32_t (*read_callback)(uint64_t, char*,
                                                       int32_t),
                              int32_t (*write_callback)(uint64_t, char*,
                                                        int32_t));
// Connect with, or construct, a debug instance connected to a simulator with
// the given id. A return value of zero indicates an error. The sysbus variant
// of the call provides methods to perform loads and stores from a memory space
// managed by the caller.
int32_t connect(char* cpu_type, int32_t id, int32_t max_name_length);
int32_t connect_with_sysbus(char* cpu_type, int32_t id, int32_t max_name_length,
                            int32_t (*read_callback)(uint64_t, char*, int32_t),
                            int32_t (*write_callback)(uint64_t, char*,
                                                      int32_t));
// Destruct the given debug instance. A negative return value indicates an
// error.
void destruct(int32_t id);
// Return the number of register entries.
int32_t get_reg_info_size(int32_t id);
// Return the register entry with the given index. The info pointer should
// store an object of type RenodeCpuRegister.
int32_t get_reg_info(int32_t id, int32_t index, char* name, void* info);
// Use the loader to read in the given ELF executable. If the for_symbols_only
// is true, do not write it to memory or set the PC to the entry point. The
// memory and register initialization will be performed by ReNode. In this case
// the file is loaded only to provide symbol lookup.
uint64_t load_elf(int32_t id, const char* elf_file_name, bool for_symbols_only,
                  int32_t* status);
// Load the content of the given file into memory, starting at the given
// address.
int32_t load_image(int32_t id, const char* file_name, uint64_t address);
// Read register reg_id in the instance id, store the value in the pointer
// given. A return value < 0 is an error.
int32_t read_register(int32_t id, uint32_t reg_id, uint64_t* value);
// Write register reg_id in the instance id. A return value < 0 is an error.
int32_t write_register(int32_t id, uint32_t reg_id, uint64_t value);
// Read/Write memory.
uint64_t read_memory(int32_t id, uint64_t address, char* buffer,
                     uint64_t length);
uint64_t write_memory(int32_t id, uint64_t address, const char* buffer,
                      uint64_t length);
// Reset the instance. A return value < 0 is an error.
int32_t reset(int32_t id);
// Step the instance id by num_to_step instructions. Return the number of
// instructions stepped. The status is written to the pointer *status.
uint64_t step(int32_t id, uint64_t num_to_step, int32_t* status);
// Set configuration items. This passes in the id, two arrays of strings (names
// and values), and the size of the two arrays. Depending on the name of the
// configuration item, the string will be interpreted according to the expected
// type.
int32_t set_config(int32_t id, const char* config_name[],
                   const char* config_value[], int32_t size);
// Set the given irq number (if valid) to the provided value.
int32_t set_irq_value(int32_t id, int32_t irq_number, bool irq_value);
}  // extern "C"

namespace mpact {
namespace sim {
namespace util {
namespace renode {

// Execution results.
enum class ExecutionResult : int32_t {
  kOk = 0,
  kInterrupted = 1,
  kWaitingForInterrupt = 2,
  kStoppedAtBreakpoint = 3,
  kStoppedAtWatchpoint = 4,
  kExternalMmuFault = 5,
  kAborted = -1,
};

// Intermediary between the C interface above and the actual debug interface
// of the simulator.
class RenodeAgent {
 public:
  constexpr static size_t kBufferSize = 64 * 1024;
  // This is a singleton class, so need a static Instance method.
  static RenodeAgent* Instance() {
    if (instance_ != nullptr) return instance_;

    instance_ = new RenodeAgent();
    return instance_;
  }

  // These methods correspond to the C methods defined above.
  int32_t Construct(char* cpu_type, int32_t max_name_length,
                    int32_t (*read_callback)(uint64_t, char*, int32_t),
                    int32_t (*write_callback)(uint64_t, char*, int32_t));
  int32_t Connect(char* cpu_type, int32_t id, int32_t max_name_length,
                  int32_t (*read_callback)(uint64_t, char*, int32_t),
                  int32_t (*write_callback)(uint64_t, char*, int32_t));
  void Destroy(int32_t id);
  int32_t Reset(int32_t id);
  int32_t GetRegisterInfoSize(int32_t id) const;
  int32_t GetRegisterInfo(int32_t id, int32_t index, char* name,
                          RenodeCpuRegister* info);
  int32_t ReadRegister(int32_t id, uint32_t reg_id, uint64_t* value);
  int32_t WriteRegister(int32_t id, uint32_t reg_id, uint64_t value);
  uint64_t ReadMemory(int32_t id, uint64_t address, char* buffer,
                      uint64_t length);
  uint64_t WriteMemory(int32_t id, uint64_t address, const char* buffer,
                       uint64_t length);

  uint64_t LoadExecutable(int32_t id, const char* elf_file_name,
                          bool for_symbols_only, int32_t* status);
  int32_t LoadImage(int32_t id, const char* file_name, uint64_t address);
  uint64_t Step(int32_t id, uint64_t num_to_step, int32_t* status);
  int32_t SetConfig(int32_t id, const char* config_names[],
                    const char* config_values[], int32_t size);
  int32_t SetIrqValue(int32_t id, int32_t irq_number, bool irq_value);

  // Accessor.
  RenodeDebugInterface* core_dbg(int32_t id) const {
    auto ptr = core_dbg_instances_.find(id);
    if (ptr != core_dbg_instances_.end()) return ptr->second;
    return nullptr;
  }

 private:
  using RenodeMemoryFcn = absl::AnyInvocable<int32_t(uint64_t, char*, int32_t)>;
  // Private constructor.
  RenodeAgent() = default;
  static RenodeAgent* instance_;
  static int32_t count_;
  // Map of renode memory access interfaces used to access devices and memory
  // off the system bus.
  absl::flat_hash_map<int32_t, RenodeMemoryAccess*> renode_memory_access_;
  // Map of debug instances.
  absl::flat_hash_map<int32_t, RenodeDebugInterface*> core_dbg_instances_;
  absl::flat_hash_map<int32_t, int32_t> name_length_map_;
  absl::flat_hash_map<int32_t, uint64_t> memory_bases_;
  absl::flat_hash_map<int32_t, uint64_t> memory_sizes_;
};

}  // namespace renode
}  // namespace util
}  // namespace sim
}  // namespace mpact

#endif  // MPACT_SIM_UTIL_RENODE_RENODE_MPACT_H_
