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

#ifndef MPACT_SIM_UTIL_MEMORY_CACHE_H_
#define MPACT_SIM_UTIL_MEMORY_CACHE_H_

#include <cstdint>
#include <limits>
#include <string>

#include "absl/container/btree_set.h"
#include "absl/status/status.h"
#include "mpact/sim/generic/component.h"
#include "mpact/sim/generic/counters.h"
#include "mpact/sim/generic/ref_count.h"
#include "mpact/sim/util/memory/memory_interface.h"
#include "mpact/sim/util/memory/tagged_memory_interface.h"

// This file defines a class for modeling a cache. It implements the memory
// interface, so it can be placed on the memory access path. A cache instance
// takes a memory interface as a constructor argument, and will forward all
// memory requests to that interface, after processing the memory request as
// a cache access. This cache class can be used with both the plain and the
// tagged memory interfaces. However, it is an error to use the cache with
// a tagged memory interface if only a plan memory interface was provided
// to the constructor.
//
// The cache is configured with a separate call that passes in a configuration
// string that is parsed into the cache parameters. The configuration string
// is expected to be in the format:
//
// <cache_size>,<block_size>,<associativity>,<write_allocate>
//
// where:
// <cache_size> is the size of the cache in bytes.
// <block_size> is the size of a cache block in bytes.
// <associativity> is the number of ways in the cache.
// <write_allocate> is a boolean indicating whether write allocate is enabled.
//
// The configuration call also takes a counter as an argument. This counter is
// intended to be the cycle counter for the simulation. The cache uses this
// counter to tag the cache lines with the time of last access, in order to
// compute the LRU line upon replacement.

namespace mpact::sim::util {

using ::mpact::sim::generic::Component;
using ::mpact::sim::generic::CounterValueOutputBase;
using ::mpact::sim::generic::SimpleCounter;
using ::mpact::sim::proto::ComponentData;

// This class implements a simple cache.
class Cache : public Component, public TaggedMemoryInterface {
 public:
  // The CacheContext class is used to store information necessary to fulfill
  // the memory request when it's forwarded on to the memory interface.
  struct CacheContext : public ReferenceCount {
    // The context of the original memory reference.
    ReferenceCount *context;
    // Original data buffer.
    DataBuffer *db;
    // Instruction to be executed upon memory access completion.
    Instruction *inst;
    // Latency of the memory access.
    int latency;
    // Two constructors depending on whether the cache is used with a tagged
    // memory interface or not.
    CacheContext(ReferenceCount *context_, DataBuffer *db_, Instruction *inst_,
                 int latency_)
        : context(context_), db(db_), inst(inst_), latency(latency_) {}
  };

  // Two constructors depending on whether the cache is used with a tagged
  // memory interface or not. The constructors take three arguments, the name
  // to use for the cache, a pointer to the parent component (used to register
  // and provide access to the performance counters), and a memory interface
  // used to forward memory requests to.
  Cache(std::string name, Component *parent, MemoryInterface *memory);
  Cache(std::string name, Component *parent,
        TaggedMemoryInterface *tagged_memory);
  // Shorthand constructors that omit some parameters.
  Cache(std::string name, MemoryInterface *memory);
  Cache(std::string name, TaggedMemoryInterface *tagged_memory);
  Cache(std::string name, Component *parent);
  explicit Cache(std::string name);
  Cache() = delete;
  Cache(const Cache &) = delete;
  Cache operator=(const Cache &) = delete;
  ~Cache() override;

  // Configure the cache. The configuration string is expected to be in the
  // format:
  //
  // <cache_size>,<block_size>,<associativity>,<write_allocate>
  //
  // where:
  // <cache_size> is the size of the cache in bytes (power of 2)
  // <block_size> is the size of a cache block in bytes (power of 2).
  // <associativity> is the number of ways in the cache (0 is fully set
  // associative) (power of 2).
  // <write_allocate> is a boolean indicating whether write
  // allocate is enabled.
  //
  // cycle_counter is a pointer to a counter that counts cycles in the
  // simulation.
  absl::Status Configure(const std::string &config,
                         CounterValueOutputBase<uint64_t> *cycle_counter);

