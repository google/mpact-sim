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

#ifndef MPACT_SIM_DECODER_PROTO_INSTRUCTION_DECODER_H_
#define MPACT_SIM_DECODER_PROTO_INSTRUCTION_DECODER_H_

#include <deque>
#include <string>
#include <vector>

#include "mpact/sim/decoder/proto_instruction_group.h"

// This class defines class for the instruction decoder.

namespace mpact {
namespace sim {
namespace decoder {

class DecoderErrorListener;

namespace proto_fmt {

class ProtoEncodingInfo;
class ProtoInstructionGroup;

class ProtoInstructionDecoder {
 public:
  ProtoInstructionDecoder(std::string name, ProtoEncodingInfo* encoding_info,
                          DecoderErrorListener* error_listener);
  ~ProtoInstructionDecoder();

  // Add an instruction group that will be part of this decoder.
  void AddInstructionGroup(ProtoInstructionGroup* inst_group);

  // Getters/Setters.
  const std::string& name() const { return name_; }
  DecoderErrorListener* error_listener() const { return error_listener_; }
  ProtoEncodingInfo* encoding_info() const { return encoding_info_; }
  std::vector<ProtoInstructionGroup*> instruction_groups() const {
    return instruction_groups_;
  }
  std::deque<std::string>& namespaces() { return namespaces_; }

 private:
  // Decoder name.
  std::string name_;
  // The global encoding structure.
  ProtoEncodingInfo* encoding_info_;
  // Error handler.
  DecoderErrorListener* error_listener_;
  // Namespace container.
  std::deque<std::string> namespaces_;
  // Instruction groups.
  std::vector<ProtoInstructionGroup*> instruction_groups_;
};

}  // namespace proto_fmt
}  // namespace decoder
}  // namespace sim
}  // namespace mpact

#endif  // MPACT_SIM_DECODER_PROTO_INSTRUCTION_DECODER_H_
