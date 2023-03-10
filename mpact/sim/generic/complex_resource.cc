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

#include "mpact/sim/generic/complex_resource.h"

#include <algorithm>
#include <string>

#include "absl/status/status.h"

namespace mpact {
namespace sim {
namespace generic {

constexpr size_t kNumBitsPerWord = sizeof(uint64_t) * 8;
constexpr size_t kLowBitMask = kNumBitsPerWord - 1;

ComplexResource::ComplexResource(ArchState *state, std::string name,
                                 size_t cycle_depth)
    : state_(state), name_(name), cycle_depth_(cycle_depth) {
  array_size_ = (cycle_depth_ + kNumBitsPerWord - 1) / kNumBitsPerWord;
  bit_array_ = new uint64_t[array_size_];
  memset(bit_array_, 0, 8 * array_size_);
  mask_array_ = new uint64_t[array_size_];
  memset(mask_array_, 0xff, 8 * array_size_);
  size_t mod = cycle_depth_ & kLowBitMask;
  if (mod > 0) {
    // Clear bits outside the cycle_depth from the last mask.
    mask_array_[array_size_ - 1] >>= kNumBitsPerWord - mod;
  }
}

ComplexResource::~ComplexResource() {
  delete[] bit_array_;
  delete[] mask_array_;
}

// Shift the cycle vector by the number of cycles since it was last shifted.
void ComplexResource::Advance() {
  uint64_t now = state_->cycle();
  uint64_t cycles = now - last_cycle_;
  last_cycle_ = now;
  // If n is greater than the number of cycles tracked, then just clear the
  // array and return.
  if (cycles > cycle_depth_) {
    memset(bit_array_, 0, 8 * array_size_);
    return;
  }
  int num_longwords = cycles >> 6;
  // If shift amount is greater than 64.
  if (num_longwords > 0) {
    for (unsigned i = 0; i < array_size_; i++) {
      unsigned index = i + num_longwords;
      bit_array_[i] = (index < array_size_) ? bit_array_[index] : 0;
    }
  }
  cycles &= kLowBitMask;
  // If cycles is now zero, return (i.e., if cycles was a multiple of 64).
  if (cycles == 0) return;

  // The number of words we have to shift within the array. Anything beyond
  // bit_array_[max - 1] was zeroed in the previous step.
  unsigned max = array_size_ - num_longwords;
  for (unsigned i = 0; i < max; i++) {
    // For each array word, shift right by the number of remaining cycles to
    // advance. This gets rid of resource reservations from 0..cycles - 1. Then
    // or in the low "cycles" bits from the next word into the high "cycles"
    // bits of the current. Get those bits from the next word by shifting it
    // left by 64 - cycles.
    uint64_t bits = (i + 1u < array_size_)
                        ? bit_array_[i + 1] << (kNumBitsPerWord - cycles)
                        : 0;
    bit_array_[i] >>= cycles;
    bit_array_[i] |= bits;
  }
}

bool ComplexResource::IsFree(absl::Span<const uint64_t> bit_span) {
  // Shift the cycle vector if needed.
  if (state_->cycle() != last_cycle_) Advance();
  // Since the input span may be shorter than the full vector size, only
  // compare the bits that matter.
  int min = std::min(bit_span.length(), array_size_);
  for (int i = 0; i < min; i++) {
    if (bit_span[i] & bit_array_[i]) return false;
  }
  return true;
}

void ComplexResource::Acquire(absl::Span<const uint64_t> bit_span) {
  // Shift the cycle vector if needed.
  if (state_->cycle() != last_cycle_) Advance();
  // Since the input span may be shorter than the full vector size, only
  // consider the bits that matter.
  int min = std::min(bit_span.length(), array_size_);
  for (int i = 0; i < min; i++) {
    bit_array_[i] |= (bit_span[i] & mask_array_[i]);
  }
}

void ComplexResource::Release(absl::Span<const uint64_t> bit_span) {
  // Shift the cycle vector if needed.
  if (state_->cycle() != last_cycle_) Advance();
  // Since the input span may be shorter than the full vector size, only
  // consider the bits that matter.
  int min = std::min(bit_span.length(), array_size_);
  for (int i = 0; i < min; i++) {
    bit_array_[i] &= ~(bit_span[i] & mask_array_[i]);
  }
}

}  // namespace generic
}  // namespace sim
}  // namespace mpact
