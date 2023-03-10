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

#include "mpact/sim/generic/token_fifo.h"

#include <cstdint>
#include <memory>

#include "absl/memory/memory.h"
#include "googlemock/include/gmock/gmock.h"
#include "googletest/include/gtest/gtest.h"
#include "mpact/sim/generic/data_buffer.h"
#include "mpact/sim/generic/program_error.h"

namespace mpact {
namespace sim {
namespace generic {
namespace {

constexpr char kControllerName[] = "ErrorController";
constexpr char kOverflowName[] = "FifoOverflow";
constexpr char kUnderflowName[] = "FifoUnderflow";
constexpr int kFifoDepth = 5;
constexpr int kNumTokens = 3;

using testing::StrEq;
using ScalarTokenFifo = TokenFifo<uint32_t>;

// Test fixture that instantiates the factory class for DataBuffers.
class TokenFifoTest : public testing::Test {
 protected:
  TokenFifoTest() : token_store_(kNumTokens) {
    db_factory_ = std::make_unique<DataBufferFactory>();
    controller_ = std::make_unique<ProgramErrorController>(kControllerName);
  }

  FifoTokenStore token_store_;
  std::unique_ptr<DataBufferFactory> db_factory_;
  std::unique_ptr<ProgramErrorController> controller_;
};

// Create scalar valued and verify attributes.
TEST_F(TokenFifoTest, ScalarCreate) {
  auto scalar_fifo = std::make_unique<ScalarTokenFifo>(
      nullptr, "S0", kFifoDepth, &token_store_);
  EXPECT_THAT(scalar_fifo->name(), StrEq("S0"));
  EXPECT_EQ(scalar_fifo->shape().size(), 1);
  EXPECT_EQ(scalar_fifo->shape()[0], 1);
  EXPECT_EQ(scalar_fifo->size(), sizeof(uint32_t));
  EXPECT_EQ(scalar_fifo->Available(), 0);
  EXPECT_EQ(scalar_fifo->Capacity(), kFifoDepth);
  EXPECT_EQ(scalar_fifo->Front(), nullptr);
  EXPECT_EQ(scalar_fifo->IsFull(), false);
  EXPECT_EQ(scalar_fifo->IsEmpty(), true);
}

// Verify scalar databuffer api.
TEST_F(TokenFifoTest, ScalarDataBuffer) {
  // Allocate fifo and make sure data_buffer is nullptr.
  auto scalar_fifo = std::make_unique<ScalarTokenFifo>(
      nullptr, "S0", kFifoDepth, &token_store_);
  EXPECT_EQ(scalar_fifo->Front(), nullptr);

  // Allocate a data buffer of the right byte size and bind it to the fifo.
  DataBuffer *db = db_factory_->Allocate(scalar_fifo->size());
  scalar_fifo->SetDataBuffer(db);
  EXPECT_EQ(scalar_fifo->Available(), 1);
  EXPECT_FALSE(scalar_fifo->IsFull());
  EXPECT_FALSE(scalar_fifo->IsEmpty());

  // Verify reference count is 2, then DecRef.
  EXPECT_EQ(db->ref_count(), 2);
  db->DecRef();
  EXPECT_EQ(scalar_fifo->Front(), db);
}

// Verify Fifo empty/full.
TEST_F(TokenFifoTest, EmptyFullEmpty) {
  auto fifo = std::make_unique<ScalarTokenFifo>(nullptr, "S0", kFifoDepth,
                                                &token_store_);

  DataBuffer *db[kNumTokens + 1];
  for (int db_num = 0; db_num < kNumTokens + 1; db_num++) {
    db[db_num] = db_factory_->Allocate(fifo->size());
  }

  // Verify IsFull, IsEmpty, and Available as 4 DataBuffer objects are pushed.
  for (int db_num = 0; db_num < kNumTokens + 1; db_num++) {
    // Before the push.
    EXPECT_EQ(fifo->IsFull(), db_num >= kNumTokens);
    EXPECT_EQ(fifo->IsEmpty(), db_num == 0);
    EXPECT_EQ(fifo->Available(), std::min(db_num, kNumTokens));

    // The 4th Push will fail.
    bool success = fifo->Push(db[db_num]);
    EXPECT_EQ(success, db_num < kNumTokens);

    EXPECT_EQ(fifo->IsFull(), db_num >= kNumTokens - 1);
    EXPECT_EQ(fifo->IsEmpty(), false);
    EXPECT_EQ(fifo->Available(), std::min(db_num + 1, kNumTokens));
  }

  // Verify IsFull, IsEmpty and Available as DataBuffer objects are popped.
  for (int db_num = 0; db_num < kNumTokens + 1; db_num++) {
    EXPECT_EQ(fifo->Front(), db_num < kNumTokens ? db[db_num] : nullptr);
    EXPECT_EQ(fifo->Available(),
              db_num > kNumTokens - 1 ? 0 : kNumTokens - db_num);
    EXPECT_EQ(fifo->IsFull(), db_num == 0);
    EXPECT_EQ(fifo->IsEmpty(), db_num > kNumTokens - 1);

    fifo->Pop();

    EXPECT_EQ(fifo->Available(),
              db_num > kNumTokens - 2 ? 0 : kNumTokens - db_num - 1);
    EXPECT_EQ(fifo->IsFull(), false);
    EXPECT_EQ(fifo->IsEmpty(), db_num > kNumTokens - 2);

    // Cleanup.
    db[db_num]->DecRef();
  }
}

TEST_F(TokenFifoTest, Reserve) {
  auto fifo = std::make_unique<ScalarTokenFifo>(nullptr, "S0", kFifoDepth,
                                                &token_store_);
  EXPECT_TRUE(fifo->IsEmpty());
  EXPECT_EQ(fifo->Reserved(), 0);
  fifo->Reserve(kNumTokens);
  EXPECT_EQ(fifo->Reserved(), kNumTokens);
  EXPECT_FALSE(fifo->IsEmpty());
  EXPECT_TRUE(fifo->IsFull());
  EXPECT_FALSE(fifo->IsOverSubscribed());

  DataBuffer *db[kNumTokens];
  for (int db_num = 0; db_num < kNumTokens; db_num++) {
    db[db_num] = db_factory_->Allocate(fifo->size());
    fifo->Push(db[db_num]);
    EXPECT_FALSE(fifo->IsEmpty());
    EXPECT_TRUE(fifo->IsFull());
    EXPECT_FALSE(fifo->IsOverSubscribed());
    EXPECT_EQ(fifo->Reserved(), kNumTokens - db_num - 1);
    EXPECT_EQ(fifo->Available(), db_num + 1);
  }

  // Cleanup.
  for (int db_num = 0; db_num < kNumTokens; db_num++) {
    fifo->Pop();
    db[db_num]->DecRef();
  }
}

TEST_F(TokenFifoTest, Overflow) {
  auto fifo = std::make_unique<ScalarTokenFifo>(nullptr, "S0", kFifoDepth,
                                                &token_store_);
  fifo->Reserve(kNumTokens + 1);
  EXPECT_TRUE(fifo->IsOverSubscribed());
}

TEST_F(TokenFifoTest, UnderflowProgramError) {
  auto fifo = std::make_unique<ScalarTokenFifo>(nullptr, "S0", kFifoDepth,
                                                &token_store_);
  controller_->AddProgramErrorName(kUnderflowName);
  auto underflow = controller_->GetProgramError(kUnderflowName);
  fifo->SetUnderflowProgramError(&underflow);
  EXPECT_FALSE(controller_->HasError());

  // Popping an empty fifo should cause an underflow program error.
  fifo->Pop();
  EXPECT_TRUE(controller_->HasError());
  EXPECT_TRUE(controller_->HasUnmaskedError());
  EXPECT_THAT(controller_->GetUnmaskedErrorNames()[0],
              testing::StrEq(kUnderflowName));
  controller_->ClearAll();

  // Accessing the Front of an empty fifo should cause an underflow program
  // error.
  (void)fifo->Front();
  EXPECT_TRUE(controller_->HasError());
  EXPECT_TRUE(controller_->HasUnmaskedError());
  EXPECT_THAT(controller_->GetUnmaskedErrorNames()[0],
              testing::StrEq(kUnderflowName));
}

TEST_F(TokenFifoTest, OverflowProgramError) {
  auto fifo = std::make_unique<ScalarTokenFifo>(nullptr, "S0", kFifoDepth,
                                                &token_store_);
  controller_->AddProgramErrorName(kOverflowName);
  auto overflow = controller_->GetProgramError(kOverflowName);
  fifo->SetOverflowProgramError(&overflow);
  EXPECT_FALSE(controller_->HasError());

  DataBuffer *db[kNumTokens + 1];
  for (int db_num = 0; db_num < kNumTokens + 1; db_num++) {
    db[db_num] = db_factory_->Allocate(fifo->size());
    fifo->Push(db[db_num]);
  }

  EXPECT_TRUE(fifo->IsFull());
  // No overflow set since there are no reserved slots. However, the overflow
  // program error should be set.
  EXPECT_FALSE(fifo->IsOverSubscribed());
  EXPECT_TRUE(controller_->HasError());
  EXPECT_TRUE(controller_->HasUnmaskedError());
  EXPECT_THAT(controller_->GetUnmaskedErrorNames()[0],
              testing::StrEq(kOverflowName));
  controller_->ClearAll();

  // Cleanup data buffers.
  for (int db_num = 0; db_num < kNumTokens + 1; db_num++) {
    db[db_num]->DecRef();
  }
}

}  // namespace
}  // namespace generic
}  // namespace sim
}  // namespace mpact
