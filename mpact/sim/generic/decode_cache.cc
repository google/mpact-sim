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

#include "mpact/sim/generic/decode_cache.h"

#include <cstdint>

#include "absl/numeric/bits.h"

namespace mpact {
namespace sim {
namespace generic {

DecodeCache::DecodeCache(const DecodeCacheProperties &props,
                         DecoderInterface *decoder)
    : decoder_(decoder), instruction_cache_(nullptr) {
  num_entries_ = absl::bit_ceil(props.num_entries);
  address_shift_ =
      (absl::bit_width(static_cast<uint32_t>(props.minimum_pc_increment)) - 1);
  address_inc_ = 1 << address_shift_;
  if (address_inc_ != props.minimum_pc_increment) {
    // minimum pc increment not a power of 2.
    instruction_cache_ = nullptr;
    return;
  }
  address_mask_ = (num_entries_ - 1) << address_shift_;

  instruction_cache_ = new Instruction *[num_entries_];
  if (nullptr == instruction_cache_) {
    // memory allocation failed
    return;
  }

  for (int entry = 0; entry < num_entries_; ++entry) {
    instruction_cache_[entry] = nullptr;
  }
}

DecodeCache::~DecodeCache() {
  InvalidateAll();
  delete[] instruction_cache_;
  instruction_cache_ = nullptr;
}

DecodeCache *DecodeCache::Create(const DecodeCacheProperties &props,
                                 DecoderInterface *decoder) {
  DecodeCache *dc = new DecodeCache(props, decoder);
  if (nullptr == dc->instruction_cache_) {
    delete dc;
    return nullptr;
  }
  return dc;
}

Instruction *DecodeCache::GetDecodedInstruction(uint64_t address) {
  // Verify that the instruction_cache_ was allocated
  if (nullptr == instruction_cache_) {
    return nullptr;
  }

  uint64_t indx = (address & address_mask_) >> address_shift_;
  Instruction *inst = instruction_cache_[indx];

  if ((nullptr != inst) && (inst->address() == address)) {
    return inst;
  }

  Instruction *new_inst = decoder_->DecodeInstruction(address);

  if (nullptr == new_inst) {
    return nullptr;
  }

  if (nullptr != inst) {
    inst->DecRef();
  }

  instruction_cache_[indx] = new_inst;

  return new_inst;
}

void DecodeCache::Invalidate(uint64_t address) {
  uint64_t entry = (address & address_mask_) >> address_shift_;
  Instruction *inst = instruction_cache_[entry];
  if ((nullptr != inst) && (inst->address() == address)) {
    instruction_cache_[entry]->DecRef();
    instruction_cache_[entry] = nullptr;
  }
}

void DecodeCache::InvalidateRange(uint64_t start_address,
                                  uint64_t end_address) {
  for (auto loc = start_address; loc < end_address; loc += address_inc_) {
    Invalidate(loc);
  }
}

void DecodeCache::InvalidateAll() {
  for (int entry = 0; entry < num_entries_; ++entry) {
    if (instruction_cache_[entry] != nullptr) {
      instruction_cache_[entry]->DecRef();
      instruction_cache_[entry] = nullptr;
    }
  }
}

}  // namespace generic
}  // namespace sim
}  // namespace mpact
