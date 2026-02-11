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

#ifndef MPACT_SIM_DECODER_PROTO_FORMAT_VISITOR_H_
#define MPACT_SIM_DECODER_PROTO_FORMAT_VISITOR_H_

#include <deque>
#include <functional>
#include <list>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "antlr4-runtime/ParserRuleContext.h"
#include "mpact/sim/decoder/ProtoFormatLexer.h"
#include "mpact/sim/decoder/ProtoFormatParser.h"
#include "mpact/sim/decoder/antlr_parser_wrapper.h"
#include "mpact/sim/decoder/decoder_error_listener.h"
#include "mpact/sim/decoder/proto_constraint_expression.h"
#include "mpact/sim/decoder/proto_encoding_info.h"
#include "mpact/sim/decoder/proto_format_contexts.h"
#include "mpact/sim/decoder/proto_instruction_group.h"
#include "re2/re2.h"
#include "src/google/protobuf/descriptor.h"

// This file defines the visitor class used to generate C++ code for decoding
// instructions encoded in protobufs.

namespace mpact {
namespace sim {
namespace decoder {
namespace proto_fmt {

class ProtoInstructionEncoding;

using ::mpact::sim::decoder::proto_fmt::generated::ProtoFormatLexer;
using ::mpact::sim::decoder::proto_fmt::generated::ProtoFormatParser;

using ProtoFmtAntlrParserWrapper =
    decoder::AntlrParserWrapper<ProtoFormatParser, ProtoFormatLexer>;

class ProtoFormatVisitor {
 public:
  ProtoFormatVisitor() = default;
  ~ProtoFormatVisitor() = default;

  // This is the main function. It parses the .proto_fmt file and generates
  // the required C++ code.
  // Parameters:
  //   file_names: vector of source file names.
  //   decoder_name: the decoder for which to generate code.
  //   prefix: if non-empty, the prefix used in the generated file names.
  //   include_roots: vector of directories from which to resolve include files.
  //   proto_dirs: vector of directories from which to resolve proto files.
  //   proto_files: vector of .proto files to import.
  //   directory: target directory to generate the C++ files in.
  absl::Status Process(const std::vector<std::string>& file_names,
                       const std::string& decoder_name, std::string_view prefix,
                       const std::vector<std::string>& include_roots,
                       const std::vector<std::string>& proto_dirs,
                       const std::vector<std::string>& proto_files,
                       std::string_view directory);

  // Accessors for the error listener.
  decoder::DecoderErrorListener* error_listener() const {
    return error_listener_.get();
  }
  void set_error_listener(
      std::unique_ptr<decoder::DecoderErrorListener> listener) {
    error_listener_ = std::move(listener);
  }

 private:
  // This struct holds information about a range assignment in an instruction
  // generator.
  struct RangeAssignmentInfo {
    std::vector<std::string> range_names;
    std::list<RE2> range_regexes;
    std::vector<std::vector<std::string>> range_values;
  };

