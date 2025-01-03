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

#include "mpact/sim/decoder/proto_encoding_info.h"

#include <string>
#include <utility>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_replace.h"
#include "absl/strings/string_view.h"
#include "mpact/sim/decoder/decoder_error_listener.h"
#include "mpact/sim/decoder/format_name.h"
#include "mpact/sim/decoder/proto_constraint_expression.h"
#include "mpact/sim/decoder/proto_instruction_decoder.h"
#include "mpact/sim/decoder/proto_instruction_group.h"
#include "src/google/protobuf/descriptor.h"

namespace mpact {
namespace sim {
namespace decoder {
namespace proto_fmt {

static constexpr char kProtoFileExtension[] = ".pb.h";

using ::mpact::sim::machine_description::instruction_set::ToPascalCase;
using ::mpact::sim::machine_description::instruction_set::ToSnakeCase;

ProtoEncodingInfo::ProtoEncodingInfo(
    const std::string &opcode_enum,
    decoder::DecoderErrorListener *error_listener)
    : opcode_enum_(opcode_enum), error_listener_(error_listener) {}

ProtoEncodingInfo::~ProtoEncodingInfo() { delete decoder_; }

void ProtoEncodingInfo::AddIncludeFile(std::string include_file) {
  include_files_.emplace(include_file);
}

ProtoInstructionDecoder *ProtoEncodingInfo::SetProtoDecoder(std::string name) {
  if (decoder_ != nullptr) {
    error_listener_->semanticError(nullptr, "Can only select one decoder");
    return nullptr;
  }
  auto *proto_decoder =
      new ProtoInstructionDecoder(name, this, error_listener());
  decoder_ = proto_decoder;
  return proto_decoder;
}

absl::StatusOr<ProtoInstructionGroup *> ProtoEncodingInfo::AddInstructionGroup(
    const std::string &group_name,
    const google::protobuf::Descriptor *descriptor) {
  // Make sure that the instruction group hasn't been added before.
  if (instruction_group_map_.contains(group_name)) {
    return absl::AlreadyExistsError(absl::StrCat(
        "Error: instruction group '", group_name, "' already defined"));
  }
  auto group =
      new ProtoInstructionGroup(group_name, descriptor, opcode_enum_, this);
  instruction_group_map_.emplace(group_name, group);
  return group;
}

absl::Status ProtoEncodingInfo::CheckSetterType(
    absl::string_view name,
    const google::protobuf::FieldDescriptor *field_desc) {
  // Is it a new setter, if so, just insert it.
  auto iter = setter_types_.find(name);
  auto cpp_type = field_desc->cpp_type();
  if (cpp_type == google::protobuf::FieldDescriptor::CPPTYPE_MESSAGE) {
    return absl::InvalidArgumentError(
        absl::StrCat("Setter type for '", name, "' cannot be a message."));
  }
  // Treat enums at int32.
  if (cpp_type == google::protobuf::FieldDescriptor::CPPTYPE_ENUM) {
    cpp_type = google::protobuf::FieldDescriptor::CPPTYPE_INT32;
  }
  if (iter == setter_types_.end()) {
    setter_types_.insert({std::string(name), cpp_type});
    return absl::OkStatus();
  }
  // If the type is the same, return ok.
  if (cpp_type == iter->second) return absl::OkStatus();

  // The types are not the same. See if we can use a compatible type.
  switch (iter->second) {
    case google::protobuf::FieldDescriptor::CPPTYPE_INT32:
      if (cpp_type == google::protobuf::FieldDescriptor::CPPTYPE_BOOL) {
        return absl::OkStatus();
      }
      if ((cpp_type == google::protobuf::FieldDescriptor::CPPTYPE_INT64) ||
          (cpp_type == google::protobuf::FieldDescriptor::CPPTYPE_UINT32)) {
        iter->second = google::protobuf::FieldDescriptor::CPPTYPE_INT64;
        return absl::OkStatus();
      }
      return absl::InvalidArgumentError(
          absl::StrCat("Type inconsistency in setter '", name, "'"));
    case google::protobuf::FieldDescriptor::CPPTYPE_INT64:
      if (cpp_type == google::protobuf::FieldDescriptor::CPPTYPE_BOOL) {
        return absl::OkStatus();
      }
      if ((cpp_type == google::protobuf::FieldDescriptor::CPPTYPE_UINT32) ||
          (cpp_type == google::protobuf::FieldDescriptor::CPPTYPE_INT32)) {
        return absl::OkStatus();
      }
      return absl::InvalidArgumentError(
          absl::StrCat("Type inconsistency in setter '", name, "'"));
    case google::protobuf::FieldDescriptor::CPPTYPE_UINT32:
      if (cpp_type == google::protobuf::FieldDescriptor::CPPTYPE_BOOL) {
        return absl::OkStatus();
      }
      if ((cpp_type == google::protobuf::FieldDescriptor::CPPTYPE_INT32) ||
          (iter->second = google::protobuf::FieldDescriptor::CPPTYPE_INT64)) {
        iter->second = google::protobuf::FieldDescriptor::CPPTYPE_INT64;
        return absl::OkStatus();
      }
      if (cpp_type == google::protobuf::FieldDescriptor::CPPTYPE_UINT64) {
        iter->second = google::protobuf::FieldDescriptor::CPPTYPE_UINT64;
        return absl::OkStatus();
      }
      return absl::InvalidArgumentError(
          absl::StrCat("Type inconsistency in setter '", name, "'"));
    case google::protobuf::FieldDescriptor::CPPTYPE_UINT64:
      if (cpp_type == google::protobuf::FieldDescriptor::CPPTYPE_BOOL) {
        return absl::OkStatus();
      }
      if ((cpp_type == google::protobuf::FieldDescriptor::CPPTYPE_INT32) ||
          (cpp_type == google::protobuf::FieldDescriptor::CPPTYPE_UINT32)) {
        return absl::OkStatus();
      }
      return absl::InvalidArgumentError(
          absl::StrCat("Type inconsistency in setter '", name, "'"));
    case google::protobuf::FieldDescriptor::CPPTYPE_DOUBLE:
      if (cpp_type == google::protobuf::FieldDescriptor::CPPTYPE_FLOAT) {
        return absl::OkStatus();
      }
      return absl::InvalidArgumentError(
          absl::StrCat("Type inconsistency in setter '", name, "'"));
    case google::protobuf::FieldDescriptor::CPPTYPE_FLOAT:
      if (cpp_type == google::protobuf::FieldDescriptor::CPPTYPE_DOUBLE) {
        iter->second = google::protobuf::FieldDescriptor::CPPTYPE_DOUBLE;
        return absl::OkStatus();
      }
      return absl::InvalidArgumentError(
          absl::StrCat("Type inconsistency in setter '", name, "'"));
    case google::protobuf::FieldDescriptor::CPPTYPE_BOOL:
      if ((cpp_type == google::protobuf::FieldDescriptor::CPPTYPE_INT32) ||
          (cpp_type == google::protobuf::FieldDescriptor::CPPTYPE_UINT32) ||
          (cpp_type == google::protobuf::FieldDescriptor::CPPTYPE_INT64) ||
          (cpp_type == google::protobuf::FieldDescriptor::CPPTYPE_UINT64)) {
        iter->second = cpp_type;
        return absl::OkStatus();
      }
      return absl::InvalidArgumentError(
          absl::StrCat("Type inconsistency in setter '", name, "'"));
    case google::protobuf::FieldDescriptor::CPPTYPE_STRING:
      return absl::InvalidArgumentError(
          absl::StrCat("Type inconsistency in setter '", name, "'"));
    default:
      return absl::InvalidArgumentError(
          absl::StrCat("Type inconsistency in setter '", name, "'"));
  }
  return absl::OkStatus();
}

constexpr char kDecodeMsgName[] = "inst_proto";

StringPair ProtoEncodingInfo::GenerateDecoderClass() {
  std::string h_output;
  std::string var_output;
  std::string cc_output;
  std::string class_name = ToPascalCase(decoder_->name()) + "Decoder";
  std::string decoder_def;
  std::string type_aliases;
  // Add type aliases for protobuf messages used in decoders.
  for (auto *inst_group : decoder_->instruction_groups()) {
    std::string qualified_message_type = absl::StrReplaceAll(
        inst_group->message_type()->full_name(), {{".", "::"}});
    absl::StrAppend(&type_aliases, "using ", ToPascalCase(inst_group->name()),
                    "MessageType = ", qualified_message_type, ";\n");
    absl::string_view file_name = inst_group->message_type()->file()->name();
    // Verify that this is a .proto file.

    if ((file_name.size() <= 5) &&
        (file_name.substr(file_name.size() - 5) != ".proto")) {
      error_listener_->semanticError(
          nullptr, absl::StrCat("Not a .proto file: '", file_name, "'"));
      return {"", ""};
    }
    auto proto_file_include = absl::StrCat(
        file_name.substr(0, file_name.size() - 6), kProtoFileExtension);
    AddIncludeFile(absl::StrCat("\"", proto_file_include, "\""));
  }
  // Include files.
  absl::StrAppend(&h_output, "#include <cstdint>\n\n");
  for (auto &include_file : include_files_) {
    absl::StrAppend(&h_output, "#include ", include_file, "\n");
  }
  absl::StrAppend(&h_output, "\n");
  absl::StrAppend(&cc_output,
                  "#include <functional>\n\n"
                  "#include \"absl/container/flat_hash_map.h\"\n\n");
  // Open namespaces.
  std::string name_space_ref = absl::StrJoin(decoder_->namespaces(), "::");
  for (auto &name_space : decoder_->namespaces()) {
    absl::StrAppend(&h_output, "namespace ", name_space, " {\n");
    absl::StrAppend(&cc_output, "namespace ", name_space, " {\n");
  }
  // Generate class definition.
  absl::StrAppend(&h_output, "\n", type_aliases, "\nclass ", class_name, " {\n",
                  " public:\n", "  ", class_name, "() = default;\n\n");
  absl::StrAppend(&h_output, "  // Decode method(s).\n");
  std::string decoder_fcns;
  for (auto *inst_group : decoder_->instruction_groups()) {
    inst_group->ProcessEncodings(error_listener_);
    absl::StrAppend(&h_output, "  ", opcode_enum_, " Decode",
                    ToPascalCase(inst_group->name()), "(",
                    ToPascalCase(inst_group->name()), "MessageType ",
                    kDecodeMsgName, ");\n");
    absl::StrAppend(&cc_output, inst_group->GenerateDecoder());
    absl::StrAppend(&decoder_fcns, opcode_enum_, " ", class_name, "::Decode",
                    ToPascalCase(inst_group->name()), "(",
                    ToPascalCase(inst_group->name()), "MessageType ",
                    kDecodeMsgName, ") {\n", "  return ", name_space_ref,
                    "::Decode", ToPascalCase(inst_group->name()), "(",
                    kDecodeMsgName, ", this);\n", "}\n\n");
  }
  if (error_listener_->HasError()) return {"", ""};
  absl::StrAppend(&cc_output, decoder_fcns);
  absl::StrAppend(&h_output, "\n  // Setters and getters.\n");
  for (auto const &[name, cpp_type] : setter_types_) {
    auto cpp_type_name = GetCppTypeName(cpp_type);
    // Generate method declarations.
    absl::StrAppend(&h_output, "  void Set", ToPascalCase(name), "(",
                    cpp_type_name, " value);\n", "  ", cpp_type_name, " Get",
                    ToPascalCase(name), "();\n");
    // Generate Method definitions.
    absl::StrAppend(&cc_output, "void ", class_name, "::Set",
                    ToPascalCase(name), "(", cpp_type_name, " value) { ",
                    ToSnakeCase(name), "_value_ = value;}\n", cpp_type_name,
                    " ", class_name, "::Get", ToPascalCase(name),
                    "() { return ", ToSnakeCase(name), "_value_;}\n\n");
    // Generate variable declarations.
    absl::StrAppend(&var_output, "  ", cpp_type_name, " ", ToSnakeCase(name),
                    "_value_;\n");
  }
  absl::StrAppend(&h_output, "\n private:\n", var_output, "};\n\n");
  // Close namespaces.
  for (auto iter = decoder_->namespaces().rbegin();
       iter != decoder_->namespaces().rend(); ++iter) {
    absl::StrAppend(&h_output, "}  // namespace ", *iter, "\n");
    absl::StrAppend(&cc_output, "}  // namespace ", *iter, "\n");
  }
  return {h_output, cc_output};
}

}  // namespace proto_fmt
}  // namespace decoder
}  // namespace sim
}  // namespace mpact
