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

#include <unistd.h>

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
      read_non_cacheable_counter_("read_non_cacheable", 0ULL),
      write_non_cacheable_counter_("write_non_cacheable", 0ULL),
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
  CHECK_OK(AddCounter(&read_non_cacheable_counter_));
  CHECK_OK(AddCounter(&write_non_cacheable_counter_));
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
  CHECK_OK(AddCounter(&read_non_cacheable_counter_));
  CHECK_OK(AddCounter(&write_non_cacheable_counter_));
  cache_inst_ = new Instruction(nullptr);
  cache_inst_->set_semantic_function(absl::bind_front(&Cache::LoadChild, this));
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
  if (config_vec.size() < 4) {
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
  // Line size in bytes.
  uint64_t line_size = std::stoull(config_vec[1], &size);
  if (size != config_vec[1].size()) {
    return absl::InvalidArgumentError("Invalid value for line size");
  }
  // Number of sets (set associativity) - 0 means fully set associative.
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
  // If there are more than 4 entries, check for cacheable, or non-cacheable
  // memory ranges. Format is: [c|nc]:<start_address>:<size>
  for (int i = 4; i < config_vec.size(); i++) {
    std::vector<std::string> range_vec = absl::StrSplit(config_vec[i], ':');
    if (range_vec.size() != 3) {
      return absl::InvalidArgumentError(
          "Invalid (non)cacheable range - must have 3 fields");
    }
    uint64_t range_start = std::stoull(range_vec[1], &size, 0);
    if (size != range_vec[1].size()) {
      return absl::InvalidArgumentError(
          "Invalid cacheable range - invalid start address");
    }
    uint64_t range_end = std::stoull(range_vec[2], &size, 0);
    if (size != range_vec[2].size()) {
      return absl::InvalidArgumentError(
          "Invalid cacheable range - invalid size");
    }
    if (range_start > range_end) {
      return absl::InvalidArgumentError(
          "Invalid cacheable range - start address is greater than end "
          "address");
    }
    if ((range_vec[0] != "c") && (range_vec[0] != "nc")) {
      return absl::InvalidArgumentError(
          "Invalid cacheable range - must start with 'c' or 'nc'");
    }
    if (range_vec[0] == "c") {
      if (!non_cacheable_ranges_.empty()) {
        return absl::InvalidArgumentError(
            "Cannot mix cacheable and non-cacheable ranges");
      }
      cacheable_ranges_.insert(AddressRange(range_start, range_end));
      has_cacheable_ = true;
    } else {
      if (!cacheable_ranges_.empty()) {
        return absl::InvalidArgumentError(
            "Cannot mix cacheable and non-cacheable ranges");
      }
      non_cacheable_ranges_.insert(AddressRange(range_start, range_end));
      has_non_cacheable_ = true;
    }
  }
  // Sanity check more cache parameters.
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
  db->IncRef();
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
  db->IncRef();
  db->set_latency(0);
  memory_->Load(address_db, mask_db, el_size, db, cache_inst_, cache_context);
  cache_context->DecRef();
}

void Cache::Load(uint64_t address, DataBuffer *db, DataBuffer *tags,
                 Instruction *inst, ReferenceCount *context) {
  // Since db can be nullptr (for a tag only load), the size and latency may
  // have to be computed differently. For size, base it on the number of tags
  // that are being loaded. For latency, use 0.
  int size = 0;
  if (db == nullptr) {
    int num_tags = tags->size<uint8_t>();
    size = (address & ~0x7ULL) + (num_tags << 3) - address;
  } else {
    size = db->size<uint8_t>();
  }
  int latency = (db == nullptr) ? 0 : db->latency();

  (void)CacheLookup(address, size, /*is_read=*/true);
  if (tagged_memory_ == nullptr) return;

  if (inst == nullptr) {  // Just forward and return.
    tagged_memory_->Load(address, db, tags, /*inst=*/nullptr, context);
    return;
  }

  auto *cache_context = new CacheContext(context, db, inst, latency);
  if (context) context->IncRef();
  if (inst) inst->IncRef();
  if (db) {
    db->set_latency(0);
    db->IncRef();
  }
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
  auto *og_context = cache_context->context;
  auto *db = cache_context->db;
  auto *og_inst = cache_context->inst;
  // Reset the db latency to the original value.
  if (nullptr != og_inst) {
    if (cache_context->latency > 0) {
      if (db != nullptr) db->set_latency(cache_context->latency);
      og_inst->state()->function_delay_line()->Add(
          db->latency(), [og_inst, og_context]() {
            og_inst->Execute(og_context);
            if (og_context != nullptr) og_context->DecRef();
            og_inst->DecRef();
          });
    } else {
      og_inst->Execute(og_context);
      if (og_context != nullptr) og_context->DecRef();
      og_inst->DecRef();
    }
  }
  if (db) db->DecRef();
}

// Cache lookup function.
int Cache::CacheLookup(uint64_t address, int size, bool is_read) {
  int miss_count = 0;
  // If the size spans more than one block, perform and access for each block.
  uint64_t first_block = address >> block_shift_;
  uint64_t last_block = (address + size - 1) >> block_shift_;
  for (uint64_t block = first_block; block <= last_block; block++) {
    // Check each access to see if it is cachaeable or not.
    if (has_cacheable_ || has_non_cacheable_) {
      bool dont_fetch =
          (has_cacheable_ && !cacheable_ranges_.contains(
                                 AddressRange(address, address + size - 1))) ||
          (has_non_cacheable_ && non_cacheable_ranges_.contains(AddressRange(
                                     address, address + size - 1)));
      if (dont_fetch) {
        // Perform read/write-around.
        if (is_read) {
          read_non_cacheable_counter_.Increment(1ULL);
        } else {
          write_non_cacheable_counter_.Increment(1ULL);
        }
        // Skip the below since it's not cacheable.
        continue;
      }
    }

    // Compute the cache index.
    uint64_t index = (block & index_mask_) << set_shift_;
    bool hit = false;
    // Iterate over the number of sets in the cache.
    for (int set = 0; set < num_sets_; set++) {
      CacheLine &line = cache_lines_[index + set];
      if (line.valid && line.tag == block) {
        hit = true;
        line.lru = cycle_counter_->GetValue();
        line.dirty |= !is_read;
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
        ReplaceBlock(block, /*is_read=*/true);
        miss_count++;
        read_miss_counter_.Increment(1ULL);
      } else {
        write_miss_counter_.Increment(1ULL);
        if (write_allocate_) {
          ReplaceBlock(block, /*is_read=*/false);
          miss_count++;
        } else {
          write_around_counter_.Increment(1ULL);
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
