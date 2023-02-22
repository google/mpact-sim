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

#include "mpact/sim/generic/fifo.h"

#include <cstdint>
#include <memory>

#include "absl/memory/memory.h"
#include "googlemock/include/gmock/gmock.h"
#include "mpact/sim/generic/config.h"
#include "mpact/sim/generic/data_buffer.h"
#include "mpact/sim/generic/program_error.h"
#include "mpact/sim/proto/component_data.pb.h"
#include "src/google/protobuf/text_format.h"
#include "src/google/protobuf/util/message_differencer.h"

namespace mpact {
namespace sim {
namespace generic {
namespace {

constexpr char kControllerName[] = "ErrorController";
constexpr char kOverflowName[] = "FifoOverflow";
constexpr char kUnderflowName[] = "FifoUnderflow";
constexpr int kVectorLength = 8;
constexpr int kMatrixRows = 8;
constexpr int kMatrixCols = 16;
constexpr int kFifoDepth = 3;

using testing::StrEq;
using ScalarFifo = Fifo<uint32_t>;
using Vector8Fifo = VectorFifo<uint32_t, kVectorLength>;
using Matrix8By16Fifo = MatrixFifo<uint32_t, kMatrixRows, kMatrixCols>;

// Test fixture that instantiates the factory class for DataBuffers.
class FifoTest : public testing::Test {
 protected:
  FifoTest() {
    db_factory_ = std::make_unique<DataBufferFactory>();
    controller_ = std::make_unique<ProgramErrorController>(kControllerName);
  }