  // Expand the given name according to any using statements that may apply.
  std::string Expand(std::string_view name) const;
  // Get the field descriptor for the named field from the message type while
  // building up a vector of one_of_fields that are on the path from the top
  // level message to the field.
  const google::protobuf::FieldDescriptor* GetField(
      const std::string& field_name,
      const google::protobuf::Descriptor* message_type,
      std::vector<const google::protobuf::FieldDescriptor*>& one_of_fields)
      const;
  // Get the descriptor for the named enumeration member.
  const google::protobuf::EnumValueDescriptor* GetEnumValueDescriptor(
      const std::string& full_name) const;
  // Get the numeric value of the named enumeration member.
  absl::StatusOr<int> GetEnumValue(const std::string& enum_name) const;
  // Helpful template function used to find a descriptor by name.
  template <typename T>
  const T* FindByName(const std::string& message_name, const std::string& name,
                      std::function<const T*(const std::string&)> finder) const;
  // Visit the parse tree to catalog the different declarations.
  void PreProcessDeclarations(const std::vector<DeclarationCtx*>& declarations);
  // Process an include file declaration.
  void VisitIncludeFile(IncludeFileCtx* ctx);
  // Parse an included file.
  void ParseIncludeFile(antlr4::ParserRuleContext* ctx,
                        const std::string& file_name,
                        const std::vector<std::string>& dirs);
  // Creates the top level data structures and visits the declarations that
  // are necessary to generate the instruction decoder.
  std::unique_ptr<ProtoEncodingInfo> ProcessTopLevel(
      const std::string& decoder_name);
  // The following methods visit specific nodes in the parse tree.
  ProtoInstructionGroup* VisitInstructionGroupDef(
      InstructionGroupDefCtx* ctx, ProtoEncodingInfo* encoding_info);
  void VisitInstructionDef(InstructionDefCtx* ctx,
                           ProtoInstructionGroup* inst_group,
                           ProtoEncodingInfo* encoding_info);
  void VisitFieldConstraint(FieldConstraintCtx* ctx,
                            ProtoInstructionEncoding* inst_encoding,
                            const ProtoInstructionGroup* inst_group);
  ProtoConstraintExpression* VisitConstraintExpression(
      ConstraintExprCtx* ctx,
      const google::protobuf::FieldDescriptor* field_desc,
      const ProtoInstructionGroup* inst_group);
  ProtoConstraintExpression* VisitValue(ValueCtx* ctx);
  ProtoConstraintExpression* VisitNumber(NumberCtx* ctx);
  ProtoConstraintExpression* VisitQualifiedIdent(
      QualifiedIdentCtx* ctx,
      const google::protobuf::FieldDescriptor* field_desc,
      const ProtoInstructionGroup* inst_group);
  void VisitSetterGroupDef(SetterGroupDefCtx* ctx,
                           ProtoInstructionGroup* inst_group,
                           ProtoEncodingInfo* encoding_info);
  void VisitSetterDef(SetterDefCtx* ctx,
                      ProtoInstructionEncoding* inst_encoding,
                      ProtoInstructionGroup* inst_group,
                      ProtoEncodingInfo* encoding_info);
  void VisitSetterRef(SetterRefCtx* ctx,
                      ProtoInstructionEncoding* inst_encoding,
                      ProtoInstructionGroup* inst_group,
                      ProtoEncodingInfo* encoding_info);
  std::unique_ptr<ProtoEncodingInfo> VisitDecoderDef(DecoderDefCtx* ctx);
  // Process and generate instruction definitions from a generate statement.
  void ProcessInstructionDefGenerator(InstructionDefCtx* ctx,
                                      ProtoInstructionGroup* inst_group,
                                      ProtoEncodingInfo* encoding_info);
  std::string GenerateInstructionDefList(
      const std::vector<RangeAssignmentInfo*>& range_info_vec, int index,
      const std::string& template_str_in) const;
  // Process instruction groups.
  void ProcessSingleGroup(DecoderAttributeCtx* attr_ctx,
                          ProtoEncodingInfo* encoding_info,
                          absl::flat_hash_set<std::string>& group_name_set);
  void ProcessParentGroup(DecoderAttributeCtx* attr_ctx,
                          ProtoEncodingInfo* encoding_info,
                          absl::flat_hash_set<std::string>& group_name_set);

  // Called to generate and emit code for the decoder according to the parsed
  // input file.
  StringPair EmitCode(ProtoEncodingInfo* encoding_info);

  // Finders used to find specific object types from the proto2 pool.
  using FieldFinder = std::function<const google::protobuf::FieldDescriptor*(
      const std::string&)>;
  using MessageFinder =
      std::function<const google::protobuf::Descriptor*(const std::string&)>;
  using EnumTypeFinder = std::function<const google::protobuf::EnumDescriptor*(
      const std::string&)>;
  using EnumValueFinder =
      std::function<const google::protobuf::EnumValueDescriptor*(
          const std::string&)>;

  FieldFinder field_finder_;
  MessageFinder message_finder_;
  EnumTypeFinder enum_type_finder_;
  EnumValueFinder enum_value_finder_;

  // This stores a vector of include file root directories.
  std::vector<std::string> include_dir_vec_;
  // Keep track of files that are included in case there is recursive includes.
  std::deque<std::string> include_file_stack_;
  // Error listening object passed to the parser.
  std::unique_ptr<decoder::DecoderErrorListener> error_listener_ = nullptr;
  std::string decoder_name_;
  // Descriptor pool.
  const google::protobuf::DescriptorPool* descriptor_pool_;
  absl::flat_hash_map<std::string, const google::protobuf::FileDescriptor*>
      file_descriptor_map_;
  // Using decl map.
  absl::flat_hash_map<std::string, std::string> using_decl_map_;
  // Maps from identifiers to declaration contexts.
  absl::flat_hash_map<std::string, InstructionGroupDefCtx*> group_decl_map_;
  absl::flat_hash_map<std::string, DecoderDefCtx*> decoder_decl_map_;
  // AntlrParserWrapper vector.
  std::vector<ProtoFmtAntlrParserWrapper*> antlr_parser_wrappers_;
};

}  // namespace proto_fmt
}  // namespace decoder
}  // namespace sim
}  // namespace mpact

#endif  // MPACT_SIM_DECODER_PROTO_FORMAT_VISITOR_H_
