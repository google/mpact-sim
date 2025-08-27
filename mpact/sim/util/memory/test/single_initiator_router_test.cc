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

#include "mpact/sim/util/memory/single_initiator_router.h"

#include <cstdint>
#include <memory>

#include "absl/log/check.h"
#include "absl/status/status.h"
#include "googlemock/include/gmock/gmock.h"
#include "googletest/include/gtest/gtest.h"
#include "mpact/sim/generic/data_buffer.h"
#include "mpact/sim/util/memory/memory_interface.h"
#include "mpact/sim/util/memory/tagged_memory_interface.h"
#include "mpact/sim/util/memory/test/dummy_memory.h"

namespace {

using ::mpact::sim::generic::DataBufferFactory;
using ::mpact::sim::util::AtomicMemoryOpInterface;
using ::mpact::sim::util::MemoryInterface;
using ::mpact::sim::util::SingleInitiatorRouter;
using ::mpact::sim::util::TaggedMemoryInterface;
using ::mpact::sim::util::test::DummyMemory;

// First test that the loads/stores go through to a single target.
TEST(SingleInitiatorRouterTest, MemoryTargetLoadStore) {
  DataBufferFactory db_factory;
  auto router = std::make_unique<SingleInitiatorRouter>("test");
  auto memory = std::make_unique<DummyMemory>();
  auto* memory_target = static_cast<MemoryInterface*>(memory.get());

  EXPECT_TRUE(router->AddTarget(memory_target, 0, 0xffff'ffff'ffff'ffff).ok());
  auto* db = db_factory.Allocate<uint32_t>(1);
  auto* tag_db = db_factory.Allocate<uint8_t>(1);

  // Verify that only the access on the correct interface call go through.
  router->Load(0x1000, db, nullptr, nullptr);
  EXPECT_EQ(memory->load_address(), 0x1000);
  router->Store(0x2000, db);
  EXPECT_EQ(memory->store_address(), 0x2000);

  // These should not update the address as they use the other interface calls.
  router->Load(0x3000, db, tag_db, nullptr, nullptr);
  EXPECT_EQ(memory->tagged_load_address(), 0);
  router->Store(0x4000, db, tag_db);
  EXPECT_EQ(memory->tagged_store_address(), 0);
  EXPECT_FALSE(router
                   ->PerformMemoryOp(
                       0x5000, AtomicMemoryOpInterface::Operation::kAtomicAdd,
                       db, nullptr, nullptr)
                   .ok());
  EXPECT_EQ(memory->memory_op_address(), 0);

  tag_db->DecRef();
  db->DecRef();
}

// Make sure vector loads/stores are captured.
TEST(SingleInitiatorRouterTest, MemoryTargetVectorLoadStore) {
  DataBufferFactory db_factory;
  auto router = std::make_unique<SingleInitiatorRouter>("test");
  auto memory = std::make_unique<DummyMemory>();
  auto* memory_target = static_cast<MemoryInterface*>(memory.get());

  EXPECT_TRUE(router->AddTarget(memory_target, 0, 0xffff'ffff'ffff'ffff).ok());
  auto* db = db_factory.Allocate<uint32_t>(2);
  auto* address_db = db_factory.Allocate<uint64_t>(2);
  auto* mask_db = db_factory.Allocate<uint8_t>(2);
  address_db->Set<uint64_t>(0, 0x1000);
  address_db->Set<uint64_t>(1, 0x2000);
  mask_db->Set<uint8_t>(0, 1);
  mask_db->Set<uint8_t>(1, 1);
  router->Load(address_db, mask_db, sizeof(uint32_t), db, nullptr, nullptr);
  EXPECT_EQ(memory->vector_load_address(), 0x1000);

  address_db->Set<uint64_t>(0, 0x3000);
  address_db->Set<uint64_t>(1, 0x4000);
  router->Store(address_db, mask_db, sizeof(uint32_t), db);
  EXPECT_EQ(memory->vector_store_address(), 0x3000);

  db->DecRef();
  address_db->DecRef();
  mask_db->DecRef();
}

// Test tagged memory loads and stores.
TEST(SingleInitiatorRouterTest, TaggedTargetLoadStore) {
  DataBufferFactory db_factory;
  auto router = std::make_unique<SingleInitiatorRouter>("test");
  auto tagged = std::make_unique<DummyMemory>();
  auto* tagged_target = static_cast<TaggedMemoryInterface*>(tagged.get());

  EXPECT_TRUE(router->AddTarget(tagged_target, 0, 0xffff'ffff'ffff'ffff).ok());
  auto* db = db_factory.Allocate<uint32_t>(1);
  auto* tag_db = db_factory.Allocate<uint8_t>(1);
  // Verify that only the access on the correct interface call go through.
  router->Load(0x1000, db, nullptr, nullptr);
  EXPECT_EQ(tagged->load_address(), 0x1000);

  router->Store(0x2000, db);
  EXPECT_EQ(tagged->store_address(), 0x2000);

  router->Load(0x3000, db, tag_db, nullptr, nullptr);
  EXPECT_EQ(tagged->tagged_load_address(), 0x3000);

  router->Store(0x4000, db, tag_db);
  EXPECT_EQ(tagged->tagged_store_address(), 0x4000);

  // This should not update the address.
  EXPECT_FALSE(router
                   ->PerformMemoryOp(
                       0x5000, AtomicMemoryOpInterface::Operation::kAtomicAdd,
                       db, nullptr, nullptr)
                   .ok());
  EXPECT_EQ(tagged->memory_op_address(), 0);
  tag_db->DecRef();
  db->DecRef();
}

// Make sure vector loads/stores are captured.
TEST(SingleInitiatorRouterTest, TaggedTargetVectorLoadStore) {
  DataBufferFactory db_factory;
  auto router = std::make_unique<SingleInitiatorRouter>("test");
  auto tagged = std::make_unique<DummyMemory>();
  auto* tagged_target = static_cast<TaggedMemoryInterface*>(tagged.get());

  EXPECT_TRUE(router->AddTarget(tagged_target, 0, 0xffff'ffff'ffff'ffff).ok());
  auto* db = db_factory.Allocate<uint32_t>(2);
  auto* address_db = db_factory.Allocate<uint64_t>(2);
  auto* mask_db = db_factory.Allocate<uint8_t>(2);
  address_db->Set<uint64_t>(0, 0x1000);
  address_db->Set<uint64_t>(1, 0x2000);
  mask_db->Set<uint8_t>(0, 1);
  mask_db->Set<uint8_t>(1, 1);
  router->Load(address_db, mask_db, sizeof(uint32_t), db, nullptr, nullptr);
  EXPECT_EQ(tagged->vector_load_address(), 0x1000);

  address_db->Set<uint64_t>(0, 0x3000);
  address_db->Set<uint64_t>(1, 0x4000);
  router->Store(address_db, mask_db, sizeof(uint32_t), db);
  EXPECT_EQ(tagged->vector_store_address(), 0x3000);

  db->DecRef();
  address_db->DecRef();
  mask_db->DecRef();
}

// Test atomic memory operations.
TEST(SingleInitiatorRouterTest, SingleAtomicTarget) {
  DataBufferFactory db_factory;
  auto router = std::make_unique<SingleInitiatorRouter>("test");
  auto atomic = std::make_unique<DummyMemory>();
  auto* atomic_target = static_cast<AtomicMemoryOpInterface*>(atomic.get());
  EXPECT_TRUE(router->AddTarget(atomic_target, 0, 0xffff'ffff'ffff'ffff).ok());
  auto* db = db_factory.Allocate<uint32_t>(1);
  auto* tag_db = db_factory.Allocate<uint8_t>(1);
  // These should not update the address.
  router->Load(0x1000, db, nullptr, nullptr);
  EXPECT_EQ(atomic->load_address(), 0);
  router->Store(0x2000, db);
  EXPECT_EQ(atomic->load_address(), 0);
  router->Load(0x3000, db, tag_db, nullptr, nullptr);
  EXPECT_EQ(atomic->tagged_load_address(), 0);
  router->Store(0x4000, db, tag_db);
  EXPECT_EQ(atomic->tagged_store_address(), 0);
  // Verify that only the access on the correct interface call go through.
  EXPECT_TRUE(router
                  ->PerformMemoryOp(
                      0x5000, AtomicMemoryOpInterface::Operation::kAtomicAdd,
                      db, nullptr, nullptr)
                  .ok());
  EXPECT_EQ(atomic->memory_op_address(), 0x5000);
  tag_db->DecRef();
  db->DecRef();
}

// Test routing to different targets.
TEST(SingleInitiatorRouterTest, MultiTargetMemory) {
  DataBufferFactory db_factory;
  auto router = std::make_unique<SingleInitiatorRouter>("test");
  auto memory0 = std::make_unique<DummyMemory>();
  auto memory1 = std::make_unique<DummyMemory>();
  auto memory2 = std::make_unique<DummyMemory>();
  auto default_memory = std::make_unique<DummyMemory>();
  auto* memory_target0 = static_cast<MemoryInterface*>(memory0.get());
  auto* memory_target1 = static_cast<MemoryInterface*>(memory1.get());
  auto* memory_target2 = static_cast<MemoryInterface*>(memory2.get());
  auto* default_target = static_cast<MemoryInterface*>(default_memory.get());
  auto* db = db_factory.Allocate<uint32_t>(1);

  // Add 3 targets at different areas in the memory map.
  EXPECT_TRUE(
      router->AddTarget(memory_target0, 0x1'0000'0000, 0x1'0000'ffff).ok());
  EXPECT_TRUE(
      router->AddTarget(memory_target1, 0x3'0000'0000, 0x3'0000'ffff).ok());
  EXPECT_TRUE(
      router->AddTarget(memory_target2, 0x5'0000'0000, 0x5'0000'ffff).ok());
  EXPECT_TRUE(router->AddDefaultTarget(default_target).ok());

  // Make sure access addresses hit the expected target.

  // Memory 0.
  router->Load(0x1'0000'1000, db, nullptr, nullptr);
  router->Store(0x1'0000'2000, db);
  EXPECT_EQ(memory0->load_address(), 0x1'0000'1000);
  EXPECT_EQ(memory0->store_address(), 0x1'0000'2000);
  // Memory 1.
  router->Load(0x3'0000'1000, db, nullptr, nullptr);
  router->Store(0x3'0000'2000, db);
  EXPECT_EQ(memory1->load_address(), 0x3'0000'1000);
  EXPECT_EQ(memory1->store_address(), 0x3'0000'2000);
  // Memory 2.
  router->Load(0x5'0000'1000, db, nullptr, nullptr);
  router->Store(0x5'0000'2000, db);
  EXPECT_EQ(memory2->load_address(), 0x5'0000'1000);
  EXPECT_EQ(memory2->store_address(), 0x5'0000'2000);

  // An access outside the memory map should hit the default target.
  memory0->ClearValues();
  memory1->ClearValues();
  memory2->ClearValues();
  default_memory->ClearValues();
  router->Load(0x2'0000'0000, db, nullptr, nullptr);
  router->Store(0x2'0000'2000, db);
  EXPECT_EQ(memory0->load_address(), 0);
  EXPECT_EQ(memory1->load_address(), 0);
  EXPECT_EQ(memory2->load_address(), 0);
  EXPECT_EQ(memory0->store_address(), 0);
  EXPECT_EQ(memory1->store_address(), 0);
  EXPECT_EQ(memory2->store_address(), 0);
  EXPECT_EQ(default_memory->load_address(), 0x2'0000'0000);
  EXPECT_EQ(default_memory->store_address(), 0x2'0000'2000);

  // An access partially overlapping the memory should not hit any of the
  // targets.
  router->Load(0x0'ffff'fffe, db, nullptr, nullptr);
  default_memory->ClearValues();
  EXPECT_EQ(memory0->load_address(), 0);
  EXPECT_EQ(memory1->load_address(), 0);
  EXPECT_EQ(memory2->load_address(), 0);
  EXPECT_EQ(memory0->store_address(), 0);
  EXPECT_EQ(memory1->store_address(), 0);
  EXPECT_EQ(memory2->store_address(), 0);
  EXPECT_EQ(default_memory->load_address(), 0);
  EXPECT_EQ(default_memory->store_address(), 0);

  db->DecRef();
}

// Test routing to different targets.
TEST(SingleInitiatorRouterTest, MultiTargetTaggedMemory) {
  DataBufferFactory db_factory;
  auto router = std::make_unique<SingleInitiatorRouter>("test");
  auto memory0 = std::make_unique<DummyMemory>();
  auto memory1 = std::make_unique<DummyMemory>();
  auto memory2 = std::make_unique<DummyMemory>();
  auto default_memory = std::make_unique<DummyMemory>();
  auto* memory_target0 = static_cast<TaggedMemoryInterface*>(memory0.get());
  auto* memory_target1 = static_cast<TaggedMemoryInterface*>(memory1.get());
  auto* memory_target2 = static_cast<TaggedMemoryInterface*>(memory2.get());
  auto* default_target =
      static_cast<TaggedMemoryInterface*>(default_memory.get());
  auto* db = db_factory.Allocate<uint32_t>(1);
  auto* tag_db = db_factory.Allocate<uint8_t>(1);

  // Add 3 targets at different areas in the memory map.
  EXPECT_TRUE(
      router->AddTarget(memory_target0, 0x1'0000'0000, 0x1'0000'ffff).ok());
  EXPECT_TRUE(
      router->AddTarget(memory_target1, 0x3'0000'0000, 0x3'0000'ffff).ok());
  EXPECT_TRUE(
      router->AddTarget(memory_target2, 0x5'0000'0000, 0x5'0000'ffff).ok());
  EXPECT_TRUE(router->AddDefaultTarget(default_target).ok());

  // Make sure access addresses hit the expected target.

  // Memory 0.
  router->Load(0x1'0000'1000, db, tag_db, nullptr, nullptr);
  router->Store(0x1'0000'2000, db, tag_db);
  EXPECT_EQ(memory0->tagged_load_address(), 0x1'0000'1000);
  EXPECT_EQ(memory0->tagged_store_address(), 0x1'0000'2000);
  // Memory 1.
  router->Load(0x3'0000'1000, db, tag_db, nullptr, nullptr);
  router->Store(0x3'0000'2000, db, tag_db);
  EXPECT_EQ(memory1->tagged_load_address(), 0x3'0000'1000);
  EXPECT_EQ(memory1->tagged_store_address(), 0x3'0000'2000);
  // Memory 2.
  router->Load(0x5'0000'1000, db, tag_db, nullptr, nullptr);
  router->Store(0x5'0000'2000, db, tag_db);
  EXPECT_EQ(memory2->tagged_load_address(), 0x5'0000'1000);
  EXPECT_EQ(memory2->tagged_store_address(), 0x5'0000'2000);

  // An access outside the memory map should hit the default target.
  memory0->ClearValues();
  memory1->ClearValues();
  memory2->ClearValues();
  default_memory->ClearValues();
  router->Load(0x2'0000'0000, db, tag_db, nullptr, nullptr);
  router->Store(0x2'0000'2000, db, tag_db);
  EXPECT_EQ(memory0->tagged_load_address(), 0);
  EXPECT_EQ(memory1->tagged_load_address(), 0);
  EXPECT_EQ(memory2->tagged_load_address(), 0);
  EXPECT_EQ(memory0->tagged_store_address(), 0);
  EXPECT_EQ(memory1->tagged_store_address(), 0);
  EXPECT_EQ(memory2->tagged_store_address(), 0);
  EXPECT_EQ(default_memory->tagged_load_address(), 0x2'0000'0000);
  EXPECT_EQ(default_memory->tagged_store_address(), 0x2'0000'2000);

  // An access partially overlapping the memory should not hit any of the
  // targets.
  router->Load(0x0'ffff'fffe, db, tag_db, nullptr, nullptr);
  router->Store(0x0'ffff'fffe, db, tag_db);
  default_memory->ClearValues();
  EXPECT_EQ(memory0->tagged_load_address(), 0);
  EXPECT_EQ(memory1->tagged_load_address(), 0);
  EXPECT_EQ(memory2->tagged_load_address(), 0);
  EXPECT_EQ(memory0->tagged_store_address(), 0);
  EXPECT_EQ(memory1->tagged_store_address(), 0);
  EXPECT_EQ(memory2->tagged_store_address(), 0);
  EXPECT_EQ(default_memory->tagged_load_address(), 0);
  EXPECT_EQ(default_memory->tagged_store_address(), 0);

  tag_db->DecRef();
  db->DecRef();
}

// Test routing to different targets.
TEST(SingleInitiatorRouterTest, MultiTargetVectorMemory) {
  DataBufferFactory db_factory;
  auto router = std::make_unique<SingleInitiatorRouter>("test");
  auto memory0 = std::make_unique<DummyMemory>();
  auto memory1 = std::make_unique<DummyMemory>();
  auto memory2 = std::make_unique<DummyMemory>();
  auto* memory_target0 = static_cast<MemoryInterface*>(memory0.get());
  auto* memory_target1 = static_cast<MemoryInterface*>(memory1.get());
  auto* memory_target2 = static_cast<MemoryInterface*>(memory2.get());
  auto* address_db = db_factory.Allocate<uint64_t>(2);
  auto* mask_db = db_factory.Allocate<uint8_t>(2);
  auto* db = db_factory.Allocate<uint32_t>(2);
  mask_db->Set<uint8_t>(0, 1);
  mask_db->Set<uint8_t>(1, 1);

  // Add 3 targets at different areas in the memory map.
  EXPECT_TRUE(
      router->AddTarget(memory_target0, 0x1'0000'0000, 0x1'0000'ffff).ok());
  EXPECT_TRUE(
      router->AddTarget(memory_target1, 0x3'0000'0000, 0x3'0000'ffff).ok());
  EXPECT_TRUE(
      router->AddTarget(memory_target2, 0x5'0000'0000, 0x5'0000'ffff).ok());

  // Make sure access addresses hit the expected target.

  // Memory 0.
  address_db->Set<uint64_t>(0, 0x1'0000'1000);
  address_db->Set<uint64_t>(1, 0x1'0000'2000);
  router->Load(address_db, mask_db, sizeof(uint32_t), db, nullptr, nullptr);
  address_db->Set<uint64_t>(0, 0x1'0000'3000);
  address_db->Set<uint64_t>(1, 0x1'0000'4000);
  router->Store(address_db, mask_db, sizeof(uint32_t), db);
  EXPECT_EQ(memory0->vector_load_address(), 0x1'0000'1000);
  EXPECT_EQ(memory0->vector_store_address(), 0x1'0000'3000);
  // Memory 1.
  address_db->Set<uint64_t>(0, 0x3'0000'1000);
  address_db->Set<uint64_t>(1, 0x3'0000'2000);
  router->Load(address_db, mask_db, sizeof(uint32_t), db, nullptr, nullptr);
  address_db->Set<uint64_t>(0, 0x3'0000'3000);
  address_db->Set<uint64_t>(1, 0x3'0000'4000);
  router->Store(address_db, mask_db, sizeof(uint32_t), db);
  EXPECT_EQ(memory1->vector_load_address(), 0x3'0000'1000);
  EXPECT_EQ(memory1->vector_store_address(), 0x3'0000'3000);
  // Memory 2.
  address_db->Set<uint64_t>(0, 0x5'0000'1000);
  address_db->Set<uint64_t>(1, 0x5'0000'2000);
  router->Load(address_db, mask_db, sizeof(uint32_t), db, nullptr, nullptr);
  address_db->Set<uint64_t>(0, 0x5'0000'3000);
  address_db->Set<uint64_t>(1, 0x5'0000'4000);
  router->Store(address_db, mask_db, sizeof(uint32_t), db);
  EXPECT_EQ(memory2->vector_load_address(), 0x5'0000'1000);
  EXPECT_EQ(memory2->vector_store_address(), 0x5'0000'3000);

  // An access outside the memory map should not hit any of the targets.
  memory0->ClearValues();
  memory1->ClearValues();
  memory2->ClearValues();
  address_db->Set<uint64_t>(0, 0x2'0000'1000);
  address_db->Set<uint64_t>(1, 0x1'0000'2000);
  router->Load(address_db, mask_db, sizeof(uint32_t), db, nullptr, nullptr);
  router->Store(address_db, mask_db, sizeof(uint32_t), db);
  EXPECT_EQ(memory0->vector_load_address(), 0);
  EXPECT_EQ(memory1->vector_load_address(), 0);
  EXPECT_EQ(memory2->vector_load_address(), 0);
  EXPECT_EQ(memory0->vector_store_address(), 0);
  EXPECT_EQ(memory1->vector_store_address(), 0);
  EXPECT_EQ(memory2->vector_store_address(), 0);

  // An access partially overlapping the memory should not hit any of the
  // targets.
  address_db->Set<uint64_t>(0, 0x0'ffff'fffe);
  address_db->Set<uint64_t>(1, 0x1'0000'2000);
  router->Load(address_db, mask_db, sizeof(uint32_t), db, nullptr, nullptr);
  router->Store(address_db, mask_db, sizeof(uint32_t), db);
  EXPECT_EQ(memory0->vector_load_address(), 0);
  EXPECT_EQ(memory1->vector_load_address(), 0);
  EXPECT_EQ(memory2->vector_load_address(), 0);
  EXPECT_EQ(memory0->vector_store_address(), 0);
  EXPECT_EQ(memory1->vector_store_address(), 0);
  EXPECT_EQ(memory2->vector_store_address(), 0);

  address_db->DecRef();
  mask_db->DecRef();
  db->DecRef();
}

// Test routing to different targets.
TEST(SingleInitiatorRouterTest, MultiTargetVectorTaggedMemory) {
  DataBufferFactory db_factory;
  auto router = std::make_unique<SingleInitiatorRouter>("test");
  auto memory0 = std::make_unique<DummyMemory>();
  auto memory1 = std::make_unique<DummyMemory>();
  auto memory2 = std::make_unique<DummyMemory>();
  auto* tagged_target0 = static_cast<TaggedMemoryInterface*>(memory0.get());
  auto* tagged_target1 = static_cast<TaggedMemoryInterface*>(memory1.get());
  auto* tagged_target2 = static_cast<TaggedMemoryInterface*>(memory2.get());
  auto* address_db = db_factory.Allocate<uint64_t>(2);
  auto* mask_db = db_factory.Allocate<uint8_t>(2);
  auto* db = db_factory.Allocate<uint32_t>(2);
  mask_db->Set<uint8_t>(0, 1);
  mask_db->Set<uint8_t>(1, 1);

  // Add 3 targets at different areas in the memory map.
  EXPECT_TRUE(
      router->AddTarget(tagged_target0, 0x1'0000'0000, 0x1'0000'ffff).ok());
  EXPECT_TRUE(
      router->AddTarget(tagged_target1, 0x3'0000'0000, 0x3'0000'ffff).ok());
  EXPECT_TRUE(
      router->AddTarget(tagged_target2, 0x5'0000'0000, 0x5'0000'ffff).ok());

  // Make sure access addresses hit the expected target.

  // Memory 0.
  address_db->Set<uint64_t>(0, 0x1'0000'1000);
  address_db->Set<uint64_t>(1, 0x1'0000'2000);
  router->Load(address_db, mask_db, sizeof(uint32_t), db, nullptr, nullptr);
  address_db->Set<uint64_t>(0, 0x1'0000'3000);
  address_db->Set<uint64_t>(1, 0x1'0000'4000);
  router->Store(address_db, mask_db, sizeof(uint32_t), db);
  EXPECT_EQ(memory0->vector_load_address(), 0x1'0000'1000);
  EXPECT_EQ(memory0->vector_store_address(), 0x1'0000'3000);
  // Memory 1.
  address_db->Set<uint64_t>(0, 0x3'0000'1000);
  address_db->Set<uint64_t>(1, 0x3'0000'2000);
  router->Load(address_db, mask_db, sizeof(uint32_t), db, nullptr, nullptr);
  address_db->Set<uint64_t>(0, 0x3'0000'3000);
  address_db->Set<uint64_t>(1, 0x3'0000'4000);
  router->Store(address_db, mask_db, sizeof(uint32_t), db);
  EXPECT_EQ(memory1->vector_load_address(), 0x3'0000'1000);
  EXPECT_EQ(memory1->vector_store_address(), 0x3'0000'3000);
  // Memory 2.
  address_db->Set<uint64_t>(0, 0x5'0000'1000);
  address_db->Set<uint64_t>(1, 0x5'0000'2000);
  router->Load(address_db, mask_db, sizeof(uint32_t), db, nullptr, nullptr);
  address_db->Set<uint64_t>(0, 0x5'0000'3000);
  address_db->Set<uint64_t>(1, 0x5'0000'4000);
  router->Store(address_db, mask_db, sizeof(uint32_t), db);
  EXPECT_EQ(memory2->vector_load_address(), 0x5'0000'1000);
  EXPECT_EQ(memory2->vector_store_address(), 0x5'0000'3000);

  // An access outside the memory map should not hit any of the targets.
  memory0->ClearValues();
  memory1->ClearValues();
  memory2->ClearValues();
  address_db->Set<uint64_t>(0, 0x2'0000'1000);
  address_db->Set<uint64_t>(1, 0x1'0000'2000);
  router->Load(address_db, mask_db, sizeof(uint32_t), db, nullptr, nullptr);
  router->Store(address_db, mask_db, sizeof(uint32_t), db);
  EXPECT_EQ(memory0->vector_load_address(), 0);
  EXPECT_EQ(memory1->vector_load_address(), 0);
  EXPECT_EQ(memory2->vector_load_address(), 0);
  EXPECT_EQ(memory0->vector_store_address(), 0);
  EXPECT_EQ(memory1->vector_store_address(), 0);
  EXPECT_EQ(memory2->vector_store_address(), 0);

  // An access partially overlapping the memory should not hit any of the
  // targets.
  address_db->Set<uint64_t>(0, 0x0'ffff'fffe);
  address_db->Set<uint64_t>(1, 0x1'0000'2000);
  router->Load(address_db, mask_db, sizeof(uint32_t), db, nullptr, nullptr);
  router->Store(address_db, mask_db, sizeof(uint32_t), db);
  EXPECT_EQ(memory0->vector_load_address(), 0);
  EXPECT_EQ(memory1->vector_load_address(), 0);
  EXPECT_EQ(memory2->vector_load_address(), 0);
  EXPECT_EQ(memory0->vector_store_address(), 0);
  EXPECT_EQ(memory1->vector_store_address(), 0);
  EXPECT_EQ(memory2->vector_store_address(), 0);

  address_db->DecRef();
  mask_db->DecRef();
  db->DecRef();
}

// Test routing to different targets.
TEST(SingleInitiatorRouterTest, MultiTargetAtomicMemory) {
  DataBufferFactory db_factory;
  auto router = std::make_unique<SingleInitiatorRouter>("test");
  auto memory0 = std::make_unique<DummyMemory>();
  auto memory1 = std::make_unique<DummyMemory>();
  auto memory2 = std::make_unique<DummyMemory>();
  auto default_memory = std::make_unique<DummyMemory>();
  auto* atomic_target0 = static_cast<AtomicMemoryOpInterface*>(memory0.get());
  auto* atomic_target1 = static_cast<AtomicMemoryOpInterface*>(memory1.get());
  auto* atomic_target2 = static_cast<AtomicMemoryOpInterface*>(memory2.get());
  auto* default_target =
      static_cast<AtomicMemoryOpInterface*>(default_memory.get());
  auto* db = db_factory.Allocate<uint32_t>(1);

  // Add 3 targets at different areas in the memory map.
  EXPECT_TRUE(
      router->AddTarget(atomic_target0, 0x1'0000'0000, 0x1'0000'ffffULL).ok());
  EXPECT_TRUE(
      router->AddTarget(atomic_target1, 0x3'0000'0000, 0x3'0000'ffffULL).ok());
  EXPECT_TRUE(
      router->AddTarget(atomic_target2, 0x5'0000'0000, 0x5'0000'ffffULL).ok());
  EXPECT_TRUE(router->AddDefaultTarget(default_target).ok());

  // Make sure access addresses hit the expected target.

  // Memory 0.
  EXPECT_TRUE(
      router
          ->PerformMemoryOp(0x1'0000'1000,
                            AtomicMemoryOpInterface::Operation::kAtomicAdd, db,
                            nullptr, nullptr)
          .ok());
  EXPECT_EQ(memory0->memory_op_address(), 0x1'0000'1000ULL);
  // Memory 1.
  EXPECT_TRUE(
      router
          ->PerformMemoryOp(0x3'0000'1000,
                            AtomicMemoryOpInterface::Operation::kAtomicAdd, db,
                            nullptr, nullptr)
          .ok());
  EXPECT_EQ(memory1->memory_op_address(), 0x3'0000'1000ULL);
  // Memory 2.
  EXPECT_TRUE(
      router
          ->PerformMemoryOp(0x5'0000'1000,
                            AtomicMemoryOpInterface::Operation::kAtomicAdd, db,
                            nullptr, nullptr)
          .ok());
  EXPECT_EQ(memory2->memory_op_address(), 0x5'0000'1000ULL);

  // An access outside the memory map should hit the default target.
  memory0->ClearValues();
  memory1->ClearValues();
  memory2->ClearValues();
  EXPECT_TRUE(
      router
          ->PerformMemoryOp(0x2'0000'1000ULL,
                            AtomicMemoryOpInterface::Operation::kAtomicAdd, db,
                            nullptr, nullptr)
          .ok());
  EXPECT_EQ(memory0->memory_op_address(), 0);
  EXPECT_EQ(memory1->memory_op_address(), 0);
  EXPECT_EQ(memory2->memory_op_address(), 0);
  EXPECT_EQ(default_memory->memory_op_address(), 0x2'0000'1000ULL);

  // An access partially overlapping the memory should not hit any of the
  // targets.
  default_memory->ClearValues();
  EXPECT_FALSE(
      router
          ->PerformMemoryOp(0x0'ffff'fffeULL,
                            AtomicMemoryOpInterface::Operation::kAtomicAdd, db,
                            nullptr, nullptr)
          .ok());
  EXPECT_EQ(memory0->memory_op_address(), 0);
  EXPECT_EQ(memory1->memory_op_address(), 0);
  EXPECT_EQ(memory2->memory_op_address(), 0);

  db->DecRef();
}
}  // namespace
