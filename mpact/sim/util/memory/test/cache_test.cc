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

#include "absl/log/check.h"
#include "absl/status/status.h"
#include "googlemock/include/gmock/gmock.h"  // IWYU pragma: keep
#include "googletest/include/gtest/gtest.h"
#include "mpact/sim/generic/counters.h"
#include "mpact/sim/generic/data_buffer.h"
#include "mpact/sim/util/memory/flat_demand_memory.h"
#include "mpact/sim/util/memory/tagged_flat_demand_memory.h"

namespace {

using ::mpact::sim::generic::DataBuffer;
using ::mpact::sim::generic::DataBufferFactory;
using ::mpact::sim::generic::SimpleCounter;
using ::mpact::sim::util::Cache;
using ::mpact::sim::util::FlatDemandMemory;
using ::mpact::sim::util::TaggedFlatDemandMemory;

constexpr unsigned kTagGranule = 16;

class CacheTest : public testing::Test {
 protected:
  CacheTest() : cycle_counter_("cycle_counter", 0ULL) {
    // Create a cache 16kB, 16B blocks, direct mapped.
    cache_ = new Cache("cache");
    db_ = db_factory_.Allocate<uint32_t>(1);
    db_->set_latency(0);
    read_hits_ = reinterpret_cast<SimpleCounter<uint64_t>*>(
        cache_->GetCounter("read_hit"));
    read_misses_ = reinterpret_cast<SimpleCounter<uint64_t>*>(
        cache_->GetCounter("read_miss"));
    write_hits_ = reinterpret_cast<SimpleCounter<uint64_t>*>(
        cache_->GetCounter("write_hit"));
    write_misses_ = reinterpret_cast<SimpleCounter<uint64_t>*>(
        cache_->GetCounter("write_miss"));
    dirty_line_writebacks_ = reinterpret_cast<SimpleCounter<uint64_t>*>(
        cache_->GetCounter("dirty_line_writeback"));
    read_arounds_ = reinterpret_cast<SimpleCounter<uint64_t>*>(
        cache_->GetCounter("read_around"));
    write_arounds_ = reinterpret_cast<SimpleCounter<uint64_t>*>(
        cache_->GetCounter("write_around"));
    read_non_cacheable_ = reinterpret_cast<SimpleCounter<uint64_t>*>(
        cache_->GetCounter("read_non_cacheable"));
    write_non_cacheable_ = reinterpret_cast<SimpleCounter<uint64_t>*>(
        cache_->GetCounter("write_non_cacheable"));
  }

  ~CacheTest() override {
    delete cache_;
    db_->DecRef();
  }

