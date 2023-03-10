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

#include "mpact/sim/generic/data_buffer.h"

#include <cstdint>
#include <vector>

#include "googlemock/include/gmock/gmock.h"
#include "googletest/include/gtest/gtest.h"

namespace mpact {
namespace sim {
namespace generic {
namespace {

// Test fixture that instantiates the factory class for DataBuffers.
class DataBufferTest : public testing::Test {
 protected:
  DataBufferTest() { db_factory_ = new DataBufferFactory(); }

  ~DataBufferTest() override { delete db_factory_; }

  DataBufferFactory *db_factory_;
};

// Test that DataBufferFactory allocates the right sizes.
TEST_F(DataBufferTest, DataBufferFactoryAllocate) {
  DataBuffer *db8_1 = db_factory_->Allocate<uint32_t>(8);
  EXPECT_EQ(db8_1->size<uint32_t>(), 8);
  EXPECT_EQ(db8_1->size<uint8_t>(), 32);
  EXPECT_EQ(db8_1->ref_count(), 1);

  DataBuffer *db4_1 = db_factory_->Allocate<uint32_t>(4);
  EXPECT_EQ(db4_1->size<uint32_t>(), 4);
  EXPECT_EQ(db4_1->size<uint8_t>(), 16);
  EXPECT_EQ(db4_1->ref_count(), 1);

  db8_1->DecRef();
  db4_1->DecRef();
}

// Test that DataBuffers are allocated and recycled.
TEST_F(DataBufferTest, DataBufferFactoryAllocateRecycleAllocate) {
  DataBuffer *db8_1 = db_factory_->Allocate<uint32_t>(8);

  db8_1->DecRef();

  DataBuffer *db4_1 = db_factory_->Allocate<uint32_t>(4);
  EXPECT_NE(db8_1, db4_1);

  db4_1->DecRef();

  DataBuffer *db8_2 = db_factory_->Allocate<uint32_t>(8);
  DataBuffer *db4_2 = db_factory_->Allocate<uint32_t>(4);

  EXPECT_EQ(db8_1, db8_2);
  EXPECT_EQ(db4_1, db4_2);
  EXPECT_NE(db8_2, db4_2);

  db8_2->DecRef();
  db4_2->DecRef();
}

// Test that MakeCopyOf allocates a DataBuffer with the content copied from
// the source DataBuffer.
TEST_F(DataBufferTest, DataBufferFactoryMakeCopyOf) {
  const uint32_t kValues[] = {0x01020304, 0xDEADBEEF, 0xA5A55A5A, 0xF0F00F0F};
  const int kSize = 4;

  DataBuffer *db_source = db_factory_->Allocate<uint32_t>(kSize);

  for (int index = 0; index < kSize; index++) {
    db_source->Set<uint32_t>(index, kValues[index]);
  }

  DataBuffer *db_dest = db_factory_->MakeCopyOf(db_source);

  EXPECT_NE(db_source, db_dest);
  EXPECT_EQ(db_source->size<uint32_t>(), db_dest->size<uint32_t>());

  for (int index = 0; index < kSize; index++) {
    EXPECT_EQ(db_dest->Get<uint32_t>(index), kValues[index]);
  }

  db_source->DecRef();
  db_dest->DecRef();
}

// Test vector/array based Set and Get of Databuffer.
TEST_F(DataBufferTest, DataBufferVectorSet) {
  // First use a const vector.
  const std::vector<uint32_t> kValueVec{0x01020304, 0xDEADBEEF, 0xA5A55A5A,
                                        0xF0F00F0F};

  DataBuffer *db = db_factory_->Allocate<uint32_t>(kValueVec.size());
  db->Set<uint32_t>(kValueVec);

  for (size_t index = 0; index < kValueVec.size(); index++) {
    EXPECT_EQ(db->Get<uint32_t>(index), kValueVec[index]);
  }
  EXPECT_THAT(db->Get<uint32_t>(), testing::ElementsAreArray(kValueVec));

  // Next try a const array.
  const uint32_t kValueArray[] = {0x01010101, 0x02020202, 0x03030303,
                                  0x04040404};
  db->Set<uint32_t>(kValueArray);

  for (size_t index = 0; index < kValueVec.size(); index++) {
    EXPECT_EQ(db->Get<uint32_t>(index), kValueArray[index]);
  }
  EXPECT_THAT(db->Get<uint32_t>(), testing::ElementsAreArray(kValueArray));

  db->DecRef();
}

// Test raw pointer use with a struct.
TEST_F(DataBufferTest, DataBufferRawPointer) {
  struct MyTest {
    bool a;
    uint16_t b;
    uint32_t c;
    uint64_t d;
    float x;
    double y;
  };

  DataBuffer *db = db_factory_->Allocate(sizeof(MyTest));
  EXPECT_EQ(sizeof(MyTest), db->size<unsigned char>());
  MyTest *my_test = reinterpret_cast<MyTest *>(db->raw_ptr());
  my_test->y = 3.14;
  db->DecRef();
}

}  // namespace
}  // namespace generic
}  // namespace sim
}  // namespace mpact
