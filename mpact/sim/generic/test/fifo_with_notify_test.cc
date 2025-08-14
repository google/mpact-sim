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

#include "mpact/sim/generic/fifo_with_notify.h"

#include <cstdint>
#include <memory>

#include "googlemock/include/gmock/gmock.h"  // IWYU pragma: keep
#include "googletest/include/gtest/gtest.h"
#include "mpact/sim/generic/data_buffer.h"

namespace {

using ::mpact::sim::generic::DataBufferFactory;
using ::mpact::sim::generic::FifoWithNotify;
using ::mpact::sim::generic::FifoWithNotifyBase;

constexpr int kFifoDepth = 5;
constexpr char kFifoName[] = "fifo";

class FifoWithNotifyTest : public testing::Test {
 protected:
  FifoWithNotifyTest() {
    db_factory_ = std::make_unique<DataBufferFactory>();
    fifo_ = std::make_unique<FifoWithNotify<uint32_t>>(nullptr, kFifoName,
                                                       kFifoDepth);
  }

  std::unique_ptr<DataBufferFactory> db_factory_;
  std::unique_ptr<FifoWithNotify<uint32_t>> fifo_;
};

// Since FifoWithNotify inherits from FifoBase we only need to test the callback
// functionality.

TEST_F(FifoWithNotifyTest, NoCallbacks) {
  EXPECT_TRUE(fifo_->IsEmpty());
  auto* db = db_factory_->Allocate(sizeof(uint32_t));
  fifo_->Push(db);
  EXPECT_TRUE(!fifo_->IsEmpty());
  fifo_->Pop();
  db->DecRef();
}

TEST_F(FifoWithNotifyTest, OnEmpty) {
  int on_empty_count = 0;
  int on_not_empty_count = 0;
  fifo_->SetOnEmpty(
      [&on_empty_count](FifoWithNotifyBase*) { on_empty_count++; });
  fifo_->SetOnNotEmpty(
      [&on_not_empty_count](FifoWithNotifyBase*) { on_not_empty_count++; });
  auto* db = db_factory_->Allocate(sizeof(uint32_t));
  EXPECT_EQ(on_empty_count, 0);
  EXPECT_EQ(on_not_empty_count, 0);
  (void)fifo_->Push(db);
  EXPECT_EQ(on_empty_count, 0);
  EXPECT_EQ(on_not_empty_count, 1);
  (void)fifo_->Push(db);
  EXPECT_EQ(on_empty_count, 0);
  EXPECT_EQ(on_not_empty_count, 1);
  fifo_->Pop();
  EXPECT_EQ(on_empty_count, 0);
  EXPECT_EQ(on_not_empty_count, 1);
  fifo_->Pop();
  EXPECT_EQ(on_empty_count, 1);
  EXPECT_EQ(on_not_empty_count, 1);
  db->DecRef();
}

}  // namespace
