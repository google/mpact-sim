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

#ifndef MPACT_SIM_DECODER_TEST_PUSH_POP_ENCODING_H_
#define MPACT_SIM_DECODER_TEST_PUSH_POP_ENCODING_H_

#include <cstdint>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/functional/any_invocable.h"
#include "mpact/sim/decoder/test/push_pop_inst_decoder.h"
#include "mpact/sim/decoder/test/push_pop_inst_enums.h"
#include "mpact/sim/generic/arch_state.h"
#include "mpact/sim/generic/operand_interface.h"

// This file defines the encoding class for the push/pop test case isa.
namespace mpact::sim::decoder::test {

using ::mpact::sim::generic::ArchState;
using ::mpact::sim::generic::DestinationOperandInterface;
using ::mpact::sim::generic::SourceOperandInterface;

class PushPopEncoding : public PushPopInstEncodingBase {
 public:
  explicit PushPopEncoding(ArchState* state);
  ~PushPopEncoding() override = default;

  void ParseInstruction(uint16_t inst_word);
  OpcodeEnum GetOpcode(SlotEnum slot, int entry) override { return opcode_; }
  OpcodeEnum opcode() const { return opcode_; }
  // Return a single source operand for the given slot, entry, opcode, and
  // source operand enum.
  SourceOperandInterface* GetSource(SlotEnum, int, OpcodeEnum, SourceOpEnum op,
                                    int source_no) override;
  // Expand the ListSourceOpEnum into a vector of source operands.
  std::vector<SourceOperandInterface*> GetSources(SlotEnum, int, OpcodeEnum,
                                                  ListSourceOpEnum op,
                                                  int source_no) override;
  // Return a single destination operand for the given slot, entry, opcode,
  // destination operand enum, and latency.
  DestinationOperandInterface* GetDestination(SlotEnum, int, OpcodeEnum,
                                              DestOpEnum op, int dest_no,
                                              int latency) override;
  // Expand the ListDestOpEnum into a vector of destination operands.
  std::vector<DestinationOperandInterface*> GetDestinations(
      SlotEnum, int, OpcodeEnum, ListDestOpEnum op, int dest_no,
      const std::vector<int>& latency) override;

 private:
  using SourceOpGetterMap =
      absl::flat_hash_map<int, absl::AnyInvocable<SourceOperandInterface*()>>;
  using DestOpGetterMap = absl::flat_hash_map<
      int, absl::AnyInvocable<DestinationOperandInterface*(int)>>;
  using ListSourceOpGetterMap = absl::flat_hash_map<
      int, absl::AnyInvocable<std::vector<SourceOperandInterface*>()>>;
  using ListDestOpGetterMap = absl::flat_hash_map<
      int, absl::AnyInvocable<std::vector<DestinationOperandInterface*>(
               const std::vector<int>&)>>;

  ArchState* state_;
  OpcodeEnum opcode_;
  uint16_t inst_word_;

  SourceOpGetterMap source_op_getters_;
  DestOpGetterMap dest_op_getters_;
  ListSourceOpGetterMap list_source_op_getters_;
  ListDestOpGetterMap list_dest_op_getters_;
};

}  // namespace mpact::sim::decoder::test

#endif  // MPACT_SIM_DECODER_TEST_PUSH_POP_ENCODING_H_
