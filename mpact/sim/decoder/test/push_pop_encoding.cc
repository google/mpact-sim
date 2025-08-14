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

#include "mpact/sim/decoder/test/push_pop_encoding.h"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "mpact/sim/decoder/test/push_pop_inst_bin_decoder.h"
#include "mpact/sim/decoder/test/push_pop_inst_enums.h"
#include "mpact/sim/generic/arch_state.h"
#include "mpact/sim/generic/immediate_operand.h"
#include "mpact/sim/generic/operand_interface.h"
#include "mpact/sim/generic/register.h"
#include "mpact/sim/generic/type_helpers.h"

namespace mpact::sim::decoder::test {

using ::mpact::sim::generic::operator*;  // NOLINT

using ::mpact::sim::generic::ArchState;
using ::mpact::sim::generic::ImmediateOperand;
using ::mpact::sim::generic::SourceOperandInterface;
using TestRegister = ::mpact::sim::generic::Register<uint32_t>;

template <typename RegType>
inline RegType* GetRegister(ArchState* state, std::string name) {
  auto iter = state->registers()->find(name);
  if (iter == state->registers()->end()) {
    return nullptr;
  }
  return static_cast<RegType*>(iter->second);
}

// Generic helper functions to create register operands.
template <typename RegType>
inline DestinationOperandInterface* GetRegisterDestinationOp(ArchState* state,
                                                             std::string name,
                                                             int latency) {
  auto* reg = GetRegister<RegType>(state, name);
  return reg->CreateDestinationOperand(latency);
}

template <typename RegType>
inline DestinationOperandInterface* GetRegisterDestinationOp(
    ArchState* state, std::string name, int latency, std::string op_name) {
  auto* reg = GetRegister<RegType>(state, name);
  return reg->CreateDestinationOperand(latency, op_name);
}

template <typename RegType>
inline SourceOperandInterface* GetRegisterSourceOp(ArchState* state,
                                                   std::string name) {
  auto* reg = GetRegister<RegType>(state, name);
  auto* op = reg->CreateSourceOperand();
  return op;
}

template <typename RegType>
inline SourceOperandInterface* GetRegisterSourceOp(ArchState* state,
                                                   std::string name,
                                                   std::string op_name) {
  auto* reg = GetRegister<RegType>(state, name);
  auto* op = reg->CreateSourceOperand(op_name);
  return op;
}

PushPopEncoding::PushPopEncoding(ArchState* state) : state_(state) {
  source_op_getters_.insert(std::make_pair(
      *SourceOpEnum::kRlist, [this]() -> SourceOperandInterface* {
        return new generic::ImmediateOperand<uint32_t>(
            p_type::ExtractRlist(inst_word_));
      }));

  source_op_getters_.insert(std::make_pair(
      *SourceOpEnum::kSpimm6, [this]() -> SourceOperandInterface* {
        return new generic::ImmediateOperand<uint32_t>(
            p_type::ExtractSpimm6(inst_word_));
      }));

  source_op_getters_.insert(
      std::make_pair(*SourceOpEnum::kX2, [this]() -> SourceOperandInterface* {
        return GetRegisterSourceOp<TestRegister>(state_, "x2");
      }));

  list_source_op_getters_.insert(std::make_pair(
      *ListSourceOpEnum::kRlist,
      [this]() -> std::vector<SourceOperandInterface*> {
        std::vector<SourceOperandInterface*> result;
        auto rlist = p_type::ExtractRlist(inst_word_);
        // Get the value of 'rlist', and add source operands accordingly.
        if (rlist < 4) return result;
        result.push_back(GetRegisterSourceOp<TestRegister>(state_, "x1"));
        if (rlist == 4) return result;
        result.push_back(GetRegisterSourceOp<TestRegister>(state_, "x8"));
        if (rlist == 5) return result;
        result.push_back(GetRegisterSourceOp<TestRegister>(state_, "x9"));
        if (rlist == 6) return result;
        result.push_back(GetRegisterSourceOp<TestRegister>(state_, "x18"));
        if (rlist == 7) return result;
        result.push_back(GetRegisterSourceOp<TestRegister>(state_, "x19"));
        if (rlist == 8) return result;
        result.push_back(GetRegisterSourceOp<TestRegister>(state_, "x20"));
        if (rlist == 9) return result;
        result.push_back(GetRegisterSourceOp<TestRegister>(state_, "x21"));
        if (rlist == 10) return result;
        result.push_back(GetRegisterSourceOp<TestRegister>(state_, "x22"));
        if (rlist == 11) return result;
        result.push_back(GetRegisterSourceOp<TestRegister>(state_, "x23"));
        if (rlist == 12) return result;
        result.push_back(GetRegisterSourceOp<TestRegister>(state_, "x24"));
        if (rlist == 13) return result;
        result.push_back(GetRegisterSourceOp<TestRegister>(state_, "x25"));
        if (rlist == 14) return result;
        result.push_back(GetRegisterSourceOp<TestRegister>(state_, "x26"));
        result.push_back(GetRegisterSourceOp<TestRegister>(state_, "x27"));
        return result;
      }));

  dest_op_getters_.insert(std::make_pair(
      *DestOpEnum::kX2, [this](int latency) -> DestinationOperandInterface* {
        return GetRegisterDestinationOp<TestRegister>(state_, "x2", latency);
      }));

  list_dest_op_getters_.insert(
      std::make_pair(*ListDestOpEnum::kRlist,
                     [this](const std::vector<int>& latency)
                         -> std::vector<DestinationOperandInterface*> {
                       std::vector<DestinationOperandInterface*> result;
                       // Get the value of 'rlist', and add destination operands
                       // accordingly.
                       auto rlist = p_type::ExtractRlist(inst_word_);
                       int size = latency.size();
                       if (rlist < 4) return result;
                       result.push_back(GetRegisterDestinationOp<TestRegister>(
                           state_, "x1", latency[result.size() % size]));
                       if (rlist == 4) return result;
                       result.push_back(GetRegisterDestinationOp<TestRegister>(
                           state_, "x8", latency[result.size() % size]));
                       if (rlist == 5) return result;
                       result.push_back(GetRegisterDestinationOp<TestRegister>(
                           state_, "x9", latency[result.size() % size]));
                       if (rlist == 6) return result;
                       result.push_back(GetRegisterDestinationOp<TestRegister>(
                           state_, "x18", latency[result.size() % size]));
                       if (rlist == 7) return result;
                       result.push_back(GetRegisterDestinationOp<TestRegister>(
                           state_, "x19", latency[result.size() % size]));
                       if (rlist == 8) return result;
                       result.push_back(GetRegisterDestinationOp<TestRegister>(
                           state_, "x20", latency[result.size() % size]));
                       if (rlist == 9) return result;
                       result.push_back(GetRegisterDestinationOp<TestRegister>(
                           state_, "x21", latency[result.size() % size]));
                       if (rlist == 10) return result;
                       result.push_back(GetRegisterDestinationOp<TestRegister>(
                           state_, "x22", latency[result.size() % size]));
                       if (rlist == 11) return result;
                       result.push_back(GetRegisterDestinationOp<TestRegister>(
                           state_, "x23", latency[result.size() % size]));
                       if (rlist == 12) return result;
                       result.push_back(GetRegisterDestinationOp<TestRegister>(
                           state_, "x24", latency[result.size() % size]));
                       if (rlist == 13) return result;
                       result.push_back(GetRegisterDestinationOp<TestRegister>(
                           state_, "x25", latency[result.size() % size]));
                       if (rlist == 14) return result;
                       result.push_back(GetRegisterDestinationOp<TestRegister>(
                           state_, "x26", latency[result.size() % size]));
                       result.push_back(GetRegisterDestinationOp<TestRegister>(
                           state_, "x27", latency[result.size() % size]));
                       return result;
                     }));
}

void PushPopEncoding::ParseInstruction(uint16_t inst_word) {
  inst_word_ = inst_word;
  opcode_ = DecodePushPop(inst_word_);
}

SourceOperandInterface* PushPopEncoding::GetSource(SlotEnum, int, OpcodeEnum,
                                                   SourceOpEnum op,
                                                   int source_no) {
  auto iter = source_op_getters_.find(*op);
  if (iter == source_op_getters_.end()) {
    return nullptr;
  }
  return iter->second();
}

std::vector<SourceOperandInterface*> PushPopEncoding::GetSources(
    SlotEnum, int, OpcodeEnum, ListSourceOpEnum op, int source_no) {
  auto iter = list_source_op_getters_.find(*op);
  if (iter == list_source_op_getters_.end()) {
    return {};
  }
  return iter->second();
}

DestinationOperandInterface* PushPopEncoding::GetDestination(
    SlotEnum, int, OpcodeEnum, DestOpEnum op, int dest_no, int latency) {
  auto iter = dest_op_getters_.find(*op);
  if (iter == dest_op_getters_.end()) {
    return nullptr;
  }
  return iter->second(latency);
}

std::vector<DestinationOperandInterface*> PushPopEncoding::GetDestinations(
    SlotEnum, int, OpcodeEnum, ListDestOpEnum op, int dest_no,
    const std::vector<int>& latency) {
  auto iter = list_dest_op_getters_.find(*op);
  if (iter == list_dest_op_getters_.end()) {
    return {};
  }
  return iter->second(latency);
}

}  // namespace mpact::sim::decoder::test