  std::unique_ptr<DataBufferFactory> db_factory_;
  std::unique_ptr<ProgramErrorController> controller_;
};

// Create scalar valued and verify attributes.
TEST_F(FifoTest, ScalarCreate) {
  auto scalar_fifo = std::make_unique<ScalarFifo>(nullptr, "S0", kFifoDepth);
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

// Create vector fifo and verify attributes.
TEST_F(FifoTest, VectorCreate) {
  auto vector_fifo = std::make_unique<Vector8Fifo>(nullptr, "V0", kFifoDepth);
  EXPECT_THAT(vector_fifo->name(), StrEq("V0"));
  EXPECT_EQ(vector_fifo->shape().size(), 1);
  EXPECT_EQ(vector_fifo->shape()[0], kVectorLength);
  EXPECT_EQ(vector_fifo->size(), kVectorLength * sizeof(uint32_t));
  EXPECT_EQ(vector_fifo->Available(), 0);
  EXPECT_EQ(vector_fifo->Capacity(), kFifoDepth);
  EXPECT_EQ(vector_fifo->Front(), nullptr);
  EXPECT_FALSE(vector_fifo->IsFull());
  EXPECT_TRUE(vector_fifo->IsEmpty());
}

// Create matrix fifo and verify attirbutes.
TEST_F(FifoTest, MatrixCreate) {
  auto matrix_fifo =
      std::make_unique<Matrix8By16Fifo>(nullptr, "M0", kFifoDepth);
  EXPECT_THAT(matrix_fifo->name(), StrEq("M0"));
  EXPECT_EQ(matrix_fifo->shape()[0], kMatrixRows);
  EXPECT_EQ(matrix_fifo->shape()[1], kMatrixCols);
  EXPECT_EQ(matrix_fifo->size(), kMatrixRows * kMatrixCols * sizeof(uint32_t));
  EXPECT_EQ(matrix_fifo->Available(), 0);
  EXPECT_EQ(matrix_fifo->Capacity(), kFifoDepth);
  EXPECT_EQ(matrix_fifo->Front(), nullptr);
  EXPECT_FALSE(matrix_fifo->IsFull());
  EXPECT_TRUE(matrix_fifo->IsEmpty());
}

// Verify scalar databuffer api.
TEST_F(FifoTest, ScalarDataBuffer) {
  // Allocate fifo and make sure data_buffer is nullptr.
  auto scalar_fifo = std::make_unique<ScalarFifo>(nullptr, "S0", kFifoDepth);
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

// Verify vector databuffer api.
TEST_F(FifoTest, VectorDataBuffer) {
  // Allocate fifo and make sure data_buffer is nullptr.
  auto vector_fifo = std::make_unique<Vector8Fifo>(nullptr, "V0", kFifoDepth);
  EXPECT_EQ(vector_fifo->Front(), nullptr);

  // Allocate a data buffer of the right byte size and bind it to the fifo.
  DataBuffer *db = db_factory_->Allocate(vector_fifo->size());
  vector_fifo->SetDataBuffer(db);
  EXPECT_EQ(vector_fifo->Available(), 1);
  EXPECT_FALSE(vector_fifo->IsFull());
  EXPECT_FALSE(vector_fifo->IsEmpty());

  // Verify reference count is 2, then DecRef.
  EXPECT_EQ(db->ref_count(), 2);
  db->DecRef();
  EXPECT_EQ(vector_fifo->Front(), db);
}

// Verify matrix databuffer api.
TEST_F(FifoTest, MatrixDataBuffer) {
  // Allocate fifo and make sure data_buffer is nullptr.
  auto matrix_fifo =
      std::make_unique<Matrix8By16Fifo>(nullptr, "M0", kFifoDepth);
  EXPECT_EQ(matrix_fifo->Front(), nullptr);

  // Allocate a data buffer of the right byte size and bind it to the fifo.
  DataBuffer *db = db_factory_->Allocate(matrix_fifo->size());
  matrix_fifo->SetDataBuffer(db);
  EXPECT_EQ(matrix_fifo->Available(), 1);
  EXPECT_FALSE(matrix_fifo->IsFull());
  EXPECT_FALSE(matrix_fifo->IsEmpty());

  // Verify reference count is 2, then DecRef.
  EXPECT_EQ(db->ref_count(), 2);
  db->DecRef();
  EXPECT_EQ(matrix_fifo->Front(), db);
}

// Verify Fifo empty/full.
TEST_F(FifoTest, EmptyFullEmpty) {
  auto fifo = std::make_unique<ScalarFifo>(nullptr, "S0", kFifoDepth);

  DataBuffer *db[kFifoDepth + 1];
  for (int db_num = 0; db_num < kFifoDepth + 1; db_num++) {
    db[db_num] = db_factory_->Allocate(fifo->size());
  }

  // Verify IsFull, IsEmpty, and Available as 4 DataBuffer objects are pushed.
  for (int db_num = 0; db_num < kFifoDepth + 1; db_num++) {
    EXPECT_EQ(fifo->IsFull(), db_num >= kFifoDepth);
    EXPECT_EQ(fifo->IsEmpty(), db_num == 0);
    EXPECT_EQ(fifo->Available(), db_num);

    // The 4th Push will fail.
    bool success = fifo->Push(db[db_num]);
    EXPECT_EQ(success, db_num < kFifoDepth);

    EXPECT_EQ(fifo->IsFull(), db_num >= kFifoDepth - 1);
    EXPECT_EQ(fifo->IsEmpty(), false);
    EXPECT_EQ(fifo->Available(), (db_num >= kFifoDepth ? db_num : db_num + 1));
  }

  // Verify IsFull, IsEmpty and Available as DataBuffer objects are popped.
  for (int db_num = 0; db_num < kFifoDepth + 1; db_num++) {
    EXPECT_EQ(fifo->Front(), db_num < kFifoDepth ? db[db_num] : nullptr);
    EXPECT_EQ(fifo->Available(),
              db_num > kFifoDepth - 1 ? 0 : kFifoDepth - db_num);
    EXPECT_EQ(fifo->IsFull(), db_num == 0);
    EXPECT_EQ(fifo->IsEmpty(), db_num > kFifoDepth - 1);

    fifo->Pop();

    EXPECT_EQ(fifo->Available(),
              db_num > kFifoDepth - 2 ? 0 : kFifoDepth - db_num - 1);
    EXPECT_EQ(fifo->IsFull(), false);
    EXPECT_EQ(fifo->IsEmpty(), db_num > kFifoDepth - 2);

    // Cleanup.
    db[db_num]->DecRef();
  }
}

TEST_F(FifoTest, Reserve) {
  auto fifo = std::make_unique<ScalarFifo>(nullptr, "S0", kFifoDepth);
  EXPECT_TRUE(fifo->IsEmpty());
  EXPECT_EQ(fifo->Reserved(), 0);
  fifo->Reserve(kFifoDepth);
  EXPECT_EQ(fifo->Reserved(), kFifoDepth);
  EXPECT_FALSE(fifo->IsEmpty());
  EXPECT_TRUE(fifo->IsFull());
  EXPECT_FALSE(fifo->IsOverSubscribed());

  DataBuffer *db[kFifoDepth];
  for (int db_num = 0; db_num < kFifoDepth; db_num++) {
    db[db_num] = db_factory_->Allocate(fifo->size());
    fifo->Push(db[db_num]);
    EXPECT_FALSE(fifo->IsEmpty());
    EXPECT_TRUE(fifo->IsFull());
    EXPECT_FALSE(fifo->IsOverSubscribed());
    EXPECT_EQ(fifo->Reserved(), kFifoDepth - db_num - 1);
    EXPECT_EQ(fifo->Available(), db_num + 1);
  }

  // Cleanup.
  for (int db_num = 0; db_num < kFifoDepth; db_num++) {
    fifo->Pop();
    db[db_num]->DecRef();
  }
}

TEST_F(FifoTest, Overflow) {
  auto fifo = std::make_unique<ScalarFifo>(nullptr, "S0", kFifoDepth);
  fifo->Reserve(kFifoDepth + 1);
  EXPECT_TRUE(fifo->IsOverSubscribed());
}

TEST_F(FifoTest, UnderflowProgramError) {
  auto fifo = std::make_unique<ScalarFifo>(nullptr, "S0", kFifoDepth);
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

TEST_F(FifoTest, OverflowProgramError) {
  auto fifo = std::make_unique<ScalarFifo>(nullptr, "S0", kFifoDepth);
  controller_->AddProgramErrorName(kOverflowName);
  auto overflow = controller_->GetProgramError(kOverflowName);
  fifo->SetOverflowProgramError(&overflow);
  EXPECT_FALSE(controller_->HasError());

  DataBuffer *db[kFifoDepth + 1];
  for (int db_num = 0; db_num < kFifoDepth + 1; db_num++) {
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
  for (int db_num = 0; db_num < kFifoDepth + 1; db_num++) {
    db[db_num]->DecRef();
  }
}

TEST_F(FifoTest, Configuration) {
  constexpr char kConfig[] = R"pb(
    name: "S0",
    configuration { name: "S0" uint64_value: 15 }
  )pb";
  proto::ComponentData fromText;
  EXPECT_TRUE(google::protobuf::TextFormat::ParseFromString(kConfig, &fromText));
  auto fifo = std::make_unique<ScalarFifo>(nullptr, "S0", kFifoDepth);
  EXPECT_TRUE(fifo->Import(fromText).ok());
}

}  // namespace
}  // namespace generic
}  // namespace sim
}  // namespace mpact
