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

#include "mpact/sim/util/renode/renode_mpact.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <ios>
#include <limits>
#include <string>

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "mpact/sim/generic/core_debug_interface.h"
#include "mpact/sim/generic/type_helpers.h"
#include "mpact/sim/util/memory/memory_interface.h"
#include "mpact/sim/util/renode/renode_debug_interface.h"
#include "mpact/sim/util/renode/renode_memory_access.h"

using ::mpact::sim::generic::operator*;  // NOLINT: used below (clang error).
using ::mpact::sim::util::MemoryInterface;

// This function must be defined in the library.
extern ::mpact::sim::util::renode::RenodeDebugInterface *CreateMpactSim(
    std::string, MemoryInterface *);

using ::mpact::sim::util::renode::RenodeAgent;
using ::mpact::sim::util::renode::RenodeCpuRegister;

// Implementation of the C interface functions. They each forward the call to
// the corresponding method in RenodeAgent.
int32_t construct(int32_t max_name_length) {
  return RenodeAgent::Instance()->Construct(max_name_length, nullptr, nullptr);
}

int32_t construct_with_sysbus(int32_t max_name_length,
                              int32_t (*read_callback)(uint64_t, char *,
                                                       int32_t),
                              int32_t (*write_callback)(uint64_t, char *,
                                                        int32_t)) {
  return RenodeAgent::Instance()->Construct(max_name_length, read_callback,
                                            write_callback);
}

int32_t connect(int32_t id, int32_t max_name_length) {
  return RenodeAgent::Instance()->Connect(id, max_name_length, nullptr,
                                          nullptr);
}

int32_t connect_with_sysbus(int32_t id, int32_t max_name_length,
                            int32_t (*read_callback)(uint64_t, char *, int32_t),
                            int32_t (*write_callback)(uint64_t, char *,
                                                      int32_t)) {
  return RenodeAgent::Instance()->Connect(id, max_name_length, read_callback,
                                          write_callback);
}

void destruct(int32_t id) { RenodeAgent::Instance()->Destroy(id); }

int32_t reset(int32_t id) { return RenodeAgent::Instance()->Reset(id); }

int32_t get_reg_info_size(int32_t id) {
  return RenodeAgent::Instance()->GetRegisterInfoSize(id);
}

int32_t get_reg_info(int32_t id, int32_t index, char *name, void *info) {
  if (info == nullptr) return -1;
  return RenodeAgent::Instance()->GetRegisterInfo(
      id, index, name, static_cast<RenodeCpuRegister *>(info));
}

uint64_t load_elf(int32_t id, const char *elf_file_name, bool for_symbols_only,
                  int32_t *status) {
  return RenodeAgent::Instance()->LoadExecutable(id, elf_file_name,
                                                 for_symbols_only, status);
}

int32_t load_image(int32_t id, const char *file_name, uint64_t address) {
  return RenodeAgent::Instance()->LoadImage(id, file_name, address);
}

int32_t read_register(int32_t id, uint32_t reg_id, uint64_t *value) {
  return RenodeAgent::Instance()->ReadRegister(id, reg_id, value);
}

int32_t write_register(int32_t id, uint32_t reg_id, uint64_t value) {
  return RenodeAgent::Instance()->WriteRegister(id, reg_id, value);
}

uint64_t read_memory(int32_t id, uint64_t address, char *buffer,
                     uint64_t length) {
  return RenodeAgent::Instance()->ReadMemory(id, address, buffer, length);
}

uint64_t write_memory(int32_t id, uint64_t address, const char *buffer,
                      uint64_t length) {
  return RenodeAgent::Instance()->WriteMemory(id, address, buffer, length);
}

uint64_t step(int32_t id, uint64_t num_to_step, int32_t *status) {
  return RenodeAgent::Instance()->Step(id, num_to_step, status);
}

int32_t set_config(int32_t id, const char *config_names[],
                   const char *config_values[], int32_t size) {
  return RenodeAgent::Instance()->SetConfig(id, config_names, config_values,
                                            size);
}

int32_t set_irq_value(int32_t id, int32_t irq_num, bool irq_value) {
  return RenodeAgent::Instance()->SetIrqValue(id, irq_num, irq_value);
}

