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

#include "mpact/sim/decoder/proto_instruction_group.h"

#include <string>
#include <utility>
#include <vector>

#include "absl/container/btree_map.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_replace.h"
#include "absl/strings/string_view.h"
#include "mpact/sim/decoder/decoder_error_listener.h"
#include "mpact/sim/decoder/format_name.h"
#include "mpact/sim/decoder/proto_encoding_group.h"
#include "mpact/sim/decoder/proto_encoding_info.h"
#include "mpact/sim/decoder/proto_format_contexts.h"
#include "mpact/sim/decoder/proto_instruction_encoding.h"
#include "src/google/protobuf/descriptor.h"

namespace mpact {
namespace sim {
namespace decoder {
namespace proto_fmt {

using ::mpact::sim::machine_description::instruction_set::ToPascalCase;

ProtoInstructionGroup::ProtoInstructionGroup(
    std::string group_name, const proto2::Descriptor *message_type,
    std::string opcode_enum, ProtoEncodingInfo *encoding_info)
    : name_(group_name),
      message_type_(message_type),
      opcode_enum_(opcode_enum),
      encoding_info_(encoding_info) {}

ProtoInstructionGroup::~ProtoInstructionGroup() {
  for (auto *encoding : encodings_) {
    delete encoding;
  }
  encodings_.clear();
  delete encoding_group_;
  encoding_group_ = nullptr;
  for (auto &[unused, setter_map] : setter_groups_) {
    for (auto &[unused, setter_info] : setter_map) {
      delete setter_info;
    }
    setter_map.clear();
  }
  setter_groups_.clear();
}

ProtoInstructionEncoding *ProtoInstructionGroup::AddInstructionEncoding(
    std::string name) {
  auto *encoding = new ProtoInstructionEncoding(name, this);
  encodings_.push_back(encoding);
  return encoding;
}

absl::StatusOr<
    std::pair<absl::btree_map<std::string, SetterInfo *>::const_iterator,
              absl::btree_map<std::string, SetterInfo *>::const_iterator>>
ProtoInstructionGroup::GetSetterGroup(absl::string_view group) const {
  auto map_iter = setter_groups_.find(group);
  if (map_iter == setter_groups_.end()) {
    return absl::NotFoundError(absl::StrCat("No setter group '", group, "'."));
  }
  return std::pair<absl::btree_map<std::string, SetterInfo *>::const_iterator,
                   absl::btree_map<std::string, SetterInfo *>::const_iterator>{
      map_iter->second.begin(), map_iter->second.end()};
}

// Add a group level setter.
absl::Status ProtoInstructionGroup::AddSetter(
    const std::string &group_name, SetterDefCtx *ctx,
    const std::string &setter_name, const proto2::FieldDescriptor *field_desc,
    std::vector<const proto2::FieldDescriptor *> one_of_fields,
    IfNotCtx *if_not) {
  auto map_iter = setter_groups_.find(group_name);
  if (map_iter == setter_groups_.end()) {
    auto [iter, unused] = setter_groups_.insert({group_name, {}});
    map_iter = iter;
  }
  if (map_iter->second.contains(setter_name)) {
    return absl::AlreadyExistsError(
        absl::StrCat("Duplicate setter name '", setter_name,
                     "' in setter group '", group_name, "'."));
  }
  auto *setter_info =
      new SetterInfo({ctx, setter_name, field_desc, one_of_fields, if_not});
  map_iter->second.insert({setter_name, setter_info});
  return absl::OkStatus();
}

void ProtoInstructionGroup::CopyInstructionEncoding(
    ProtoInstructionEncoding *encoding) {
  if (encoding_name_set_.contains(encoding->name())) {
    encoding_info_->error_listener()->semanticWarning(
        nullptr, absl::StrCat("Duplicate instruction opcode name '",
                              encoding->name(), "' in group '", name(), "'."));
  }
  encoding_name_set_.insert(encoding->name());
  encodings_.push_back(encoding);
}

void ProtoInstructionGroup::ProcessEncodings(
    DecoderErrorListener *error_listener) {
  // Create a new encoding group for this instruction group and add all the
  // encodings to it.
  encoding_group_ = new ProtoEncodingGroup(this, 0, error_listener);
  for (auto *encoding : encodings_) {
    encoding_group_->AddEncoding(new ProtoInstructionEncoding(*encoding));
  }
  // Call the encoding group to break it into a proper decoding hierarchy.
  encoding_group_->AddSubGroups();
}

std::string ProtoInstructionGroup::GenerateDecoder() const {
  if (encoding_group_ == nullptr) {
    return absl::StrCat("#error No decoder generated for instruction group '",
                        name(), "'.");
  }
  std::string output;
  if (message_type_ == nullptr) {
    absl::StrAppend(&output, "\n#error No message type for instruction group '",
                    name(), "'.\n");
    return output;
  }
  std::string qualified_message_type =
      absl::StrReplaceAll(message_type_->full_name(), {{".", "::"}});
  std::string message_type = ToPascalCase(name()) + "MessageType";
  absl::StrAppend(
      &output, "\n// Decoding functions for instruction group: ", name(), "\n");
  absl::StrAppend(&output, "namespace {\n\n");
  absl::StrAppend(&output, encoding_info_->opcode_enum(), " ", "Decode",
                  ToPascalCase(name()), "_None(", message_type, ", ",
                  ToPascalCase(encoding_info_->decoder()->name()),
                  "Decoder *) {\n", "  return ", encoding_info_->opcode_enum(),
                  "::kNone;\n}\n\n");
  absl::StrAppend(&output, encoding_group_->EmitDecoders(
                               "Decode" + ToPascalCase(name()),
                               encoding_info_->opcode_enum(), message_type));
  absl::StrAppend(&output, "}  // namespace\n\n");
  return output;
}

}  // namespace proto_fmt
}  // namespace decoder
}  // namespace sim
}  // namespace mpact
