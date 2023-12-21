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

#ifndef MPACT_SIM_DECODER_PROTO_ENCODING_GROUP_H_
#define MPACT_SIM_DECODER_PROTO_ENCODING_GROUP_H_

#include <cstdint>
#include <string>
#include <vector>

#include "absl/container/btree_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/strings/string_view.h"
#include "src/google/protobuf/descriptor.h"

// This file defines the ProtoEncodingGroup class, which is used in a hierarchy
// to divide instruction encodings into groups that can be differentiated based
// on a single field value (or has_*() reference). Based on how complex the
// instruction encoding is, this can create a hierarchy that are several levels
// deep.

namespace mpact {
namespace sim {
namespace decoder {

class DecoderErrorListener;

namespace proto_fmt {

// Forward declarations.
class ProtoConstraintValueSet;
class ProtoInstructionEncoding;
class ProtoInstructionGroup;
struct FieldInfo;

class ProtoEncodingGroup {
 public:
  // Constructor for a top level encoding group with no field differentiator.
  ProtoEncodingGroup(ProtoInstructionGroup *inst_group, int level,
                     DecoderErrorListener *error_listener);
  // Constructor for a child encoding group.
  ProtoEncodingGroup(ProtoEncodingGroup *parent,
                     ProtoInstructionGroup *inst_group, int level,
                     DecoderErrorListener *error_listener);
  // Deleted constructors.
  ProtoEncodingGroup() = delete;
  ProtoEncodingGroup(const ProtoEncodingGroup &) = delete;
  ProtoEncodingGroup &operator=(const ProtoEncodingGroup &) = delete;
  // Destructor.
  ~ProtoEncodingGroup();

  // Add encoding to the current group.
  void AddEncoding(ProtoInstructionEncoding *encoding);
  // Process the encodings in this group and divide them into subgroups based
  // on their constraint value for the differentiating field.
  void AddSubGroups();
  // Top level function called for creating C++ code for the decoder.
  std::string EmitDecoders(absl::string_view fcn_name,
                           absl::string_view opcode_enum,
                           absl::string_view message_type_name);

  // Getters/Setters.
  DecoderErrorListener *error_listener() const { return error_listener_; }
  int64_t value() const { return value_; }
  void set_value(int64_t value) { value_ = value; }
  int level() const { return level_; }

 private:
  // Check the encodings after the subgroups have been created and make sure
  // there are no encoding ambiguities, such as multiple opcodes with the same
  // encoding, etc.
  void CheckEncodings();
  bool DoConstraintsOverlap(const std::vector<ProtoConstraintValueSet *> &lhs,
                            const std::vector<ProtoConstraintValueSet *> &rhs);
  // Create C++ code for leaf (encoding group) instruction decoder.
  std::string EmitLeafDecoder(absl::string_view fcn_name,
                              absl::string_view opcode_enum,
                              absl::string_view message_type_name,
                              int indent_width) const;
  // Create C++ code for intermediary instruction decoder.
  std::string EmitComplexDecoder(absl::string_view fcn_name,
                                 absl::string_view opcode_enum,
                                 absl::string_view message_type_name);

  ProtoInstructionGroup *inst_group_ = nullptr;
  ProtoEncodingGroup *parent_ = nullptr;
  DecoderErrorListener *error_listener_ = nullptr;
  FieldInfo *differentiator_ = nullptr;
  int64_t value_;
  int level_;
  std::vector<ProtoInstructionEncoding *> encoding_vec_;
  std::vector<ProtoEncodingGroup *> encoding_group_vec_;
  absl::btree_map<std::string, FieldInfo *> field_map_;
  absl::flat_hash_set<const google::protobuf::FieldDescriptor *>
      other_field_set_;
  absl::flat_hash_set<const google::protobuf::OneofDescriptor *>
      other_oneof_set_;
};

}  // namespace proto_fmt
}  // namespace decoder
}  // namespace sim
}  // namespace mpact

#endif  // MPACT_SIM_DECODER_PROTO_ENCODING_GROUP_H_
