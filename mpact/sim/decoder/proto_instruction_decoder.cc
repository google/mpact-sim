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

#include "mpact/sim/decoder/proto_instruction_decoder.h"

#include <string>

#include "mpact/sim/decoder/proto_instruction_group.h"

namespace mpact {
namespace sim {
namespace decoder {
namespace proto_fmt {

ProtoInstructionDecoder::ProtoInstructionDecoder(
    std::string name, ProtoEncodingInfo* encoding_info,
    DecoderErrorListener* error_listener)
    : name_(name),
      encoding_info_(encoding_info),
      error_listener_(error_listener) {}

ProtoInstructionDecoder::~ProtoInstructionDecoder() {
  for (auto* group : instruction_groups_) delete group;
  instruction_groups_.clear();
}

void ProtoInstructionDecoder::AddInstructionGroup(
    ProtoInstructionGroup* inst_group) {
  instruction_groups_.push_back(inst_group);
}

}  // namespace proto_fmt
}  // namespace decoder
}  // namespace sim
}  // namespace mpact
