// Copyright 2023 Google LLC
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

#ifndef MPACT_SIM_GENERIC_COMPLEX_RESOURCE_H_
#define MPACT_SIM_GENERIC_COMPLEX_RESOURCE_H_

#include <cstddef>
#include <cstdint>
#include <string>

#include "absl/types/span.h"
#include "mpact/sim/generic/arch_state.h"

// The complex resource is a resource that is not reserved continuously from
// the beginning of the execution of an instruction, but for one or more cycles
// during the execution. The resource can be reserved ahead of time, thus there
// is a need to maintain a vector of free/used bits. While simple resources can
// be grouped into a "horizontal" vector of free/used bits across resources,
// a complex resource each represents a "vertical" vector of free/used bits
// across cycles, and therefore cannot be grouped with other complex resources.
// Instead, each complex resource must be acquired/released individually.
// This should be fine, as the number of complex resources should be relatively
// manageable, and in general less common that simple resources.
// Since the complex resource uses a cycle vector, it needs to be shifted
// appropriately as time advances. Instead of advancing the resource on each
// cycle, which may not be needed, it is instead advanced by the number of
// cycles since last Acquire/Release/IsFree operation.

namespace mpact {
namespace sim {
namespace generic {

class ComplexResource {
 public:
  static inline const int kMaxDepth = 512;

  ComplexResource(ArchState* state, std::string name, size_t cycle_depth);
  ComplexResource() = delete;
  ~ComplexResource();

  // Returns true if the resource is free for the cycles in the vector that are
  // set to 1.
  bool IsFree(absl::Span<const uint64_t> bit_span);
  // Acquires the resource for the cycles specified by bit_span.
  void Acquire(absl::Span<const uint64_t> bit_span);
  // Release the resource for the cycles specified by the bit_span.
  void Release(absl::Span<const uint64_t> bit_span);
  // Return printable representation.
  std::string AsString() const { return name(); }
  // Accessors.
  const std::string& name() const { return name_; }
  absl::Span<uint64_t> bit_array() const {
    return absl::MakeSpan(bit_array_, array_size_);
  }
  size_t cycle_depth() const { return cycle_depth_; }

 private:
  // Advance the cycle vector to current time.
  void Advance();
  // Pointer to the arch state. This is needed to obtain current time.
  ArchState* state_;
  // Name of resource.
  std::string name_;
  // Last time the cycle vector was shifted.
  uint64_t last_cycle_ = 0;
  // How many cycles are kept in the vector.
  size_t cycle_depth_;
  // How many uint64_t elements in the array.
  size_t array_size_;
  // Pointer to the arrays.
  uint64_t* bit_array_ = nullptr;
  uint64_t* mask_array_ = nullptr;
};

}  // namespace generic
}  // namespace sim
}  // namespace mpact

#endif  // MPACT_SIM_GENERIC_COMPLEX_RESOURCE_H_