  DataBufferFactory db_factory_;
  DataBuffer* db_;
  Cache* cache_;
  SimpleCounter<uint64_t>* read_hits_;
  SimpleCounter<uint64_t>* read_misses_;
  SimpleCounter<uint64_t>* write_hits_;
  SimpleCounter<uint64_t>* write_misses_;
  SimpleCounter<uint64_t>* dirty_line_writebacks_;
  SimpleCounter<uint64_t>* read_arounds_;
  SimpleCounter<uint64_t>* write_arounds_;
  SimpleCounter<uint64_t>* read_non_cacheable_;
  SimpleCounter<uint64_t>* write_non_cacheable_;
  SimpleCounter<uint64_t> cycle_counter_;
};

TEST_F(CacheTest, DirectMappedReadsCold) {
  // Create a cache 16kB, 16B blocks, direct mapped.
  CHECK_OK(cache_->Configure("1k,16,1,true", &cycle_counter_));

  for (uint64_t address = 0; address < 1024; address += 4) {
    cache_->Load(address, db_, nullptr, nullptr);
  }
  uint64_t refs = 1024 / 4;
  EXPECT_EQ(read_misses_->GetValue(), refs / 4);
  EXPECT_EQ(read_hits_->GetValue(), (refs / 4) * 3);
}

TEST_F(CacheTest, DirectMappedWritesCold) {
  // Create a cache 16kB, 16B blocks, direct mapped.
  CHECK_OK(cache_->Configure("1k,16,1,true", &cycle_counter_));

  for (uint64_t address = 0; address < 1024; address += 4) {
    cache_->Store(address, db_);
  }
  uint64_t refs = 1024 / 4;
  EXPECT_EQ(write_misses_->GetValue(), refs / 4);
  EXPECT_EQ(write_hits_->GetValue(), (refs / 4) * 3);
}

TEST_F(CacheTest, DirectMappedReadsWarm) {
  // Create a cache 16kB, 16B blocks, direct mapped.
  CHECK_OK(cache_->Configure("1k,16,1,true", &cycle_counter_));

  // Warm the cache.
  for (uint64_t address = 0; address < 1024; address += 4) {
    cache_->Load(address, db_, nullptr, nullptr);
  }
  // Clear the counters.
  read_misses_->SetValue(0);
  read_hits_->SetValue(0);

  // Access the cache again. Should be all hits.
  for (uint64_t address = 0; address < 1024; address += 4) {
    cache_->Load(address, db_, nullptr, nullptr);
  }
  uint64_t refs = 1024 / 4;
  EXPECT_EQ(read_misses_->GetValue(), 0);
  EXPECT_EQ(read_hits_->GetValue(), refs);

  // Clear the counters.
  read_misses_->SetValue(0);
  read_hits_->SetValue(0);
  // Access the next 1k, should be like a cold cache.
  for (uint64_t address = 1024; address < 2048; address += 4) {
    cache_->Load(address, db_, nullptr, nullptr);
  }
  EXPECT_EQ(read_misses_->GetValue(), refs / 4);
  EXPECT_EQ(read_hits_->GetValue(), (refs / 4) * 3);
}

TEST_F(CacheTest, DirectMappedWritesWarm) {
  // Create a cache 16kB, 16B blocks, direct mapped.
  CHECK_OK(cache_->Configure("1k,16,1,true", &cycle_counter_));

  // Warm the cache.
  for (uint64_t address = 0; address < 1024; address += 4) {
    cache_->Store(address, db_);
  }
  // Clear the counters.
  write_misses_->SetValue(0);
  write_hits_->SetValue(0);

  // Access the cache again. Should be all hits.
  for (uint64_t address = 0; address < 1024; address += 4) {
    cache_->Store(address, db_);
  }
  uint64_t refs = 1024 / 4;
  EXPECT_EQ(write_misses_->GetValue(), 0);
  EXPECT_EQ(write_hits_->GetValue(), refs);

  // Clear the counters.
  write_misses_->SetValue(0);
  write_hits_->SetValue(0);
  // Access the next 1k, should be like a cold cache.
  for (uint64_t address = 1024; address < 2048; address += 4) {
    cache_->Store(address, db_);
  }
  EXPECT_EQ(write_misses_->GetValue(), refs / 4);
  EXPECT_EQ(write_hits_->GetValue(), (refs / 4) * 3);
}

TEST_F(CacheTest, TwoWayReads) {
  // Create a cache 16kB, 16B blocks, two way set associative.
  CHECK_OK(cache_->Configure("1k,16,2,true", &cycle_counter_));
  // Fill half the cache.
  for (uint64_t address = 0; address < 512; address += 16) {
    cache_->Load(address, db_, nullptr, nullptr);
    cache_->Load(address + 1024, db_, nullptr, nullptr);
  }
  EXPECT_EQ(read_misses_->GetValue(), 2 * 512 / 16);
  EXPECT_EQ(read_hits_->GetValue(), 0);
  // Clear the counters.
  read_misses_->SetValue(0);
  read_hits_->SetValue(0);
  // All these references should hit.
  for (uint64_t address = 0; address < 512; address += 16) {
    cache_->Load(address, db_, nullptr, nullptr);
    cache_->Load(address + 1024, db_, nullptr, nullptr);
  }
  EXPECT_EQ(read_misses_->GetValue(), 0);
  EXPECT_EQ(read_hits_->GetValue(), 2 * 512 / 16);
}

TEST_F(CacheTest, MemoryTest) {
  FlatDemandMemory memory;
  cache_->set_memory(&memory);
  CHECK_OK(cache_->Configure("1k,16,1,true", &cycle_counter_));

  DataBuffer* st_db1 = db_factory_.Allocate<uint8_t>(1);
  DataBuffer* st_db2 = db_factory_.Allocate<uint16_t>(1);
  DataBuffer* st_db4 = db_factory_.Allocate<uint32_t>(1);
  DataBuffer* st_db8 = db_factory_.Allocate<uint64_t>(1);

  st_db1->Set<uint8_t>(0, 0x0F);
  st_db2->Set<uint16_t>(0, 0xA5A5);
  st_db4->Set<uint32_t>(0, 0xDEADBEEF);
  st_db8->Set<uint64_t>(0, 0x0F0F0F0F'A5A5A5A5);

  cache_->Store(0x1000, st_db1);
  cache_->Store(0x1002, st_db2);
  cache_->Store(0x1004, st_db4);
  cache_->Store(0x1008, st_db8);

  DataBuffer* ld_db1 = db_factory_.Allocate<uint8_t>(1);
  DataBuffer* ld_db2 = db_factory_.Allocate<uint16_t>(1);
  DataBuffer* ld_db4 = db_factory_.Allocate<uint32_t>(1);
  DataBuffer* ld_db8 = db_factory_.Allocate<uint64_t>(1);

  ld_db1->set_latency(0);
  ld_db2->set_latency(0);
  ld_db4->set_latency(0);
  ld_db8->set_latency(0);

  cache_->Load(0x1000, ld_db1, nullptr, nullptr);
  cache_->Load(0x1002, ld_db2, nullptr, nullptr);
  cache_->Load(0x1004, ld_db4, nullptr, nullptr);
  cache_->Load(0x1008, ld_db8, nullptr, nullptr);

  EXPECT_EQ(ld_db1->Get<uint8_t>(0), st_db1->Get<uint8_t>(0));
  EXPECT_EQ(ld_db2->Get<uint16_t>(0), st_db2->Get<uint16_t>(0));
  EXPECT_EQ(ld_db4->Get<uint32_t>(0), st_db4->Get<uint32_t>(0));
  EXPECT_EQ(ld_db8->Get<uint64_t>(0), st_db8->Get<uint64_t>(0));

  ld_db1->DecRef();
  ld_db2->DecRef();
  ld_db4->DecRef();
  ld_db8->DecRef();

  st_db1->DecRef();
  st_db2->DecRef();
  st_db4->DecRef();
  st_db8->DecRef();
}

TEST_F(CacheTest, TaggedMemoryTest) {
  TaggedFlatDemandMemory memory(kTagGranule);
  cache_->set_tagged_memory(&memory);
  CHECK_OK(cache_->Configure("1k,16,1,true", &cycle_counter_));

  DataBuffer* ld_data_db = db_factory_.Allocate<uint8_t>(kTagGranule * 16);
  DataBuffer* ld_tag_db = db_factory_.Allocate<uint8_t>(16);
  DataBuffer* st_data_db = db_factory_.Allocate<uint8_t>(kTagGranule * 16);
  DataBuffer* st_tag_db = db_factory_.Allocate<uint8_t>(16);
  ld_data_db->set_latency(0);
  ld_tag_db->set_latency(0);

  cache_->Load(0x1000, ld_data_db, ld_tag_db, nullptr, nullptr);
  // The loaded data should be all zeros.
  for (int i = 0; i < 16; i++) {
    for (int j = 0; j < kTagGranule; j++) {
      EXPECT_EQ(ld_data_db->Get<uint8_t>(i * kTagGranule + j), 0);
    }
    EXPECT_EQ(ld_tag_db->Get<uint8_t>(i), 0);
  }
  // Write out known data.
  for (int i = 0; i < st_data_db->size<uint8_t>(); i++) {
    st_data_db->Set<uint8_t>(i, i);
  }
  for (int i = 0; i < st_tag_db->size<uint8_t>(); i++) {
    st_tag_db->Set<uint8_t>(i, 1);
  }
  cache_->Store(0x1000, st_data_db, st_tag_db);
  // Verify that the loaded data is equal to the stored data.
  cache_->Load(0x1000, ld_data_db, ld_tag_db, nullptr, nullptr);
  for (int i = 0; i < ld_data_db->size<uint8_t>(); i++) {
    EXPECT_EQ(ld_data_db->Get<uint8_t>(i), i);
  }
  for (int i = 0; i < ld_tag_db->size<uint8_t>(); i++) {
    EXPECT_EQ(ld_tag_db->Get<uint8_t>(i), 1);
  }
  // Clear every third tag and store them.
  for (int i = 0; i < st_tag_db->size<uint8_t>(); i++) {
    if (i % 3 == 0) st_tag_db->Set<uint8_t>(i, 0);
  }
  cache_->Store(0x1000, st_data_db, st_tag_db);
  // Re-load and compare.
  cache_->Load(0x1000, ld_data_db, ld_tag_db, nullptr, nullptr);
  for (int i = 0; i < ld_data_db->size<uint8_t>(); i++) {
    EXPECT_EQ(ld_data_db->Get<uint8_t>(i), i);
  }
  for (int i = 0; i < ld_tag_db->size<uint8_t>(); i++) {
    EXPECT_EQ(ld_tag_db->Get<uint8_t>(i), i % 3 == 0 ? 0 : 1) << i;
  }
  ld_data_db->DecRef();
  ld_tag_db->DecRef();
  st_data_db->DecRef();
  st_tag_db->DecRef();
}

TEST_F(CacheTest, CacheableRanges) {
  // Create a cache 16kB, 16B blocks, direct mapped.
  CHECK_OK(cache_->Configure("1k,16,1,true,c:0x1000:0x1fff,c:0x3000:0x3fff",
                             &cycle_counter_));
  // These accesses should be cacheable.
  for (uint64_t address = 0x1000; address < 0x2000; address += 0x100) {
    cache_->Load(address, db_, nullptr, nullptr);
  }
  EXPECT_EQ(read_non_cacheable_->GetValue(), 0);
  EXPECT_EQ(read_misses_->GetValue(), 0x1000 / 0x100);
  // These accesses should be non-cacheable.
  for (uint64_t address = 0x2000; address < 0x3000; address += 0x100) {
    cache_->Load(address, db_, nullptr, nullptr);
  }
  EXPECT_EQ(read_non_cacheable_->GetValue(), 0x1000 / 0x100);
  EXPECT_EQ(read_misses_->GetValue(), 0x1000 / 0x100);
  read_misses_->SetValue(0);
  read_non_cacheable_->SetValue(0);
  // These accesses should be cacheable.
  for (uint64_t address = 0x3000; address < 0x4000; address += 0x100) {
    cache_->Load(address, db_, nullptr, nullptr);
  }
  EXPECT_EQ(read_non_cacheable_->GetValue(), 0);
  EXPECT_EQ(read_misses_->GetValue(), 0x1000 / 0x100);
  // These accesses should be non-cacheable.
  for (uint64_t address = 0x4000; address < 0x5000; address += 0x100) {
    cache_->Load(address, db_, nullptr, nullptr);
  }
  EXPECT_EQ(read_non_cacheable_->GetValue(), 0x1000 / 0x100);
  EXPECT_EQ(read_misses_->GetValue(), 0x1000 / 0x100);
}

TEST_F(CacheTest, NonCacheableRanges) {
  // Create a cache 16kB, 16B blocks, direct mapped.
  CHECK_OK(cache_->Configure("1k,16,1,true,nc:0x1000:0x1fff,nc:0x3000:0x3fff",
                             &cycle_counter_));
  // These accesses should be non-cacheable.
  for (uint64_t address = 0x1000; address < 0x2000; address += 0x100) {
    cache_->Load(address, db_, nullptr, nullptr);
  }
  EXPECT_EQ(read_non_cacheable_->GetValue(), 0x1000 / 0x100);
  EXPECT_EQ(read_misses_->GetValue(), 0);
  // These accesses should be cacheable.
  for (uint64_t address = 0x2000; address < 0x3000; address += 0x100) {
    cache_->Load(address, db_, nullptr, nullptr);
  }
  EXPECT_EQ(read_non_cacheable_->GetValue(), 0x1000 / 0x100);
  EXPECT_EQ(read_misses_->GetValue(), 0x1000 / 0x100);
  read_misses_->SetValue(0);
  read_non_cacheable_->SetValue(0);
  // These accesses should be non-cacheable.
  for (uint64_t address = 0x3000; address < 0x4000; address += 0x100) {
    cache_->Load(address, db_, nullptr, nullptr);
  }
  EXPECT_EQ(read_non_cacheable_->GetValue(), 0x1000 / 0x100);
  EXPECT_EQ(read_misses_->GetValue(), 0);
  // These accesses should be cacheable.
  for (uint64_t address = 0x4000; address < 0x5000; address += 0x100) {
    cache_->Load(address, db_, nullptr, nullptr);
  }
  EXPECT_EQ(read_non_cacheable_->GetValue(), 0x1000 / 0x100);
  EXPECT_EQ(read_misses_->GetValue(), 0x1000 / 0x100);
}

TEST_F(CacheTest, CacheableRangesConfigErrors) {
  absl::Status status;
  // Not enough fields.
  status = cache_->Configure("1k,16,1,true,c:0x1000,c:0x2000", &cycle_counter_);
  EXPECT_EQ(status.code(), absl::StatusCode::kInvalidArgument);
  // Mix of cacheable and non-cacheable ranges.
  status = cache_->Configure("1k,16,1,true,c:0x1000:0x1fff,nc:0x2000:0x2fff",
                             &cycle_counter_);
  EXPECT_EQ(status.code(), absl::StatusCode::kInvalidArgument);
  // Syntax error in number.
  status = cache_->Configure("1k,16,1,true,c:0x1000x:0x1fff", &cycle_counter_);
  EXPECT_EQ(status.code(), absl::StatusCode::kInvalidArgument);
  status = cache_->Configure("1k,16,1,true,c:0x1000:0x1fxff", &cycle_counter_);
  EXPECT_EQ(status.code(), absl::StatusCode::kInvalidArgument);
  // Using neither c nor nc.
  status = cache_->Configure("1k,16,1,true,x:0x1000:0x1fff", &cycle_counter_);
  EXPECT_EQ(status.code(), absl::StatusCode::kInvalidArgument);
  // Lower bound is greater than upper bound.
  status = cache_->Configure("1k,16,1,true,c:0x1fff:0x1000", &cycle_counter_);
  EXPECT_EQ(status.code(), absl::StatusCode::kInvalidArgument);
}

}  // namespace
