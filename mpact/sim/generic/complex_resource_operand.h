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

#ifndef MPACT_SIM_GENERIC_COMPLEX_RESOURCE_OPERAND_H_
#define MPACT_SIM_GENERIC_COMPLEX_RESOURCE_OPERAND_H_

#include <string>

#include "absl/status/status.h"
#include "absl/types/span.h"
#include "mpact/sim/generic/complex_resource.h"
#include "mpact/sim/generic/resource_operand_interface.h"

namespace mpact {
namespace sim {
namespace generic {

// The ComplexResourceOperand is used in Instruction instances to schedule
// acquisition of resources over a span of cycles that generally do not start
// at cycle 0. Each instance controls the acquisition of only a single resource.
class ComplexResourceOperand : public ResourceOperandInterface {
 public:
  ComplexResourceOperand() = delete;
  explicit ComplexResourceOperand(ComplexResource *resource)
      : resource_(resource) {}

  ~ComplexResourceOperand() override { delete[] bit_array_; }

  // Set the cycle mask for when this resource needs to be available. The first
  // method sets it using begin and end cycle values.
  absl::Status SetCycleMask(size_t begin, size_t end);
  // The second method uses a uint64_t span. Any 1's beyond the cycle depth of
  // the resource will cause an error.
  absl::Status SetCycleMask(absl::Span<const uint64_t> span);

  // Returns true if the resource is available during the cycles specified in
  // the cycle mask.
  bool IsFree() override {
    return resource_->IsFree(absl::MakeSpan(bit_array_, span_size_));
  }

  // Acquires the resource for the cycles specified in the cycle mask
  void Acquire() override {
    return resource_->Acquire(absl::MakeSpan(bit_array_, span_size_));
  }

  std::string AsString() const override { return resource_->AsString(); }

  absl::Span<const uint64_t> bit_array() const {
    return absl::MakeSpan(bit_array_, span_size_);
  }

 private:
  ComplexResource *resource_ = nullptr;
  uint64_t *bit_array_ = nullptr;
  int span_size_ = 0;
};

}  // namespace generic
}  // namespace sim
}  // namespace mpact

#endif  // MPACT_SIM_GENERIC_COMPLEX_RESOURCE_OPERAND_H_
