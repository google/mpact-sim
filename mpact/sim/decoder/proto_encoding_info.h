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

#ifndef MPACT_SIM_DECODER_PROTO_ENCODING_INFO_H_
#define MPACT_SIM_DECODER_PROTO_ENCODING_INFO_H_

#include <string>

#include "absl/container/btree_map.h"
#include "absl/container/btree_set.h"
#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "mpact/sim/decoder/decoder_error_listener.h"
#include "mpact/sim/decoder/proto_instruction_decoder.h"
#include "src/google/protobuf/descriptor.h"

// This class defines the ProtoEncodingInfo class that is used to keep and
// maintain the top level decoder information.

namespace mpact {
namespace sim {
namespace decoder {
namespace proto_fmt {

class ProtoInstructionDecoder;
class ProtoInstructionGroup;

struct StringPair {
  std::string h_file;
  std::string cc_file;
};

class ProtoEncodingInfo {
 public:
  ProtoEncodingInfo(const std::string &opcode_enum,
                    decoder::DecoderErrorListener *error_listener);
  ProtoEncodingInfo() = delete;
  ~ProtoEncodingInfo();
  // Add file to be included in the generated code.
  void AddIncludeFile(std::string include_file);
  // Create and return a proto instruction decoder object with the given name.
  ProtoInstructionDecoder *SetProtoDecoder(std::string name);
  // Create and return an instruction group object with the given name.
  absl::StatusOr<ProtoInstructionGroup *> AddInstructionGroup(
      const std::string &group_name,
      const google::protobuf::Descriptor *descriptor);
  // Check that the setter types are consistent.
  absl::Status CheckSetterType(
      absl::string_view name,
      const google::protobuf::FieldDescriptor *field_desc);
  // Generate the C++ .h and .cc file contents.
  StringPair GenerateDecoderClass();

  // Accessors.
  DecoderErrorListener *error_listener() { return error_listener_; }
  absl::flat_hash_map<std::string, ProtoInstructionGroup *> &
  instruction_group_map() {
    return instruction_group_map_;
  }
  const absl::btree_set<std::string> &include_files() const {
    return include_files_;
  }
  ProtoInstructionDecoder *decoder() const { return decoder_; }
  absl::string_view opcode_enum() const { return opcode_enum_; }

 private:
  std::string opcode_enum_;
  DecoderErrorListener *error_listener_;
  // Map of all instruction groups.
  absl::flat_hash_map<std::string, ProtoInstructionGroup *>
      instruction_group_map_;
  // The include files are stored in a btree set so they can be iterated over
  // in alphabetic order.
  absl::btree_set<std::string> include_files_;
  ProtoInstructionDecoder *decoder_ = nullptr;
  // Map of all setter types.
  absl::btree_map<std::string, google::protobuf::FieldDescriptor::CppType>
      setter_types_;
};

}  // namespace proto_fmt
}  // namespace decoder
}  // namespace sim
}  // namespace mpact

#endif  // MPACT_SIM_DECODER_PROTO_ENCODING_INFO_H_
