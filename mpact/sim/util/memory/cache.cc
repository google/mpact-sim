// Copyright 2024 Google LLC
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

#include "mpact/sim/util/memory/cache.h"

#include <cstdint>
#include <limits>
#include <string>
#include <vector>

#include "absl/functional/bind_front.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/numeric/bits.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "mpact/sim/generic/component.h"
#include "mpact/sim/generic/instruction.h"
#include "mpact/sim/util/memory/memory_interface.h"
#include "mpact/sim/util/memory/tagged_memory_interface.h"

namespace mpact::sim::util {

using ::mpact::sim::generic::Component;

// Constructors.
Cache::Cache(std::string name, Component *parent, MemoryInterface *memory)
    : Component(name, parent),
      read_hit_counter_("read_hit", 0ULL),
      read_miss_counter_("read_miss", 0ULL),
      write_hit_counter_("write_hit", 0ULL),
      write_miss_counter_("write_miss", 0ULL),
      dirty_line_writeback_counter_("dirty_line_writeback", 0ULL),
      read_around_counter_("read_around", 0ULL),
      write_around_counter_("write_around", 0ULL),
      memory_(memory),
      tagged_memory_(nullptr) {
  // Register the counters.
  CHECK_OK(AddCounter(&read_hit_counter_));
  CHECK_OK(AddCounter(&read_miss_counter_));
  CHECK_OK(AddCounter(&write_hit_counter_));
  CHECK_OK(AddCounter(&write_miss_counter_));
  CHECK_OK(AddCounter(&dirty_line_writeback_counter_));
  CHECK_OK(AddCounter(&read_around_counter_));
  CHECK_OK(AddCounter(&write_around_counter_));
  cache_inst_ = new Instruction(nullptr);
  cache_inst_->set_semantic_function(absl::bind_front(&Cache::LoadChild, this));
}

Cache::Cache(std::string name, Component *parent,
             TaggedMemoryInterface *tagged_memory)
    : Component(name, parent),
      read_hit_counter_("read_hit", 0ULL),
      read_miss_counter_("read_miss", 0ULL),
      write_hit_counter_("write_hit", 0ULL),
      write_miss_counter_("write_miss", 0ULL),
      dirty_line_writeback_counter_("dirty_line_writeback", 0ULL),
      read_around_counter_("read_around", 0ULL),
      write_around_counter_("write_around", 0ULL),
      memory_(tagged_memory),
      tagged_memory_(tagged_memory) {
  // Register the counters.
  CHECK_OK(AddCounter(&read_hit_counter_));
  CHECK_OK(AddCounter(&read_miss_counter_));
  CHECK_OK(AddCounter(&write_hit_counter_));
  CHECK_OK(AddCounter(&write_miss_counter_));
  CHECK_OK(AddCounter(&dirty_line_writeback_counter_));
  CHECK_OK(AddCounter(&read_around_counter_));
  CHECK_OK(AddCounter(&write_around_counter_));
}

// The simple constructors just call the main constructors.
Cache::Cache(std::string name, MemoryInterface *memory)
    : Cache(name, nullptr, memory) {}

Cache::Cache(std::string name, TaggedMemoryInterface *tagged_memory)
    : Cache(name, nullptr, tagged_memory) {}

Cache::Cache(std::string name, Component *parent)
    : Cache(name, parent, static_cast<MemoryInterface *>(nullptr)) {}

Cache::Cache(std::string name)
    : Cache(name, static_cast<Component *>(nullptr)) {}

Cache::~Cache() {
  delete[] cache_lines_;
  cache_inst_->DecRef();
}

absl::Status Cache::Configure(const std::string &config,
                              CounterValueOutputBase<uint64_t> *cycle_counter) {
  if (cycle_counter == nullptr) {
    return absl::InvalidArgumentError("Cycle counter is null");
  }
  cycle_counter_ = cycle_counter;
  // Split the configuration string into fields, make sure there are 4 fields.
  std::vector<std::string> config_vec = absl::StrSplit(config, ',');
  if (config_vec.size() != 4) {
    return absl::InvalidArgumentError("Invalid configuration - too few fields");
  }
  // Compute the cache size in bytes, including any suffixes ('k', 'M', 'G').
  std::string::size_type size;
  uint64_t cache_size = std::stoull(config_vec[0], &size);
  if (size < config_vec[0].size()) {
    auto suffix = config_vec[0].substr(size);
    if (suffix == "k") {
      cache_size *= 1024;
    } else if (suffix == "M") {
      cache_size *= 1024 * 1024;
    } else if (suffix == "G") {
      cache_size *= 1024 * 1024 * 1024;
    } else {
      return absl::InvalidArgumentError(
          absl::StrCat("Invalid cache size suffix: '", suffix, "'"));
    }
  }
  // Sanity check the cache parameters.
  uint64_t line_size = std::stoull(config_vec[1], &size);
  if (size != config_vec[1].size()) {
    return absl::InvalidArgumentError("Invalid value for line size");
  }
  num_sets_ = std::stoull(config_vec[2], &size);
  if (size != config_vec[2].size()) {
    return absl::InvalidArgumentError("Invalid value for number of sets");
  }
  // Write allocate.
  if (config_vec[3] == "true") {
    write_allocate_ = true;
  } else if (config_vec[3] == "false") {
    write_allocate_ = false;
  } else {
    return absl::InvalidArgumentError("Invalid write allocate value");
  }
  if (!absl::has_single_bit(cache_size)) {
    return absl::InvalidArgumentError("Cache size is not a power of 2");
  }
  if (!absl::has_single_bit(line_size)) {
    return absl::InvalidArgumentError("Line size is not a power of 2");
  }
  if (num_sets_ != 0 && !absl::has_single_bit(num_sets_)) {
    return absl::InvalidArgumentError("Number of sets is not a power of 2");
  }
  if (cache_size == 0) {
    return absl::InvalidArgumentError("Cache size is zero");
  }
  if (line_size < 4) {
    return absl::InvalidArgumentError("Line size must be at least 4 bytes");
  }
  if (cache_size < line_size) {
    return absl::InvalidArgumentError("Cache size is less than line size");
  }
  // If num_sets is 0, then the cache is fully associative.
  if (num_sets_ == 0) {
    num_sets_ = cache_size / line_size;
  }
  uint64_t num_lines = cache_size / line_size;
  if (num_sets_ > num_lines) {
    return absl::InvalidArgumentError(
        "Cache associativity is greater than the number of lines");
  }
  delete[] cache_lines_;
  cache_lines_ = new CacheLine[num_lines];
  block_shift_ = absl::bit_width(line_size) - 1;
  index_mask_ = (1ULL << (absl::bit_width(num_lines / num_sets_) - 1)) - 1;
  set_shift_ = absl::bit_width(num_sets_) - 1;
  return absl::OkStatus();
}

// Each of the following memory (and tagged memory) interface methods will call
// the CacheLookup method to perform the cache access. If the access is a miss,
// the method will call the ReplaceBlock method to replace the block in the
// cache. The memory request itself will be forwarded to the memory interface
// provided to the constructor (if not nullptr).
void Cache::Load(uint64_t address, DataBuffer *db, Instruction *inst,
                 ReferenceCount *context) {
  (void)CacheLookup(address, db->size<uint8_t>(), /*is_read=*/true);
  if (memory_ == nullptr) return;

  auto *cache_context = new CacheContext(context, db, inst, db->latency());
  if (context) context->IncRef();
  if (inst) inst->IncRef();
  db->set_latency(0);
  memory_->Load(address, db, cache_inst_, cache_context);
  cache_context->DecRef();
}

void Cache::Load(DataBuffer *address_db, DataBuffer *mask_db, int el_size,
                 DataBuffer *db, Instruction *inst, ReferenceCount *context) {
  auto address_span = address_db->Get<uint64_t>();
  auto mask_span = mask_db->Get<bool>();
  for (int i = 0; i < address_db->size<uint64_t>(); i++) {
    if (mask_span[i]) {
      (void)CacheLookup(address_span[i], el_size, /*is_read=*/true);
    }
  }
  if (memory_ == nullptr) return;

  auto *cache_context = new CacheContext(context, db, inst, db->latency());
  if (context) context->IncRef();
  if (inst) inst->IncRef();
  db->set_latency(0);
  memory_->Load(address_db, mask_db, el_size, db, cache_inst_, cache_context);
  cache_context->DecRef();
}

void Cache::Load(uint64_t address, DataBuffer *db, DataBuffer *tags,
                 Instruction *inst, ReferenceCount *context) {
  (void)CacheLookup(address, db->size<uint8_t>(), /*is_read=*/true);
  if (tagged_memory_ == nullptr) return;

  auto *cache_context =
      new CacheContext(context, db, tags, inst, db->latency());
  if (context) context->IncRef();
  if (inst) inst->IncRef();
  db->set_latency(0);
  tagged_memory_->Load(address, db, tags, cache_inst_, cache_context);
  cache_context->DecRef();
}

void Cache::Store(uint64_t address, DataBuffer *db) {
  (void)CacheLookup(address, db->size<uint8_t>(), /*is_read=*/false);
  if (memory_ == nullptr) return;

  memory_->Store(address, db);
}

void Cache::Store(DataBuffer *address_db, DataBuffer *mask_db, int el_size,
                  DataBuffer *db) {
  auto address_span = address_db->Get<uint64_t>();
  auto mask_span = mask_db->Get<bool>();
  for (int i = 0; i < address_db->size<uint64_t>(); i++) {
    if (mask_span[i]) {
      (void)CacheLookup(address_span[i], el_size, /*is_read=*/false);
    }
  }
  if (memory_ == nullptr) return;

  memory_->Store(address_db, mask_db, el_size, db);
}

void Cache::Store(uint64_t address, DataBuffer *db, DataBuffer *tags) {
  (void)CacheLookup(address, db->size<uint8_t>(), /*is_read=*/false);
  if (tagged_memory_ == nullptr) return;

  tagged_memory_->Store(address, db, tags);
}

// This is the semantic function that is bound to the cache_inst_ instruction
// and is used to perform the writeback to the processor of the data that was
// read.
void Cache::LoadChild(const Instruction *inst) {
  auto *cache_context = static_cast<CacheContext *>(inst->context());
  auto *context = cache_context->context;
  auto *db = cache_context->db;
  auto *og_inst = cache_context->inst;
  // Reset the db latency to the original value.
  db->set_latency(cache_context->latency);
  if (nullptr != og_inst) {
    if (db->latency() > 0) {
      og_inst->IncRef();
      og_inst->state()->function_delay_line()->Add(db->latency(),
                                                   [og_inst, context]() {
                                                     og_inst->Execute(context);
                                                     if (context != nullptr)
                                                       context->DecRef();
                                                     og_inst->DecRef();
                                                   });
    }
    cache_context->DecRef();
    og_inst->DecRef();
  }
}

// Cache lookup function.
int Cache::CacheLookup(uint64_t address, int size, bool is_read) {
  int miss_count = 0;
  // If the size spans more than one block, perform and access for each block.
  uint64_t first_block = address >> block_shift_;
  uint64_t last_block = (address + size - 1) >> block_shift_;
  for (uint64_t block = first_block; block <= last_block; block++) {
    // Compute the cache index.
    uint64_t index = (block & index_mask_) << set_shift_;
    bool hit = false;
    // Iterate over the number of sets in the cache.
    for (int set = 0; set < num_sets_; set++) {
      CacheLine &line = cache_lines_[index + set];
      if (line.valid && line.tag == block) {
        hit = true;
        line.lru = cycle_counter_->GetValue();
        break;
      }
    }
    if (hit) {
      if (is_read) {
        read_hit_counter_.Increment(1ULL);
      } else {
        write_hit_counter_.Increment(1ULL);
      }
    } else {
      if (is_read) {
        ReplaceBlock(block, is_read);
        miss_count++;
        read_miss_counter_.Increment(1ULL);
      } else {
        ReplaceBlock(block, is_read);
        write_miss_counter_.Increment(1ULL);
        if (write_allocate_) {
          miss_count++;
        }
      }
    }
  }
  return miss_count;
}

void Cache::ReplaceBlock(uint64_t block, bool is_read) {
  // Recompute the cache index.
  uint64_t index = (block & index_mask_) << set_shift_;
  int victim = -1;
  uint64_t victim_lru = std::numeric_limits<uint64_t>::max();
  for (int set = 0; set < num_sets_; set++) {
    CacheLine &line = cache_lines_[index + set];
    // If the line is invalid, use it as the victim.
    if (!line.valid) {
      victim = index + set;
      break;
    }
    // Skip any pinned lines.
    if (line.pinned) continue;
    // See if the next line has a smaller lru timestamp, if so, make that the
    // victim.
    if (line.lru < victim_lru) {
      victim = index + set;
      victim_lru = line.lru;
    }
  }
  // If there is no victim, that means the lines were pinned, so couldn't
  // replace. In this case the miss really becomes a read/write around.
  if (victim == -1) {
    if (is_read) {
      read_around_counter_.Increment(1ULL);
    } else {
      write_around_counter_.Increment(1ULL);
    }
    return;
  }
  // Perform the replacement on the victim.
  CacheLine &line = cache_lines_[victim];
  // If the line is dirty (and valid), count the writeback.
  if (line.valid && line.dirty) {
    dirty_line_writeback_counter_.Increment(1ULL);
  }
  // Initialize the new line.
  line.valid = true;
  line.tag = block;
  line.pinned = false;
  line.dirty = false;
  line.lru = cycle_counter_->GetValue();
}

}  // namespace mpact::sim::util