namespace mpact {
namespace sim {
namespace util {
namespace renode {

using HaltReason = ::mpact::sim::generic::CoreDebugInterface::HaltReason;

RenodeAgent *RenodeAgent::instance_ = nullptr;
int32_t RenodeAgent::count_ = 0;

// Create the debug instance by calling the factory function.
int32_t RenodeAgent::Construct(int32_t max_name_length,
                               int32_t (*read_callback)(uint64_t, char *,
                                                        int32_t),
                               int32_t (*write_callback)(uint64_t, char *,
                                                         int32_t)) {
  std::string name = absl::StrCat("renode", count_);
  auto *memory_access = new RenodeMemoryAccess(read_callback, write_callback);
  auto *dbg = CreateMpactSim(name, memory_access);
  if (dbg == nullptr) {
    delete memory_access;
    return -1;
  }
  // Make sure that we don't reuse an instance number that may have been created
  // through Connect.
  while (core_dbg_instances_.contains(RenodeAgent::count_)) {
    RenodeAgent::count_++;
  }
  core_dbg_instances_.emplace(RenodeAgent::count_, dbg);
  name_length_map_.emplace(RenodeAgent::count_, max_name_length);
  renode_memory_access_.emplace(RenodeAgent::count_, memory_access);
  return RenodeAgent::count_++;
}

int32_t RenodeAgent::Connect(int32_t id, int32_t max_name_length,
                             int32_t (*read_callback)(uint64_t, char *,
                                                      int32_t),
                             int32_t (*write_callback)(uint64_t, char *,
                                                       int32_t)) {
  // First check if the instance already exists.
  auto iter = core_dbg_instances_.find(id);
  if (iter != core_dbg_instances_.end()) {
    // If memory callbacks are provided, don't overwrite any previous non-null
    // callbacks.
    auto *mem_access = renode_memory_access_.at(id);
    // Replace any null callbacks.
    if (!mem_access->has_read_fcn()) {
      mem_access->set_read_fcn(read_callback);
    }
    if (!mem_access->has_write_fcn()) {
      mem_access->set_write_fcn(write_callback);
    }
    return id;
  }

  // The instance does not exist, so create a new debug instance.
  std::string name = absl::StrCat("renode", id);
  auto *memory_access = new RenodeMemoryAccess(read_callback, write_callback);
  auto *dbg = CreateMpactSim(name, memory_access);
  if (dbg == nullptr) {
    delete memory_access;
    return -1;
  }
  core_dbg_instances_.emplace(id, dbg);
  name_length_map_.emplace(id, max_name_length);
  renode_memory_access_.emplace(id, memory_access);
  return id;
}

// Destroy the debug instance.
void RenodeAgent::Destroy(int32_t id) {
  // Check for valid instance.
  auto dbg_iter = core_dbg_instances_.find(id);
  // If it doesn't exist, it may already have been deleted, so just return.
  if (dbg_iter == core_dbg_instances_.end()) return;

  delete dbg_iter->second;
  core_dbg_instances_.erase(dbg_iter);

  // Delete the memory access shim.
  auto mem_iter = renode_memory_access_.find(id);
  if (mem_iter == renode_memory_access_.end()) return;
  delete mem_iter->second;
  renode_memory_access_.erase(mem_iter);
}

int32_t RenodeAgent::Reset(int32_t id) {
  // Check for valid instance.
  auto dbg_iter = core_dbg_instances_.find(id);
  if (dbg_iter == core_dbg_instances_.end()) return -1;

  // For now, do nothing.
  return 0;
}

int32_t RenodeAgent::GetRegisterInfoSize(int32_t id) const {
  // Check for valid instance.
  auto dbg_iter = core_dbg_instances_.find(id);
  if (dbg_iter == core_dbg_instances_.end()) return -1;
  auto *dbg = dbg_iter->second;
  return dbg->GetRenodeRegisterInfoSize();
}

int32_t RenodeAgent::GetRegisterInfo(int32_t id, int32_t index, char *name,
                                     RenodeCpuRegister *info) {
  // Check for valid instance.
  if (info == nullptr) return -1;
  auto dbg_iter = core_dbg_instances_.find(id);
  if (dbg_iter == core_dbg_instances_.end()) return -1;
  auto *dbg = dbg_iter->second;
  int32_t max_len = name_length_map_.at(id);
  auto result = dbg->GetRenodeRegisterInfo(index, max_len, name, *info);
  if (!result.ok()) return -1;
  return 0;
}

// Read the register given by the id.
int32_t RenodeAgent::ReadRegister(int32_t id, uint32_t reg_id,
                                  uint64_t *value) {
  // Check for valid instance.
  if (value == nullptr) return -1;
  auto dbg_iter = core_dbg_instances_.find(id);
  if (dbg_iter == core_dbg_instances_.end()) return -1;
  // Read register.
  auto *dbg = dbg_iter->second;
  auto result = dbg->ReadRegister(reg_id);
  if (!result.ok()) return -1;
  *value = result.value();
  return 0;
}

int32_t RenodeAgent::WriteRegister(int32_t id, uint32_t reg_id,
                                   uint64_t value) {
  // Check for valid instance.
  auto dbg_iter = core_dbg_instances_.find(id);
  if (dbg_iter == core_dbg_instances_.end()) return -1;
  // Write register.
  auto *dbg = dbg_iter->second;
  auto result = dbg->WriteRegister(reg_id, value);
  if (!result.ok()) return -1;
  return 0;
}

uint64_t RenodeAgent::ReadMemory(int32_t id, uint64_t address, char *buffer,
                                 uint64_t length) {
  // Get the debug interface.
  auto dbg_iter = core_dbg_instances_.find(id);
  if (dbg_iter == core_dbg_instances_.end()) {
    LOG(ERROR) << "No such core dbg instance: " << id;
    return 0;
  }
  auto *dbg = dbg_iter->second;
  auto res = dbg->ReadMemory(address, buffer, length);
  if (!res.ok()) return 0;
  return res.value();
}

uint64_t RenodeAgent::WriteMemory(int32_t id, uint64_t address,
                                  const char *buffer, uint64_t length) {
  // Get the debug interface.
  auto dbg_iter = core_dbg_instances_.find(id);
  if (dbg_iter == core_dbg_instances_.end()) {
    LOG(ERROR) << "No such core dbg instance: " << id;
    return 0;
  }
  auto *dbg = dbg_iter->second;
  auto res = dbg->WriteMemory(address, buffer, length);
  if (!res.ok()) return 0;
  return res.value();
}

uint64_t RenodeAgent::LoadExecutable(int32_t id, const char *file_name,
                                     bool for_symbols_only, int32_t *status) {
  // Get the debug interface.
  auto dbg_iter = core_dbg_instances_.find(id);
  if (dbg_iter == core_dbg_instances_.end()) {
    LOG(ERROR) << "No such core dbg instance: " << id;
    *status = -1;
    return 0;
  }
  // Instantiate loader.
  auto *dbg = dbg_iter->second;
  auto res = dbg->LoadExecutable(file_name, for_symbols_only);
  if (!res.ok()) {
    LOG(ERROR) << "Failed to load executable: " << res.status().message();
    *status = -1;
    return 0;
  }
  *status = 0;
  uint64_t entry = res.value();
  return entry;
}

int32_t RenodeAgent::LoadImage(int32_t id, const char *file_name,
                               uint64_t address) {
  // Get the debug interface.
  auto dbg_iter = core_dbg_instances_.find(id);
  if (dbg_iter == core_dbg_instances_.end()) {
    LOG(ERROR) << "No such core dbg instance: " << id;
    return -1;
  }
  auto *dbg = dbg_iter->second;
  // Open up the image file.
  std::ifstream image_file;
  image_file.open(file_name, std::ios::in | std::ios::binary);
  if (!image_file.good()) {
    LOG(ERROR) << "LoadImage: Input file not in good state";
    return -1;
  }
  char buffer[kBufferSize];
  size_t gcount = 0;
  uint64_t load_address = address;
  do {
    // Fill buffer.
    image_file.read(buffer, kBufferSize);
    // Get the number of bytes that was read.
    gcount = image_file.gcount();
    // Write to the simulator memory.
    auto res = dbg->WriteMemory(load_address, buffer, gcount);
    // Check that the write succeeded, increment address if it did.
    if (!res.ok()) {
      LOG(ERROR) << "LoadImage: Memory write failed";
      return -1;
    }
    if (res.value() != gcount) {
      LOG(ERROR) << "LoadImage: Memory write failed to write all the bytes";
      return -1;
    }
    load_address += gcount;
  } while (image_file.good() && (gcount > 0));
  return 0;
}

uint64_t RenodeAgent::Step(int32_t id, uint64_t num_to_step, int32_t *status) {
  // Get the core debug if object.
  auto *dbg = RenodeAgent::Instance()->core_dbg(id);
  // Is the debug interface valid?
  if (dbg == nullptr) {
    if (status != nullptr) {
      *status = static_cast<int32_t>(ExecutionResult::kAborted);
    }
    return 0;
  }

  if (num_to_step == 0) {
    *status = static_cast<int32_t>(ExecutionResult::kOk);
    return 0;
  }

  // Check the previous halt reason.
  auto halt_res = dbg->GetLastHaltReason();
  if (!halt_res.ok()) {
    if (status != nullptr) {
      *status = static_cast<int32_t>(ExecutionResult::kAborted);
    }
    return 0;
  }

  // If the previous halt reason was a semihost halt request, then we
  // shouldn't step any further. Just return with "waiting for interrupt"
  // code.
  if (halt_res.value() ==
      *RenodeDebugInterface::HaltReason::kSemihostHaltRequest) {
    *status = static_cast<int32_t>(ExecutionResult::kAborted);
    return 0;
  }
  // Perform the stepping.
  uint32_t total_executed = 0;
  while (num_to_step > 0) {
    // Check how far to step, and make multiple calls if the number
    // is greater than <int>::max();
    int step_count = (num_to_step > std::numeric_limits<int>::max())
                         ? std::numeric_limits<int>::max()
                         : static_cast<int>(num_to_step);
    auto res = dbg->Step(step_count);
    // An error occurred.
    if (!res.ok()) {
      if (status != nullptr) {
        *status = static_cast<int32_t>(ExecutionResult::kAborted);
      }
      return total_executed;
    }
    int num_executed = res.value();
    total_executed += num_executed;

    // Check if the execution was halted due to a semihosting halt request,
    // i.e., program exit.
    halt_res = dbg->GetLastHaltReason();
    if (!halt_res.ok()) {
      if (status != nullptr) {
        *status = static_cast<int32_t>(ExecutionResult::kAborted);
      }
      return total_executed;
    }
    if (halt_res.value() == *HaltReason::kProgramDone) {
      if (status != nullptr) {
        *status = static_cast<int32_t>(ExecutionResult::kAborted);
      }
      return total_executed;
    }
    if (halt_res.value() ==
        *RenodeDebugInterface::HaltReason::kSemihostHaltRequest) {
      if (status != nullptr) {
        *status = static_cast<uint32_t>(ExecutionResult::kAborted);
      }
      return total_executed;
    }
    // Check if the execution ended at a software breakpoint.
    if (halt_res.value() ==
        *RenodeDebugInterface::HaltReason::kSoftwareBreakpoint) {
      if (status != nullptr) {
        *status = static_cast<int32_t>(ExecutionResult::kStoppedAtBreakpoint);
      }
      return total_executed;
    }
    // If we stepped fewer instructions than anticipated, stop stepping and
    // return with no error.
    if (num_executed < step_count) {
      if (status != nullptr) {
        *status = static_cast<int32_t>(ExecutionResult::kOk);
      }
      return total_executed;
    }
    num_to_step -= num_executed;
  }
  if (status != nullptr) {
    *status = static_cast<int32_t>(ExecutionResult::kOk);
  }
  return total_executed;
}

// Set configuration item.
int32_t RenodeAgent::SetConfig(int32_t id, const char *config_names[],
                               const char *config_values[], int size) {
  // Get the core debug interface object.
  auto *dbg = RenodeAgent::Instance()->core_dbg(id);
  // Is the debug interface valid?
  if (dbg == nullptr) {
    return -1;
  }
  auto status = dbg->SetConfig(config_names, config_values, size);
  if (!status.ok()) {
    LOG(ERROR) << "SetConfig: " << status.message();
    return -1;
  }
  return 0;
}

// Set irq value.
int32_t RenodeAgent::SetIrqValue(int32_t id, int32_t irq_num, bool irq_value) {
  // Get the core debug interface object.
  auto *dbg = RenodeAgent::Instance()->core_dbg(id);
  // Is the debug interface valid?
  if (dbg == nullptr) {
    return -1;
  }
  auto status = dbg->SetIrqValue(irq_num, irq_value);
  if (!status.ok()) {
    LOG(ERROR) << "SetIrqValue: " << status.message();
    return -1;
  }
  return 0;
}

}  // namespace renode
}  // namespace util
}  // namespace sim
}  // namespace mpact
