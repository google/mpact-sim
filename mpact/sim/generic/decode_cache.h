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

#ifndef MPACT_SIM_GENERIC_DECODE_CACHE_H_
#define MPACT_SIM_GENERIC_DECODE_CACHE_H_

#include <cstdint>

#include "mpact/sim/generic/decoder_interface.h"
#include "mpact/sim/generic/instruction.h"

// The purpose of the decode cache is to cache the "decoded" simulator internal
// represenations of target instructions so that the runtime cost of decoding
// an instruction can be amortized over many instruction executions. The decode
// cache is agnostic with respect to the type and format of the actual source
// instruction encoding.

namespace mpact {
namespace sim {
namespace generic {

// This structure contains properties used to configure the decode cache. The
// minimum number of cached instructions and the minimum possible pc increment.
// The actual size of the decode cache will be the smallest power of two that
// is greater or equal to the minimum number of entries. The decode cache is
// currently only direct mapped, but different organizations may be permitted
// in the future.
struct DecodeCacheProperties {
  // Actual size will be the power of two that is >= num_entries.
  uint32_t num_entries;
  // Minimum pc increment should be a power of two.
  uint32_t minimum_pc_increment;
};

// Instructions are decoded into an internal representation for the simulator.
// Because the decode process can be slow (at least there's no requirement that
// it be fast), the decoded internal represenations are cached in a DecodeCache.
// The organization of the decode cache is specified by a DecodeCacheProperties
// struct which is passed in to the DecodeCache constructor.
//
// Instruction decode invalidation is supported, for the whole decode cache,
// an address range in the decode cache, or for a single instruction address.
// The instructions are reference counted, so while an instruction may be in
// the process of being executed, its internal representation will not be
// deleted until the execution completes with a DecRef.
class DecodeCache {
 private:
  // Constructed using static factory method.
  DecodeCache(const DecodeCacheProperties &props, DecoderInterface *decoder);

 public:
  DecodeCache() = delete;
  virtual ~DecodeCache();
  // The DecodeCache factory method takes the property struct and the interface
  // to a decoder that will supply an instruction internal representation for
  // a given address. If memory allocation fails it returns nullptr.
  static DecodeCache *Create(const DecodeCacheProperties &props,
                             DecoderInterface *decoder);
  // Returns the decoded instruction associated with the given address. If there
  // is not an instruction with that address in the decode cache, the decoder
  // will be called and the newly decoded instruction will be cached.
  Instruction *GetDecodedInstruction(uint64_t address);
  // Invalidation operations
  // These methods cases the removal from the cache of the instruction
  // internal represenations that match the address, address range [start, end),
  // or the entire cache. Each instruction that is removed from the cache is
  // DecRef'ed.
  void Invalidate(uint64_t address);
  void InvalidateRange(uint64_t start_address, uint64_t end_address);
  void InvalidateAll();

  // Accessors.
  int num_entries() const { return num_entries_; }
  uint64_t address_mask() const { return address_mask_; }
  int address_shift() const { return address_shift_; }
  int address_inc() const { return address_inc_; }

 private:
  DecoderInterface *decoder_;
  int num_entries_;
  int address_shift_;
  uint32_t address_inc_;
  uint64_t address_mask_;
  Instruction **instruction_cache_;
};

}  // namespace generic
}  // namespace sim
}  // namespace mpact

#endif  // MPACT_SIM_GENERIC_DECODE_CACHE_H_
