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

#include <cstdint>
#include <cstring>
#include <string>

#include "absl/types/span.h"
#include "mpact/sim/generic/arch_state.h"
#include "mpact/sim/generic/operand_interface.h"
#include "mpact/sim/generic/resource_operand_interface.h"  // IWYU pragma: keep

namespace mpact {
namespace sim {
namespace generic {

void Instruction::AppendChild(Instruction* inst) {
  if (nullptr == inst) return;
  inst->parent_ = this;
  if (nullptr == child_) {
    inst->IncRef();
    child_ = inst;
  } else {
    child_->Append(inst);
  }
}

void Instruction::Append(Instruction* inst) {
  if (nullptr == inst) return;
  if (nullptr == next_) {
    inst->IncRef();
    next_ = inst;
  } else {
    next_->Append(inst);
  }
}

void Instruction::SetPredicate(PredicateOperandInterface* predicate) {
  predicate_ = predicate;
}

void Instruction::AppendSource(SourceOperandInterface* op) {
  sources_.push_back(op);
}

int Instruction::SourcesSize() const { return sources_.size(); }

void Instruction::AppendDestination(DestinationOperandInterface* op) {
  dests_.push_back(op);
}

int Instruction::DestinationsSize() const { return dests_.size(); }

Instruction::Instruction(uint64_t address, ArchState* state)
    : predicate_(nullptr),
      address_(address),
      state_(state),
      context_(nullptr),
      child_(nullptr),
      parent_(nullptr),
      next_(nullptr) {}

Instruction::Instruction(ArchState* state) : Instruction(0, state) {}

Instruction::~Instruction() {
  delete[] attribute_array_;
  delete predicate_;
  for (auto* op : sources_) {
    delete op;
  }
  sources_.clear();
  for (auto* op : dests_) {
    delete op;
  }
  dests_.clear();
  for (auto* op : resource_hold_) {
    delete op;
  }
  resource_hold_.clear();
  for (auto* op : resource_acquire_) {
    delete op;
  }
  resource_acquire_.clear();
  if (parent_ != nullptr) {
    // remove reference to this from the parent
    parent_->child_ = nullptr;
  }
  if (next_ != nullptr) {
    next_->DecRef();
    next_ = nullptr;
  }
  if (child_ != nullptr) {
    child_->parent_ = nullptr;
    child_->DecRef();
    child_ = nullptr;
  }
}

std::string Instruction::AsString() const { return disasm_string_; }

void Instruction::SetAttributes(absl::Span<const int> attributes) {
  delete attribute_array_;
  attribute_array_ = new int[attributes.size()];
  std::memcpy(attribute_array_, attributes.data(),
              attributes.size() * sizeof(int));
  attributes_ = absl::Span<const int>(attribute_array_, attributes.size());
}

}  // namespace generic
}  // namespace sim
}  // namespace mpact
