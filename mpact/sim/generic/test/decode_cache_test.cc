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

#include "googlemock/include/gmock/gmock.h"  // IWYU pragma: keep
#include "googletest/include/gtest/gtest.h"
#include "mpact/sim/generic/decoder_interface.h"
#include "mpact/sim/generic/instruction.h"

namespace mpact {
namespace sim {
namespace generic {
namespace {

// Simple decoder class.
class MockDecoder : public DecoderInterface {
 public:
  MockDecoder() : num_decoded_(0) {}
  ~MockDecoder() override {}
  Instruction* DecodeInstruction(uint64_t address) override {
    num_decoded_++;
    Instruction* inst = new Instruction(address, nullptr);
    return inst;
  }
  int GetNumOpcodes() const override { return 0; }
  const char* GetOpcodeName(int index) const override { return ""; }
  void set_num_decoded(int val) { num_decoded_ = val; }
  int num_decoded() const { return num_decoded_; }

 private:
  int num_decoded_;
};

// Test fixture for DecodeCacheTest.
class DecodeCacheTest : public testing::Test {
 protected:
  DecodeCacheTest() { decoder_ = new MockDecoder(); }

  ~DecodeCacheTest() override { delete decoder_; }

  MockDecoder* decoder_;
};

// Test creation and verify basic properties.
TEST_F(DecodeCacheTest, BasicProperties) {
  DecodeCacheProperties props;

  props.num_entries = 1000;
  props.minimum_pc_increment = 4;
  DecodeCache* dc = DecodeCache::Create(props, decoder_);
  EXPECT_EQ(dc->num_entries(), 1024);
  EXPECT_EQ(dc->address_mask(), 0xFFC);
  EXPECT_EQ(dc->address_shift(), 2);
  EXPECT_EQ(dc->address_inc(), 4);
  delete dc;

  props.num_entries = 500;
  props.minimum_pc_increment = 1;
  dc = DecodeCache::Create(props, decoder_);
  EXPECT_EQ(dc->num_entries(), 512);
  EXPECT_EQ(dc->address_mask(), 0x1FF);
  EXPECT_EQ(dc->address_shift(), 0);
  EXPECT_EQ(dc->address_inc(), 1);
  delete dc;
}

// Test that the decode cache caches a decoded instruction.
TEST_F(DecodeCacheTest, CacheOne) {
  DecodeCacheProperties props;
  props.num_entries = 1000;
  props.minimum_pc_increment = 4;
  DecodeCache* dc = DecodeCache::Create(props, decoder_);

  Instruction* inst;
  EXPECT_EQ(decoder_->num_decoded(), 0);
  // Not in cache, decoder will be called.
  inst = dc->GetDecodedInstruction(0x1000);
  EXPECT_EQ(decoder_->num_decoded(), 1);
  // In cache. No call to decoder.
  inst = dc->GetDecodedInstruction(0x1000);
  EXPECT_EQ(decoder_->num_decoded(), 1);
  // Not in cache, decoder will be called.
  inst = dc->GetDecodedInstruction(0x1004);
  EXPECT_EQ(decoder_->num_decoded(), 2);
  inst = dc->GetDecodedInstruction(0x1000);
  EXPECT_EQ(decoder_->num_decoded(), 2);
  // This will kick out the instruction with address 0x1000.
  inst = dc->GetDecodedInstruction(0x2000);
  EXPECT_EQ(decoder_->num_decoded(), 3);
  // This will need to be re-decoded.
  inst = dc->GetDecodedInstruction(0x1000);
  EXPECT_EQ(decoder_->num_decoded(), 4);
  // This is still in the cache.
  inst = dc->GetDecodedInstruction(0x1004);
  EXPECT_EQ(decoder_->num_decoded(), 4);
  (void)inst;
  delete dc;
}

// Test invalidation of single instruction.
TEST_F(DecodeCacheTest, InvalidateOne) {
  DecodeCacheProperties props;
  props.num_entries = 1000;
  props.minimum_pc_increment = 4;
  DecodeCache* dc = DecodeCache::Create(props, decoder_);

  Instruction* inst;
  inst = dc->GetDecodedInstruction(0x1000);
  inst = dc->GetDecodedInstruction(0x1004);
  inst = dc->GetDecodedInstruction(0x1008);
  inst = dc->GetDecodedInstruction(0x100c);
  EXPECT_EQ(decoder_->num_decoded(), 4);
  dc->Invalidate(0x1008);
  inst = dc->GetDecodedInstruction(0x1000);
  inst = dc->GetDecodedInstruction(0x1004);
  inst = dc->GetDecodedInstruction(0x1008);
  inst = dc->GetDecodedInstruction(0x100c);
  EXPECT_EQ(decoder_->num_decoded(), 5);
  (void)inst;
  delete dc;
}

// Test invalidation of range.
TEST_F(DecodeCacheTest, InvalidateRange) {
  DecodeCacheProperties props;
  props.num_entries = 1000;
  props.minimum_pc_increment = 4;
  DecodeCache* dc = DecodeCache::Create(props, decoder_);

  Instruction* inst;
  inst = dc->GetDecodedInstruction(0x1000);
  inst = dc->GetDecodedInstruction(0x1004);
  inst = dc->GetDecodedInstruction(0x1008);
  inst = dc->GetDecodedInstruction(0x100c);
  EXPECT_EQ(decoder_->num_decoded(), 4);
  dc->InvalidateRange(0x1004, 0x100c);
  inst = dc->GetDecodedInstruction(0x1000);
  inst = dc->GetDecodedInstruction(0x1004);
  inst = dc->GetDecodedInstruction(0x1008);
  inst = dc->GetDecodedInstruction(0x100c);
  EXPECT_EQ(decoder_->num_decoded(), 6);
  (void)inst;
  delete dc;
}

// Test invalidate all.
TEST_F(DecodeCacheTest, InvalidateAll) {
  DecodeCacheProperties props;
  props.num_entries = 1000;
  props.minimum_pc_increment = 4;
  DecodeCache* dc = DecodeCache::Create(props, decoder_);

  Instruction* inst;
  inst = dc->GetDecodedInstruction(0x1000);
  inst = dc->GetDecodedInstruction(0x1004);
  inst = dc->GetDecodedInstruction(0x1008);
  inst = dc->GetDecodedInstruction(0x100c);
  EXPECT_EQ(decoder_->num_decoded(), 4);
  dc->InvalidateAll();
  inst = dc->GetDecodedInstruction(0x1000);
  inst = dc->GetDecodedInstruction(0x1004);
  inst = dc->GetDecodedInstruction(0x1008);
  inst = dc->GetDecodedInstruction(0x100c);
  EXPECT_EQ(decoder_->num_decoded(), 8);
  (void)inst;
  delete dc;
}

}  // namespace
}  // namespace generic
}  // namespace sim
}  // namespace mpact
