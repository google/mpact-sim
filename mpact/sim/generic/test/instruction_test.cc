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

#include "mpact/sim/generic/instruction.h"

#include <memory>

#include "absl/types/span.h"
#include "googlemock/include/gmock/gmock.h"
#include "googletest/include/gtest/gtest.h"
#include "mpact/sim/generic/ref_count.h"

namespace mpact {
namespace sim {
namespace generic {
namespace {

struct InstructionContext : public ReferenceCount {
 public:
  int* value;
};

// Tests values of the instruction properties.
TEST(InstructionTest, BasicProperties) {
  auto inst = std::make_unique<Instruction>(0x1000, nullptr);
  EXPECT_EQ(inst->child(), nullptr);
  EXPECT_EQ(inst->parent(), nullptr);
  EXPECT_EQ(inst->next(), nullptr);
  EXPECT_EQ(inst->context(), nullptr);
  EXPECT_EQ(inst->state(), nullptr);
  EXPECT_EQ(inst->address(), 0x1000);
  EXPECT_EQ(inst->Predicate(), nullptr);

  inst->set_size(2);
  EXPECT_EQ(inst->size(), 2);
}

// Tests setting the semantic function and "Excecuting" the instruction
// with an execution context
TEST(InstructionTest, SemanticFunction) {
  auto inst = std::make_unique<Instruction>(0x1000, nullptr);
  int my_value = 0;
  int context_value = 1;
  auto my_context = std::make_unique<InstructionContext>();
  my_context->value = &context_value;

  inst->set_semantic_function([&](Instruction* inst) {
    EXPECT_EQ(inst->context(), static_cast<ReferenceCount*>(my_context.get()));
    my_value = 1;
    auto context = static_cast<InstructionContext*>(inst->context());
    ++(*context->value);
  });

  inst->Execute(my_context.get());
  EXPECT_EQ(my_value, 1);
  EXPECT_EQ(context_value, 2);
}

// Tests adding instructions to the ChildBundle list.
TEST(InstructionTest, ChildBundle) {
  auto inst = std::make_unique<Instruction>(0x1000, nullptr);

  Instruction* child0 = new Instruction(nullptr);
  Instruction* child1 = new Instruction(nullptr);

  inst->AppendChild(nullptr);
  EXPECT_EQ(inst->child(), nullptr);

  inst->AppendChild(child0);
  inst->AppendChild(child1);
  // inst owns the two instructions now, so can DecRef them so that they
  // will be deallocated when inst is destroyed.
  child0->DecRef();
  child1->DecRef();

  EXPECT_EQ(inst->child(), child0);
  EXPECT_EQ(child0->next(), child1);
  EXPECT_EQ(inst->child()->next(), child1);
  EXPECT_EQ(inst->child()->next()->next(), nullptr);
}

// Tests adding instructions to the "next" list.
TEST(InstructionTest, InstructionList) {
  auto inst = std::make_unique<Instruction>(0x1000, nullptr);

  Instruction* next0 = new Instruction(nullptr);
  Instruction* next1 = new Instruction(nullptr);

  inst->Append(nullptr);
  EXPECT_EQ(inst->next(), nullptr);

  inst->Append(next0);
  inst->Append(next1);
  // inst owns the two instructions now, so can DecRef them so that they
  // will be deallocated when inst is destroyed.
  next0->DecRef();
  next1->DecRef();

  EXPECT_EQ(inst->next(), next0);
  EXPECT_EQ(next0->next(), next1);
  EXPECT_EQ(inst->next()->next(), next1);
  EXPECT_EQ(inst->next()->next()->next(), nullptr);
}

TEST(InstructionTest, Attributes) {
  auto inst = std::make_unique<Instruction>(0x1000, nullptr);
  auto attr = inst->Attributes();
  EXPECT_EQ(attr.size(), 0);
  int my_array[] = {1, 2, 3, 4, 5};
  inst->SetAttributes(my_array);
  EXPECT_THAT(inst->Attributes(),
              testing::ElementsAreArray(absl::Span<int>(my_array)));
}

}  // namespace
}  // namespace generic
}  // namespace sim
}  // namespace mpact
