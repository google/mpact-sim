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

// This file contains the definition of the DebugInfo base class.

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

  virtual const DebugRegisterMap& debug_register_map() const = 0;

  virtual int GetFirstGpr() const = 0;
  virtual int GetLastGpr() const = 0;
  virtual int GetGprWidth() const = 0;
  virtual std::string_view GetLLDBHostInfo() const = 0;
};

}  // namespace mpact::sim::generic

#endif  // MPACT_SIM_GENERIC_DEBUG_INFO_H_
