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

#include "mpact/sim/decoder/test/push_pop_decoder.h"

#include <sys/types.h>

#include <cstdint>

#include "mpact/sim/decoder/test/push_pop_encoding.h"
#include "mpact/sim/decoder/test/push_pop_inst_decoder.h"
#include "mpact/sim/decoder/test/push_pop_inst_enums.h"
#include "mpact/sim/generic/instruction.h"
#include "mpact/sim/util/memory/memory_interface.h"

namespace mpact::sim::decoder::test {

using ::mpact::sim::generic::Instruction;
using ::mpact::sim::util::MemoryInterface;

PushPopDecoder::PushPopDecoder(ArchState *state, MemoryInterface *memory)
    : state_(state), memory_(memory) {
  push_pop_isa_factory_ = new PushPopIsaFactory();
  push_pop_isa_ = new PushPopInstInstructionSet(state, push_pop_isa_factory_);
  push_pop_encoding_ = new PushPopEncoding(state_);
  inst_db_ = state_->db_factory()->Allocate<uint16_t>(1);
}

PushPopDecoder::~PushPopDecoder() {
  inst_db_->DecRef();
  delete push_pop_encoding_;
  delete push_pop_isa_;
  delete push_pop_isa_factory_;
}

Instruction *PushPopDecoder::DecodeInstruction(uint64_t address) {
  memory_->Load(address, inst_db_, nullptr, nullptr);
  uint16_t iword = inst_db_->Get<uint16_t>(0);
  push_pop_encoding_->ParseInstruction(iword);
  auto *instruction = push_pop_isa_->Decode(address, push_pop_encoding_);
  instruction->set_opcode(
      *push_pop_encoding_->GetOpcode(SlotEnum::kPushPopInst, 0));
  return instruction;
}

}  // namespace mpact::sim::decoder::test
