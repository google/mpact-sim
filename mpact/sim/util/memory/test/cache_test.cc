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
#include "googlemock/include/gmock/gmock.h"  // IWYU pragma: keep
#include "googletest/include/gtest/gtest.h"
#include "mpact/sim/generic/counters.h"
#include "mpact/sim/generic/data_buffer.h"

namespace {

using ::mpact::sim::generic::DataBuffer;
using ::mpact::sim::generic::DataBufferFactory;
using ::mpact::sim::generic::SimpleCounter;
using ::mpact::sim::util::Cache;

class CacheTest : public testing::Test {
 protected:
  CacheTest() : cycle_counter_("cycle_counter", 0ULL) {
    // Create a cache 16kB, 16B blocks, direct mapped.
    cache_ = new Cache("cache");
    db_ = db_factory_.Allocate<uint32_t>(1);
    read_hits_ = reinterpret_cast<SimpleCounter<uint64_t> *>(
        cache_->GetCounter("read_hit"));
    read_misses_ = reinterpret_cast<SimpleCounter<uint64_t> *>(
        cache_->GetCounter("read_miss"));
    write_hits_ = reinterpret_cast<SimpleCounter<uint64_t> *>(
        cache_->GetCounter("write_hit"));
    write_misses_ = reinterpret_cast<SimpleCounter<uint64_t> *>(
        cache_->GetCounter("write_miss"));
    dirty_line_writebacks_ = reinterpret_cast<SimpleCounter<uint64_t> *>(
        cache_->GetCounter("dirty_line_writeback"));
    read_arounds_ = reinterpret_cast<SimpleCounter<uint64_t> *>(
        cache_->GetCounter("read_around"));
    write_arounds_ = reinterpret_cast<SimpleCounter<uint64_t> *>(
        cache_->GetCounter("write_around"));
  }

  ~CacheTest() override {
    delete cache_;
    db_->DecRef();
  }

  DataBufferFactory db_factory_;
  DataBuffer *db_;
  Cache *cache_;
  SimpleCounter<uint64_t> *read_hits_;
  SimpleCounter<uint64_t> *read_misses_;
  SimpleCounter<uint64_t> *write_hits_;
  SimpleCounter<uint64_t> *write_misses_;
  SimpleCounter<uint64_t> *dirty_line_writebacks_;
  SimpleCounter<uint64_t> *read_arounds_;
  SimpleCounter<uint64_t> *write_arounds_;
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

}  // namespace
