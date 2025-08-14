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

#include "mpact/sim/util/memory/memory_router.h"

#include <cstdint>
#include <memory>

#include "absl/status/status.h"
#include "googlemock/include/gmock/gmock.h"
#include "googletest/include/gtest/gtest.h"
#include "mpact/sim/generic/data_buffer.h"
#include "mpact/sim/util/memory/memory_interface.h"
#include "mpact/sim/util/memory/single_initiator_router.h"
#include "mpact/sim/util/memory/tagged_memory_interface.h"
#include "mpact/sim/util/memory/test/dummy_memory.h"

// This file contains unit tests for the MemoryRouter class.
namespace {

using ::mpact::sim::generic::DataBufferFactory;
using ::mpact::sim::util::AtomicMemoryOpInterface;
using ::mpact::sim::util::MemoryInterface;
using ::mpact::sim::util::MemoryRouter;
using ::mpact::sim::util::SingleInitiatorRouter;
using ::mpact::sim::util::TaggedMemoryInterface;
using ::mpact::sim::util::test::DummyMemory;

TEST(MemoryRouterTest, AddInitiator) {
  auto memory_router = std::make_unique<MemoryRouter>();
  // Add initiator.
  auto* memory_initiator0 = memory_router->AddMemoryInitiator("initiator0");
  // Try to add a second initiator with the same name.
  auto* memory_initiator1 = memory_router->AddMemoryInitiator("initiator0");
  // They should be the same object.
  EXPECT_EQ(memory_initiator0, memory_initiator1);

  // Add tagged initiator by the same name - it should also be the same object.
  auto* tagged_initiator = memory_router->AddTaggedInitiator("initiator0");
  // Since the types are different, make sure that the pointers point to the
  // same area within the size of SingleInitiatorRouter.
  uint64_t p0 = reinterpret_cast<uint64_t>(memory_initiator0);
  uint64_t p1 = reinterpret_cast<uint64_t>(tagged_initiator);
  EXPECT_TRUE(((p0 >= p1) && (p0 < p1 + sizeof(SingleInitiatorRouter))) ||
              ((p1 >= p0) && (p1 < p0 + sizeof(SingleInitiatorRouter))));

  // Add atomic initiator by the same name - it should also be the same object.
  auto* atomic_initiator = memory_router->AddAtomicInitiator("initiator0");
  // Since the types are different, make sure that the pointers point to the
  // same area within the size of SingleInitiatorRouter.
  p0 = reinterpret_cast<uint64_t>(tagged_initiator);
  p1 = reinterpret_cast<uint64_t>(atomic_initiator);
  EXPECT_TRUE(((p0 >= p1) && (p0 < p1 + sizeof(SingleInitiatorRouter))) ||
              ((p1 >= p0) && (p1 < p0 + sizeof(SingleInitiatorRouter))));
}

TEST(MemoryRouterTest, AddTarget) {
  auto memory_router = std::make_unique<MemoryRouter>();
  auto memory = std::make_unique<DummyMemory>();
  // Add memory target.
  EXPECT_TRUE(memory_router
                  ->AddTarget("memory_target",
                              static_cast<MemoryInterface*>(memory.get()))
                  .ok());
  // Try adding it again, for each interface. It should fail.
  EXPECT_FALSE(memory_router
                   ->AddTarget("memory_target",
                               static_cast<MemoryInterface*>(memory.get()))
                   .ok());
  EXPECT_FALSE(
      memory_router
          ->AddTarget("memory_target",
                      static_cast<TaggedMemoryInterface*>(memory.get()))
          .ok());
  EXPECT_FALSE(
      memory_router
          ->AddTarget("memory_target",
                      static_cast<AtomicMemoryOpInterface*>(memory.get()))
          .ok());
  // Add the memory target with different names. This should work.
  EXPECT_TRUE(memory_router
                  ->AddTarget("memory_target_2",
                              static_cast<MemoryInterface*>(memory.get()))
                  .ok());
  EXPECT_TRUE(memory_router
                  ->AddTarget("tagged_target",
                              static_cast<TaggedMemoryInterface*>(memory.get()))
                  .ok());
  EXPECT_TRUE(
      memory_router
          ->AddTarget("atomic_target",
                      static_cast<AtomicMemoryOpInterface*>(memory.get()))
          .ok());
}

TEST(MemoryRouterTest, AddMapping) {
  auto memory_router = std::make_unique<MemoryRouter>();
  auto memory = std::make_unique<DummyMemory>();
  // Add initiator (ignoring the return value) and target.
  (void)memory_router->AddMemoryInitiator("initiator");
  EXPECT_TRUE(
      memory_router
          ->AddTarget("mem", static_cast<MemoryInterface*>(memory.get()))
          .ok());
  EXPECT_TRUE(
      memory_router->AddMapping("initiator", "mem", 0x1000, 0x1fff).ok());
  EXPECT_TRUE(
      memory_router->AddMapping("initiator", "mem", 0x1000, 0x1fff).ok());
  EXPECT_FALSE(
      memory_router->AddMapping("initiator", "mem", 0x800, 0x1800).ok());
  EXPECT_FALSE(memory_router->AddMapping("none", "mem", 0x2000, 0x2fff).ok());
  EXPECT_FALSE(
      memory_router->AddMapping("initiator", "none", 0x2000, 0x2fff).ok());
  EXPECT_TRUE(
      memory_router->AddMapping("initiator", "mem", 0x2000, 0x2fff).ok());
}

TEST(MemoryRouterTest, RoutingTest) {
  // Create a router with 2 initiators and 2 memory targets. With different
  // mappings for each initiator.
  DataBufferFactory factory;
  auto* db = factory.Allocate<uint32_t>(1);
  auto memory_router = std::make_unique<MemoryRouter>();
  auto memory0 = std::make_unique<DummyMemory>();
  auto memory1 = std::make_unique<DummyMemory>();
  auto* initiator0 = memory_router->AddMemoryInitiator("initiator0");
  auto* initiator1 = memory_router->AddMemoryInitiator("initiator1");
  EXPECT_TRUE(
      memory_router
          ->AddTarget("mem0", static_cast<MemoryInterface*>(memory0.get()))
          .ok());
  EXPECT_TRUE(
      memory_router
          ->AddTarget("mem1", static_cast<MemoryInterface*>(memory1.get()))
          .ok());
  EXPECT_TRUE(
      memory_router->AddMapping("initiator0", "mem0", 0x1000, 0x1fff).ok());
  EXPECT_TRUE(
      memory_router->AddMapping("initiator0", "mem1", 0x2000, 0x2fff).ok());
  EXPECT_TRUE(
      memory_router
          ->AddMapping("initiator1", "mem0", 0x1'0000'0000, 0x1'0000'ffff)
          .ok());
  EXPECT_TRUE(
      memory_router
          ->AddMapping("initiator1", "mem1", 0x2'0000'0000, 0x2'0000'ffff)
          .ok());
  // Load using initiator 0.
  initiator0->Load(0x1000, db, nullptr, nullptr);
  initiator0->Load(0x2000, db, nullptr, nullptr);
  EXPECT_EQ(memory0->load_address(), 0x1000);
  EXPECT_EQ(memory1->load_address(), 0x2000);
  memory0->ClearValues();
  memory1->ClearValues();
  // Load using initiator 1.
  initiator1->Load(0x1'0000'1000, db, nullptr, nullptr);
  initiator1->Load(0x2'0000'2000, db, nullptr, nullptr);
  EXPECT_EQ(memory0->load_address(), 0x1'0000'1000);
  EXPECT_EQ(memory1->load_address(), 0x2'0000'2000);
  EXPECT_EQ(memory0->load_address(), 0x1'0000'1000);
  EXPECT_EQ(memory1->load_address(), 0x2'0000'2000);

  db->DecRef();
}

}  // namespace
