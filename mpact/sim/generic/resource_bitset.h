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

#ifndef MPACT_SIM_GENERIC_RESOURCE_BITSET_H_
#define MPACT_SIM_GENERIC_RESOURCE_BITSET_H_

#include <cstdint>

namespace mpact {
namespace sim {
namespace generic {

// This files defines the class for a limited bitset type that allows for easy
// union, set difference, and determining if the intersection of two sets is
// empty. The bitset can be resized as well.

class ResourceBitSet {
 public:
  ResourceBitSet() = default;
  ResourceBitSet(const ResourceBitSet &rhs);
  ResourceBitSet(ResourceBitSet &&rhs);
  ResourceBitSet &operator=(const ResourceBitSet &rhs);
  explicit ResourceBitSet(int bit_size);
  ~ResourceBitSet();

  // Set bit at the given position.
  void Set(int position);
  // Add rhs bitset content to this. If the size of rhs is greater than this,
  // resize this to match. If the size of this is larger than rhs, the bits not
  // in rhs are assumed to be zero.
  void Or(const ResourceBitSet &rhs);
  // Remove rhs bitset content from this. If the size of rhs is greater than
  // this, the additional bits are ignored. If this is larger than rhs, the
  // missing rhs bits are assumed to be zero.
  void AndNot(const ResourceBitSet &rhs);
  // Return true if the bitsets have a non-empty intersection. If either is
  // larger than the other, the "missing" bits are assumed to be zero.
  bool IsIntersectionNonEmpty(const ResourceBitSet &rhs) const;
  // Locate the first bit set.  If there is no such bit
  // return false, otherwise, return true and set *position to the position of
  // that bit.
  bool FindFirstSetBit(int *position) const;
  // Locate the first bit set at or after *position. If there is no such bit
  // return false, otherwise, return true and set *position to the position of
  // that bit.
  bool FindNextSetBit(int *position) const;
  // Return the number of set bits.
  int GetOnesCount() const;
  // Make the bitsize at least size bits long.
  void Resize(int bit_size);

 private:
  using UInt = uint64_t;
  static constexpr int kBitsInUint = sizeof(UInt) * 8;
  UInt *bits_ = nullptr;
  int size_ = 0;
};

}  // namespace generic
}  // namespace sim
}  // namespace mpact

#endif  // MPACT_SIM_GENERIC_RESOURCE_BITSET_H_
