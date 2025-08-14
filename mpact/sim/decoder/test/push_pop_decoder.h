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
#ifndef MPACT_SIM_DECODER_TEST_PUSH_POP_DECODER_H_
#define MPACT_SIM_DECODER_TEST_PUSH_POP_DECODER_H_

#include <cstdint>
#include <memory>

#include "mpact/sim/decoder/test/push_pop_encoding.h"
#include "mpact/sim/decoder/test/push_pop_inst_decoder.h"
#include "mpact/sim/decoder/test/push_pop_inst_enums.h"
#include "mpact/sim/generic/arch_state.h"
#include "mpact/sim/generic/data_buffer.h"
#include "mpact/sim/generic/decoder_interface.h"
#include "mpact/sim/generic/instruction.h"
#include "mpact/sim/generic/type_helpers.h"
#include "mpact/sim/util/memory/memory_interface.h"

// This file defines the decoder class for the push/pop isa test case.

namespace mpact::sim::decoder::test {

using ::mpact::sim::generic::operator*;  // NOLINT

using ::mpact::sim::generic::ArchState;
using ::mpact::sim::generic::DataBuffer;
using ::mpact::sim::util::MemoryInterface;

class PushPopIsaFactory : public PushPopInstInstructionSetFactory {
 public:
  std::unique_ptr<PushPopInstSlot> CreatePushPopInstSlot(
      ArchState* state) override {
    return std::make_unique<PushPopInstSlot>(state);
  }
};

class PushPopDecoder : public generic::DecoderInterface {
 public:
  PushPopDecoder(ArchState* state, MemoryInterface* memory);
  PushPopDecoder() = delete;
  ~PushPopDecoder() override;

  generic::Instruction* DecodeInstruction(uint64_t address) override;
  int GetNumOpcodes() const override { return *OpcodeEnum::kPastMaxValue; }
  const char* GetOpcodeName(int index) const override {
    return kOpcodeNames[index];
  }

  PushPopEncoding* push_pop_encoding() const { return push_pop_encoding_; }

 private:
  ArchState* state_;
  MemoryInterface* memory_;
  PushPopEncoding* push_pop_encoding_;
  PushPopIsaFactory* push_pop_isa_factory_;
  PushPopInstInstructionSet* push_pop_isa_;
  DataBuffer* inst_db_;
};

}  // namespace mpact::sim::decoder::test

#endif  // MPACT_SIM_DECODER_TEST_PUSH_POP_DECODER_H_