  // MemoryInterface and TaggedMemoryInterfacemethods.
  void Load(uint64_t address, DataBuffer *db, Instruction *inst,
            ReferenceCount *context) override;
  void Load(DataBuffer *address_db, DataBuffer *mask_db, int el_size,
            DataBuffer *db, Instruction *inst,
            ReferenceCount *context) override;
  void Load(uint64_t address, DataBuffer *db, DataBuffer *tags,
            Instruction *inst, ReferenceCount *context) override;
  void Store(uint64_t address, DataBuffer *db) override;
  void Store(DataBuffer *address, DataBuffer *mask, int el_size,
             DataBuffer *db) override;
  void Store(uint64_t address, DataBuffer *db, DataBuffer *tags) override;
  // Setters for the memory interfaces.
  void set_memory(MemoryInterface *memory) {
    memory_ = memory;
    tagged_memory_ = nullptr;
  }
  void set_tagged_memory(TaggedMemoryInterface *tagged_memory) {
    tagged_memory_ = tagged_memory;
    memory_ = static_cast<MemoryInterface *>(tagged_memory);
  }

 private:
  // Address range struct used as key in maps from range to callback function.
  struct AddressRange {
    uint64_t start;
    uint64_t end;
    explicit AddressRange(uint64_t address) : start(address), end(address) {}
    AddressRange(uint64_t start_address, uint64_t end_address)
        : start(start_address), end(end_address) {}
  };
  // Comparator used in maps/sets to compare two address ranges so as to be able
  // to order the keys. Note, two address ranges are "equal" (as in
  // overlapping), if neither is less than the other. In this context A range is
  // less than another if the addresses of the first are less than the addresses
  // of the second. Thus if neither is less than the other, they overlap in
  // in some way.
  struct AddressRangeLess {
    constexpr bool operator()(const AddressRange &lhs,
                              const AddressRange &rhs) const {
      return lhs.end < rhs.start;
    }
  };
  // This struct represents a cache line.
  struct CacheLine {
    // True if the line is valid.
    bool valid = false;
    // The tag includes both the index and the remaining tag bits of the
    // address.
    uint64_t tag;
    // True if the line is pinned. Pinned lines are never replaced.
    bool pinned = false;
    // True if the line is dirty. Dirty lines are written back to memory
    // upon replacement.
    bool dirty = false;
    // LRU timestamp.
    uint64_t lru = std::numeric_limits<uint64_t>::max();
  };
  // This is a semantic function that is bound to a local instruction instance
  // and is used to perform the writeback to the processor of the data that was
  // read.
  void LoadChild(const Instruction *inst);
  // Cache read/write function. Returns the number of cache misses.
  int CacheLookup(uint64_t address, int size, bool is_read);
  void ReplaceBlock(uint64_t block, bool is_read);
  // The cache.
  CacheLine *cache_lines_ = nullptr;
  // Shift amounts and mask used to compute the index from the address.
  int block_shift_ = 0;
  int set_shift_ = 0;
  uint64_t index_mask_ = 0;
  // True if allocate cache line on write is enabled.
  bool write_allocate_ = false;
  uint64_t num_sets_ = 0;
  // Cacheability ranges.
  absl::btree_multiset<AddressRange, AddressRangeLess> non_cacheable_ranges_;
  absl::btree_multiset<AddressRange, AddressRangeLess> cacheable_ranges_;
  bool has_non_cacheable_ = false;
  bool has_cacheable_ = false;
  // Instruction object used to perform the writeback to the processor.
  Instruction *cache_inst_;
  CounterValueOutputBase<uint64_t> *cycle_counter_;
  // Performance counters.
  SimpleCounter<uint64_t> read_hit_counter_;
  SimpleCounter<uint64_t> read_miss_counter_;
  SimpleCounter<uint64_t> write_hit_counter_;
  SimpleCounter<uint64_t> write_miss_counter_;
  SimpleCounter<uint64_t> dirty_line_writeback_counter_;
  SimpleCounter<uint64_t> read_around_counter_;
  SimpleCounter<uint64_t> write_around_counter_;
  SimpleCounter<uint64_t> read_non_cacheable_counter_;
  SimpleCounter<uint64_t> write_non_cacheable_counter_;
  // Memory interface pointers.
  MemoryInterface *memory_;
  TaggedMemoryInterface *tagged_memory_;
};

}  // namespace mpact::sim::util

#endif  // MPACT_SIM_UTIL_MEMORY_CACHE_H_
