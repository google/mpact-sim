// Copyright 2026 Google LLC
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

// This file contains the definition of the DebugInfo base class. The DebugInfo
// class is used to provide information about the registers and target XML to
// gdbserver.

#ifndef MPACT_SIM_GENERIC_DEBUG_INFO_H_
#define MPACT_SIM_GENERIC_DEBUG_INFO_H_

#include <cstdint>
#include <string>
#include <string_view>

#include "absl/container/flat_hash_map.h"

namespace mpact::sim::generic {

class DebugInfo {
 public:
  using DebugRegisterMap = absl::flat_hash_map<uint64_t, std::string>;

  virtual ~DebugInfo() = default;

  // Returns the map of register numbers to register names. This is used so that
  // gdbserver can convert from the register numbers used by the debugger to
  // the register names used by the simulator.
  virtual const DebugRegisterMap& debug_register_map() const = 0;

  // Returns the first and last general purpose register numbers.
  virtual int GetFirstGpr() const = 0;
  virtual int GetLastGpr() const = 0;
  // Get the pc register number.
  virtual int GetPcRegister() const = 0;
  // Returns the byte width of the general purpose registers.
  virtual int GetGprWidth() const = 0;
  // Returns the byte width of the register with the given number.
  virtual int GetRegisterByteWidth(int) const = 0;
  // Returns the XML file describing the target for gdb (or lldb).
  virtual std::string_view GetGdbTargetXml() const = 0;
  // Returns the host info string for gdb/lldb.
  virtual std::string_view GetLLDBHostInfo() const = 0;
};

}  // namespace mpact::sim::generic

#endif  // MPACT_SIM_GENERIC_DEBUG_INFO_H_
