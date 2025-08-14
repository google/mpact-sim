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

#include "mpact/sim/generic/control_register.h"

#include <cstdint>
#include <memory>

#include "googlemock/include/gmock/gmock.h"
#include "googletest/include/gtest/gtest.h"
#include "mpact/sim/generic/data_buffer.h"
#include "mpact/sim/generic/register.h"

namespace {

using ::mpact::sim::generic::ControlRegisterBase;
using ::mpact::sim::generic::DataBuffer;
using ::mpact::sim::generic::DataBufferFactory;
using testing::StrEq;
using TestRegister = ::mpact::sim::generic::ControlRegister<uint32_t>;

class ControlRegisterTest : public testing::Test {
 protected:
  ControlRegisterTest() { db_factory_ = new DataBufferFactory(); }

  ~ControlRegisterTest() override { delete db_factory_; }

  DataBufferFactory* db_factory_;
};

// Create scalar register and verify attributes.
TEST_F(ControlRegisterTest, Create) {
  auto scalar_reg = std::make_unique<TestRegister>(
      nullptr, "R0", [](ControlRegisterBase*, DataBuffer*) {});
  EXPECT_THAT(scalar_reg->name(), StrEq("R0"));
  EXPECT_EQ(scalar_reg->shape().size(), 1);
  EXPECT_EQ(scalar_reg->size(), sizeof(uint32_t));
}

// Verify databuffer api. and that the update callback works
TEST_F(ControlRegisterTest, DataBuffer) {
  bool works = false;
  // Allocate register and make sure data_buffer is nullptr.
  auto reg = std::make_unique<TestRegister>(
      nullptr, "R0", [&works](ControlRegisterBase* creg, DataBuffer* db) {
        works = true;
        creg->RegisterBase::SetDataBuffer(db);
      });
  EXPECT_EQ(reg->data_buffer(), nullptr);

  // Allocate a data buffer of the right byte size and bind it to the register.
  DataBuffer* db = db_factory_->Allocate(reg->size());
  reg->SetDataBuffer(db);

  EXPECT_TRUE(works);
  // Verify reference count is 2, then DecRef.
  EXPECT_EQ(db->ref_count(), 2);
  db->DecRef();
  EXPECT_EQ(reg->data_buffer(), db);
}

}  // namespace
