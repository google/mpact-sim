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

#ifndef MPACT_SIM_DECODER_PROTO_INSTRUCTION_GROUP_H_
#define MPACT_SIM_DECODER_PROTO_INSTRUCTION_GROUP_H_

#include <string>
#include <utility>
#include <vector>

#include "absl/container/btree_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "mpact/sim/decoder/decoder_error_listener.h"
#include "mpact/sim/decoder/proto_format_contexts.h"
#include "mpact/sim/decoder/proto_instruction_encoding.h"
#include "src/google/protobuf/descriptor.h"

namespace mpact {
namespace sim {
namespace decoder {
namespace proto_fmt {

class ProtoInstructionEncoding;
class ProtoEncodingInfo;
class ProtoEncodingGroup;

struct SetterInfo {
  SetterDefCtx* ctx;
  std::string name;
  const google::protobuf::FieldDescriptor* field_desc;
  std::vector<const google::protobuf::FieldDescriptor*> one_of_fields;
  IfNotCtx* if_not;
};

// This class represents an instruction group from the .proto_fmt file.
class ProtoInstructionGroup {
 public:
  ProtoInstructionGroup(std::string group_name,
                        const google::protobuf::Descriptor* message_type,
                        std::string opcode_enum,
                        ProtoEncodingInfo* encoding_info);
  ProtoInstructionGroup() = delete;
  ~ProtoInstructionGroup();

  // Add group level setter.
  absl::Status AddSetter(
      const std::string& group_name, SetterDefCtx* ctx,
      const std::string& setter_name,
      const google::protobuf::FieldDescriptor* field_desc,
      std::vector<const google::protobuf::FieldDescriptor*> one_of_fields,
      IfNotCtx* if_not);
  // Look up the setters in the named setter group. If found, return the begin
  // and end iterators for those setters.
  absl::StatusOr<
      std::pair<absl::btree_map<std::string, SetterInfo*>::const_iterator,
                absl::btree_map<std::string, SetterInfo*>::const_iterator>>
  GetSetterGroup(absl::string_view group) const;
  // Create and return an instruction encoding with the given name.
  ProtoInstructionEncoding* AddInstructionEncoding(std::string name);
  // Create a copy of the given instruction encoding.
  void CopyInstructionEncoding(ProtoInstructionEncoding* encoding);
  // Create an encoding group for this instruction group and then subdivide
  // it in a hierarchy as necessary.
  void ProcessEncodings(DecoderErrorListener* error_listener);

  // Generate the decoder for this instruction group.
  std::string GenerateDecoder() const;

  // Accessors.
  const std::string& name() const { return name_; }
  const google::protobuf::Descriptor* message_type() const {
    return message_type_;
  }
  const std::vector<ProtoInstructionEncoding*>& encodings() const {
    return encodings_;
  }
  const ProtoEncodingInfo* encoding_info() const { return encoding_info_; }

 private:
  std::string name_;
  const google::protobuf::Descriptor* message_type_;
  std::string opcode_enum_;
  ProtoEncodingInfo* encoding_info_;
  absl::flat_hash_set<std::string> encoding_name_set_;
  std::vector<ProtoInstructionEncoding*> encodings_;
  // Encoding group.
  ProtoEncodingGroup* encoding_group_ = nullptr;
  // Setter names and types.
  absl::btree_map<std::string, int> setter_name_to_type_;
  // Setter group map. Maps from setter group name to a map from setter name
  // to setter info.
  absl::btree_map<std::string, absl::btree_map<std::string, SetterInfo*>>
      setter_groups_;
};

}  // namespace proto_fmt
}  // namespace decoder
}  // namespace sim
}  // namespace mpact

#endif  // MPACT_SIM_DECODER_PROTO_INSTRUCTION_GROUP_H_
