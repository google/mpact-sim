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

#include "mpact/sim/generic/complex_resource_operand.h"

#include "absl/status/status.h"

namespace mpact {
namespace sim {
namespace generic {

// Sets the bits in the bit_array_ array for the cycles, starting at begin and
// ending at end. Bits are counted from lsb to msb [(63..0), (127..64), (191,
// 128),..] etc.
absl::Status ComplexResourceOperand::SetCycleMask(size_t begin, size_t end) {
  // If the resource is null return an error.
  if (resource_ == nullptr) {
    return absl::InternalError("Resource is null in ComplexResourceOperand");
  }
  // If the start cycle is after the end cycle, return an error.
  if (begin > end) {
    return absl::InvalidArgumentError(absl::StrCat(
        "Begin cycle (", begin, ") is greater than end cycle (", end, ")"));
  }
  // If the end cycle is beyond the cycle depth of the resource, return an
  // error.
  if (end >= resource_->cycle_depth()) {
    return absl::InvalidArgumentError(absl::StrCat(
        "ComplexResourceOperand for resource '", resource_->name(), "': end(",
        end, ") is greater than cycle depth (", resource_->cycle_depth()));
  }
  // Compute the size of the array needed.
  span_size_ = (end + 63) / 64;
  // Delete any previous allocation.
  delete[] bit_array_;
  // Allocate the array and clear the bytes.
  bit_array_ = new uint64_t[span_size_];
  memset(bit_array_, 0, span_size_ * 8);
  // Set the bits in [begin, end].
  for (size_t i = begin; i <= end; i++) {
    int word_index = i >> 6;
    int bit_index = i & 0x3f;
    bit_array_[word_index] |= 1ULL << bit_index;
  }
  return absl::OkStatus();
}

absl::Status ComplexResourceOperand::SetCycleMask(
    absl::Span<const uint64_t> span) {
  // If the resource is null, return an error.
  if (resource_ == nullptr) {
    return absl::InternalError("Resource is null in ComplexResourceOperand");
  }
  // If the span is too long, return an error.
  if (resource_->bit_array().size() < span.size()) {
    return absl::InvalidArgumentError("Span too long for cycle mask");
  }

  span_size_ = span.size();
  // See if there are any bits set beyond the cycle depth of the resource.
  size_t mod = resource_->cycle_depth() & 0x3f;
  if (span[span_size_ - 1] >> mod) {
    return absl::InvalidArgumentError(
        absl::StrCat("Bits set beyond the cycle depth of resource '",
                     resource_->name(), "'"));
  }
  // Make sure that there are some bits set in the span.
  uint64_t or_value = 0;
  for (uint64_t value : span) {
    or_value |= value;
  }
  if (or_value == 0) {
    return absl::InvalidArgumentError("No bits set in input span");
  }
  // Delete any previous allocation.
  delete[] bit_array_;
  // Allocate the array and copy the span.
  bit_array_ = new uint64_t[span_size_];
  for (int i = 0; i < span_size_; i++) {
    bit_array_[i] = span[i];
  }
  return absl::OkStatus();
}

}  // namespace generic
}  // namespace sim
}  // namespace mpact
