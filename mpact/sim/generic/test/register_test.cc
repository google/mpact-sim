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

#include "mpact/sim/generic/register.h"

#include <cstdint>
#include <memory>

#include "googlemock/include/gmock/gmock.h"  // IWYU pragma: keep
#include "googletest/include/gtest/gtest.h"
#include "mpact/sim/generic/data_buffer.h"
#include "mpact/sim/generic/simple_resource.h"

namespace mpact {
namespace sim {
namespace generic {
namespace {

constexpr int kVectorLength = 8;
constexpr int kMatrixRows = 8;
constexpr int kMatrixCols = 16;

static constexpr char kTestPoolName[] = "TestPool";
static constexpr int kTestPoolSize = 35;

using testing::StrEq;
using ScalarRegister = Register<uint32_t>;
using Vector8Register = VectorRegister<uint32_t, kVectorLength>;
using Matrix8By16Register = MatrixRegister<uint32_t, kMatrixRows, kMatrixCols>;

using ScalarReservedRegister = ReservedRegister<uint32_t>;

// Test fixture that instantiates the factory class for DataBuffers.
class RegisterTest : public testing::Test {
 protected:
  RegisterTest() {
    db_factory_ = new DataBufferFactory();
    pool_ = new SimpleResourcePool(kTestPoolName, kTestPoolSize);
  }

  ~RegisterTest() override {
    delete db_factory_;
    delete pool_;
  }

  DataBufferFactory* db_factory_;
  SimpleResourcePool* pool_;
};

// Create scalar register and verify attributes.
TEST_F(RegisterTest, ScalarCreate) {
  auto scalar_reg = std::make_unique<ScalarRegister>(nullptr, "R0");
  EXPECT_THAT(scalar_reg->name(), StrEq("R0"));
  EXPECT_EQ(scalar_reg->shape().size(), 1);
  EXPECT_EQ(scalar_reg->size(), sizeof(uint32_t));
}

// Create vector register and verify attributes.
TEST_F(RegisterTest, VectorCreate) {
  auto vector_reg = std::make_unique<Vector8Register>(nullptr, "V0");
  EXPECT_THAT(vector_reg->name(), StrEq("V0"));
  EXPECT_EQ(vector_reg->shape()[0], kVectorLength);
  EXPECT_EQ(vector_reg->size(), kVectorLength * sizeof(uint32_t));
}

// Create matrix register and verify attirbutes.
TEST_F(RegisterTest, MatrixCreate) {
  auto matrix_reg = std::make_unique<Matrix8By16Register>(nullptr, "M0");
  EXPECT_THAT(matrix_reg->name(), StrEq("M0"));
  EXPECT_EQ(matrix_reg->shape()[0], kMatrixRows);
  EXPECT_EQ(matrix_reg->shape()[1], kMatrixCols);
  EXPECT_EQ(matrix_reg->size(), kMatrixRows * kMatrixCols * sizeof(uint32_t));
}

// Create reserved register and verify that it releases the resource when
// SetDataBuffer is called.
TEST_F(RegisterTest, ScalarReservedCreate) {
  ASSERT_TRUE(pool_->AddResource("S0").ok());
  auto scalar_reserved_reg = std::make_unique<ScalarReservedRegister>(
      nullptr, "S0", pool_->GetResource("S0"));
  pool_->GetResource("S0")->Acquire();
  EXPECT_FALSE(pool_->GetResource("S0")->IsFree());
  auto* db = db_factory_->Allocate(scalar_reserved_reg->size());
  scalar_reserved_reg->SetDataBuffer(db);
  EXPECT_TRUE(pool_->GetResource("S0")->IsFree());
  db->DecRef();
}

// Verify scalar databuffer api.
TEST_F(RegisterTest, ScalarDataBuffer) {
  // Allocate register and make sure data_buffer is nullptr.
  auto scalar_reg = std::make_unique<ScalarRegister>(nullptr, "R0");
  EXPECT_EQ(scalar_reg->data_buffer(), nullptr);

  // Allocate a data buffer of the right byte size and bind it to the register.
  DataBuffer* db = db_factory_->Allocate(scalar_reg->size());
  scalar_reg->SetDataBuffer(db);

  // Verify reference count is 2, then DecRef.
  EXPECT_EQ(db->ref_count(), 2);
  db->DecRef();
  EXPECT_EQ(scalar_reg->data_buffer(), db);
}

// Verify vector databuffer api.
TEST_F(RegisterTest, VectorDataBuffer) {
  // Allocate register and make sure data_buffer is nullptr.
  auto vector_reg = std::make_unique<Vector8Register>(nullptr, "V0");
  EXPECT_EQ(vector_reg->data_buffer(), nullptr);

  // Allocate a data buffer of the right byte size and bind it to the register.
  DataBuffer* db = db_factory_->Allocate(vector_reg->size());
  vector_reg->SetDataBuffer(db);

  // Verify reference count is 2, then DecRef.
  EXPECT_EQ(db->ref_count(), 2);
  db->DecRef();
  EXPECT_EQ(vector_reg->data_buffer(), db);
}

// Verify matrix databuffer api.
TEST_F(RegisterTest, MatrixDataBuffer) {
  // Allocate register and make sure data_buffer is nullptr.
  auto matrix_reg = std::make_unique<Matrix8By16Register>(nullptr, "M0");
  EXPECT_EQ(matrix_reg->data_buffer(), nullptr);

  // Allocate a data buffer of the right byte size and bind it to the register.
  DataBuffer* db = db_factory_->Allocate(matrix_reg->size());
  matrix_reg->SetDataBuffer(db);

  // Verify reference count is 2, then DecRef.
  EXPECT_EQ(db->ref_count(), 2);
  db->DecRef();
  EXPECT_EQ(matrix_reg->data_buffer(), db);
}

}  // namespace
}  // namespace generic
}  // namespace sim
}  // namespace mpact
