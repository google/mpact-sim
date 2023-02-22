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

#include "mpact/sim/generic/resource_bitset.h"

#include <algorithm>
#include <limits>

#include "absl/log/log.h"
#include "absl/numeric/bits.h"

namespace mpact {
namespace sim {
namespace generic {

ResourceBitSet::ResourceBitSet(int bit_size) {
  size_ = (bit_size + kBitsInUint - 1) / kBitsInUint;
  bits_ = new UInt[size_];
  for (int i = 0; i < size_; ++i) bits_[i] = 0;
}

ResourceBitSet::ResourceBitSet(const ResourceBitSet& rhs)
    : ResourceBitSet(rhs.size_) {
  Or(rhs);
}

ResourceBitSet::ResourceBitSet(ResourceBitSet&& rhs) {
  bits_ = rhs.bits_;
  size_ = rhs.size_;
  rhs.bits_ = nullptr;
  rhs.size_ = 0;
}

ResourceBitSet& ResourceBitSet::operator=(const ResourceBitSet& rhs) {
  delete[] bits_;
  size_ = rhs.size_;
  bits_ = new UInt[size_];
  for (int i = 0; i < size_; ++i) bits_[i] = rhs.bits_[i];
  return *this;
}

ResourceBitSet::~ResourceBitSet() {
  delete[] bits_;
  size_ = 0;
}

void ResourceBitSet::Set(int position) {
  auto word = position / kBitsInUint;
  auto bit = position % kBitsInUint;
  if (word > size_) Resize(position);
  bits_[word] |= static_cast<UInt>(1) << bit;
}

void ResourceBitSet::Or(const ResourceBitSet& rhs) {
  if (rhs.size_ > size_) Resize(rhs.size_ * kBitsInUint);
  for (int i = 0; i < size_; ++i) bits_[i] |= rhs.bits_[i];
}

void ResourceBitSet::AndNot(const ResourceBitSet& rhs) {
  auto min = std::min(size_, rhs.size_);
  for (int i = 0; i < min; ++i) bits_[i] &= ~rhs.bits_[i];
}

bool ResourceBitSet::IsIntersectionNonEmpty(const ResourceBitSet& rhs) const {
  auto min = std::min(size_, rhs.size_);
  UInt accumulator = 0;
  for (int i = 0; i < min; ++i) accumulator |= bits_[i] & rhs.bits_[i];
  return accumulator != 0;
}

bool ResourceBitSet::FindFirstSetBit(int* position) const {
  for (int i = 0; i < size_; ++i) {
    if (bits_[i] == 0) continue;
    // Count the trailing zeros (position increases right to left).
    int pos = i * kBitsInUint + absl::countr_zero(bits_[i]);
    if (position != nullptr) *position = pos;
    return true;
  }
  return false;
}

bool ResourceBitSet::FindNextSetBit(int* position) const {
  if (position == nullptr) {
    LOG(ERROR) << "null position passed to FindNextSetBit";
  }
  int start_word = *position / kBitsInUint;
  int bit_position = *position % kBitsInUint;
  UInt mask = ~(std::numeric_limits<UInt>::max() << bit_position);
  for (int word = start_word; word < size_; ++word) {
    // Mask out bits before the starting position.
    UInt value = bits_[word] & ~mask;
    if (value == 0) {
      mask = 0;
      continue;
    }
    // Count the trailing zeros (position increases right to left).
    int pos = word * kBitsInUint + absl::countr_zero(value);
    *position = pos;
    return true;
  }
  return false;
}

int ResourceBitSet::GetOnesCount() const {
  int count = 0;
  for (int i = 0; i < size_; ++i) {
    count += absl::popcount(bits_[i]);
  }
  return count;
}

void ResourceBitSet::Resize(int bit_size) {
  auto new_size = (bit_size + kBitsInUint - 1) / kBitsInUint;
  // If the size is the same, or smaller, zero out the high order bits.
  if (new_size <= size_) {
    int bit_offset = bit_size % kBitsInUint;
    UInt mask = ~(std::numeric_limits<UInt>::max() << bit_offset);
    for (int i = new_size; i < size_; ++i) {
      bits_[i] = bits_[i] & ~mask;
      mask = 0;
    }
    return;
  }

  auto prev = bits_;
  bits_ = new UInt[new_size];
  for (int i = 0; i < new_size; ++i) {
    bits_[i] = (i < size_) ? prev[i] : 0;
  }
  delete[] prev;
  size_ = new_size;
}

}  // namespace generic
}  // namespace sim
}  // namespace mpact
