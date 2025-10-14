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

#include "mpact/sim/decoder/proto_format_visitor.h"

#include <unistd.h>

#include <cctype>
#include <cstdint>
#include <fstream>
#include <functional>
#include <iostream>
#include <istream>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "absl/container/flat_hash_set.h"
#include "absl/functional/bind_front.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/match.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "antlr4-runtime/ParserRuleContext.h"
#include "mpact/sim/decoder/decoder_error_listener.h"
#include "mpact/sim/decoder/format_name.h"
#include "mpact/sim/decoder/proto_constraint_expression.h"
#include "mpact/sim/decoder/proto_encoding_info.h"
#include "mpact/sim/decoder/proto_format_contexts.h"
#include "mpact/sim/decoder/proto_instruction_decoder.h"
#include "mpact/sim/decoder/proto_instruction_encoding.h"
#include "mpact/sim/decoder/proto_instruction_group.h"
#include "src/google/protobuf/compiler/importer.h"
#include "src/google/protobuf/descriptor.h"
#include "util/regexp/re2/re2.h"

namespace mpact {
namespace sim {
namespace decoder {
namespace proto_fmt {

using ::mpact::sim::machine_description::instruction_set::ToHeaderGuard;

namespace {

std::string StripQuotes(const std::string& str) {
  return str.substr(1, str.length() - 2);
}

}  // namespace

// Error collector for .proto file parsing.
class MultiFileErrorCollector
    : public google::protobuf::compiler::MultiFileErrorCollector {
 public:
  MultiFileErrorCollector() {}
  MultiFileErrorCollector(const MultiFileErrorCollector&) = delete;
  MultiFileErrorCollector& operator=(const MultiFileErrorCollector&) = delete;

  void RecordError(absl::string_view filename, int line, int column,
                   absl::string_view message) override {
    LOG(ERROR) << absl::StrCat("Line ", line, " Column ", column, ": ", message,
                               "\n");
    absl::StrAppend(&error_, "Line ", line, " Column ", column, ": ", message,
                    "\n");
  }
  const std::string& GetError() const { return error_; }

 private:
  std::string error_;
};

// Main public method. This is called to parse a descriptor file and generate
// C++ code to decode protobuf encoded instructions.
// Parameters:
//   file_names: vector of source file names.
//   decoder_name: the decoder for which to generate code.
//   prefix: if non-empty, the prefix used in the generated file names.
//   include_roots: vector of directories from which to resolve include files.
//   proto_dirs: vector of directories from which to resolve proto files.
//   proto_files: vector of .proto files to import.
//   directory: target directory to generate the C++ files in.
absl::Status ProtoFormatVisitor::Process(
    const std::vector<std::string>& file_names, const std::string& decoder_name,
    std::string_view prefix, const std::vector<std::string>& include_roots,
    const std::vector<std::string>& proto_dirs,
    const std::vector<std::string>& proto_files, std::string_view directory) {
  decoder_name_ = decoder_name;

  include_dir_vec_.push_back(".");
  for (const auto& root : include_roots) {
    include_dir_vec_.push_back(root);
  }

  std::istream* source_stream = &std::cin;

  if (!file_names.empty()) {
    source_stream = new std::fstream(file_names[0], std::fstream::in);
  }

  // Create an antlr4 stream from the input stream.
  ProtoFmtAntlrParserWrapper parser_wrapper(source_stream);

  // Create and add the error listener.
  set_error_listener(std::make_unique<decoder::DecoderErrorListener>());
  error_listener_->set_file_name(file_names[0]);
  parser_wrapper.parser()->removeErrorListeners();
  parser_wrapper.parser()->addErrorListener(error_listener());

  // Initialize the proto source tree with the proto directories to resolve
  // proto files from.
  google::protobuf::compiler::DiskSourceTree source_tree;
  source_tree.MapPath("", "./");
  for (auto const& proto_dir : proto_dirs) {
    source_tree.MapPath("", proto_dir);
  }
  // Import the proto files.
  MultiFileErrorCollector proto_error_collector;
  google::protobuf::compiler::Importer importer(&source_tree,
                                                &proto_error_collector);
  for (auto const& proto_file : proto_files) {
    auto* file_desc = importer.Import(proto_file);
    if (file_desc == nullptr) {
      error_listener()->semanticError(
          nullptr, absl::StrCat("Failed to import '", proto_file, "'"));
      continue;
    }
    file_descriptor_map_.emplace(proto_file, file_desc);
  }
  // If there have been any errors, terminate.
  if (!proto_error_collector.GetError().empty()) {
    return absl::InternalError(proto_error_collector.GetError());
  }
  if (error_listener()->HasError()) {
    return absl::InternalError("Errors encountered - terminating.");
  }

  descriptor_pool_ = importer.pool();
  // Set the finder function objects.
  field_finder_ = absl::bind_front(
      &google::protobuf::DescriptorPool::FindFieldByName, descriptor_pool_);
  message_finder_ =
      absl::bind_front(&google::protobuf::DescriptorPool::FindMessageTypeByName,
                       descriptor_pool_);
  enum_type_finder_ = absl::bind_front(
      &google::protobuf::DescriptorPool::FindEnumTypeByName, descriptor_pool_);
  enum_value_finder_ = absl::bind_front(
      &google::protobuf::DescriptorPool::FindEnumValueByName, descriptor_pool_);

  // Parse the file and then create the data structures.
  TopLevelCtx* top_level = parser_wrapper.parser()->top_level();
  (void)top_level;
  if (!file_names.empty()) {
    delete source_stream;
    source_stream = nullptr;
  }

  if (error_listener_->HasError() > 0) {
    return absl::InternalError("Errors encountered - terminating.");
  }

  // Visit the parse tree.
  PreProcessDeclarations(top_level->declaration());
  // Process additional source files.
  for (int i = 1; i < file_names.size(); ++i) {
    ParseIncludeFile(top_level, file_names[i], {});
  }

  // Process the parse tree.
  auto encoding_info = ProcessTopLevel(decoder_name);
  // Include files may generate additional syntax errors.
  if (encoding_info == nullptr) {
    LOG(ERROR) << "No encoding specified";
    return absl::InternalError("No encoding specified");
  }
  if (error_listener_->HasError() > 0) {
    return absl::InternalError("Errors encountered - terminating.");
  }

  // Generate the decoder.
  auto [h_decoder, cc_decoder] = encoding_info->GenerateDecoderClass();

  // Terminate if there were errors.
  if (error_listener_->HasError() > 0) {
    return absl::InternalError("Errors encountered - terminating.");
  }
  // Create file names for the output files.
  std::string file_prefix(prefix);
  if (file_prefix.empty()) {
    // If there is no prefix specified, use the decoder name in snake case.
    file_prefix = mpact::sim::machine_description::instruction_set::ToSnakeCase(
        decoder_name);
  }
  std::string dot_h_name = absl::StrCat(file_prefix, "_proto_decoder.h");
  std::string dot_cc_name = absl::StrCat(file_prefix, "_proto_decoder.cc");
  // Create output streams for .h and .cc files.
  std::ofstream dot_h_file(absl::StrCat(directory, "/", dot_h_name));
  std::ofstream dot_cc_file(absl::StrCat(directory, "/", dot_cc_name));
  // Output the decoder with header guards inserted in the .h file.
  std::string header_guard_name = ToHeaderGuard(dot_h_name);
  dot_h_file << "#ifndef " << header_guard_name << "\n";
  dot_h_file << "#define " << header_guard_name << "\n\n";
  dot_h_file << h_decoder;
  dot_h_file << "\n#endif  // " << header_guard_name << "\n";
  dot_cc_file << absl::StrCat("#include \"", dot_h_name, "\"\n\n");
  dot_cc_file << cc_decoder;
  // Close files.
  dot_h_file.close();
  dot_cc_file.close();
  return absl::OkStatus();
}

// Check the using declarations map, and expand the name if it matches.
std::string ProtoFormatVisitor::Expand(std::string_view name) const {
  // The name might be a qualified name, in which case we only expand the
  // first part.
  std::string prefix(name);
  auto pos = name.find_first_of('.');
  std::string remainder;
  // If the name is qualified, try expanding the first part only.
  if (pos != std::string::npos) {
    remainder = name.substr(pos);
    prefix = prefix.substr(0, pos);
  }
  auto iter = using_decl_map_.find(prefix);
  if (iter == using_decl_map_.end()) return std::string(name);
  return iter->second + remainder;
}

// Helper templated function used to find proto objects by name.
template <typename T>
const T* ProtoFormatVisitor::FindByName(
    const std::string& name, const std::string& message_name,
    std::function<const T*(const std::string&)> finder) const {
  auto* object = finder(Expand(message_name + "." + name));
  if (object != nullptr) return object;
  object = finder(Expand(name));
  return object;
}

const google::protobuf::FieldDescriptor* ProtoFormatVisitor::GetField(
    const std::string& field_name,
    const google::protobuf::Descriptor* message_type,
    std::vector<const google::protobuf::FieldDescriptor*>& one_of_fields)
    const {
  auto pos = field_name.find_first_of('.');
  // If this is a "leaf" field, find it and return if found.
  if (pos == std::string::npos) {
    auto* field_desc = message_type->FindFieldByName(field_name);
    if (field_desc->containing_oneof() != nullptr) {
      one_of_fields.push_back(field_desc);
    }
    return field_desc;
  }
  // Recursively traverse the components of the field name.
  std::string field = field_name.substr(0, pos);
  std::string remainder = field_name.substr(pos + 1);
  auto const* field_desc = message_type->FindFieldByName(field);
  if (field_desc == nullptr) return nullptr;
  if (field_desc->containing_oneof() != nullptr) {
    one_of_fields.push_back(field_desc);
  }
  auto const* message_desc = field_desc->message_type();
  if (message_desc == nullptr) return nullptr;
  return GetField(remainder, message_desc, one_of_fields);
}

const google::protobuf::EnumValueDescriptor*
ProtoFormatVisitor::GetEnumValueDescriptor(const std::string& full_name) const {
  std::string expanded = Expand(full_name);
  auto pos = expanded.find_last_of('.');
  // If this is a "leaf", it fails. The enum must be qualified by enum type.
  if (pos == std::string::npos) {
    return nullptr;
  }
  std::string enum_name = expanded.substr(pos + 1);
  std::string enum_type_name = expanded.substr(0, pos);
  // Find the enum type.
  auto const* enum_type_desc =
      descriptor_pool_->FindEnumTypeByName(enum_type_name);
  if (enum_type_desc == nullptr) return nullptr;
  // Find the enum value in the enum type.
  auto const* enum_value_desc = enum_type_desc->FindValueByName(enum_name);
  return enum_value_desc;
}

absl::StatusOr<int> ProtoFormatVisitor::GetEnumValue(
    const std::string& enum_name) const {
  auto* enum_value_desc = GetEnumValueDescriptor(enum_name);
  if (enum_value_desc == nullptr) {
    return absl::NotFoundError(
        absl::StrCat("Enum '", enum_name, "' not found"));
  }
  return enum_value_desc->number();
}

void ProtoFormatVisitor::PreProcessDeclarations(
    const std::vector<DeclarationCtx*>& declarations) {
  std::vector<IncludeFileCtx*> include_files;
  for (auto* declaration : declarations) {
    // Create map from instruction group name to instruction group ctx. That
    // way we can visit those that are referenced by the decoder definition
    // later.
    if (declaration->instruction_group_def() != nullptr) {
      auto group_def = declaration->instruction_group_def();
      auto name = group_def->name->getText();
      auto iter = group_decl_map_.find(name);
      if (iter != group_decl_map_.end()) {
        error_listener()->semanticError(
            group_def->start,
            absl::StrCat(
                "Multiple definitions of instruction group '", name,
                "' first defined at line: ", iter->second->start->getLine()));
        continue;
      }
      group_decl_map_.emplace(name, group_def);
      continue;
    }
    // Visit all the 'using' declarations so they can be used to resolve
    // proto message and field references later.
    if (declaration->using_alias() != nullptr) {
      std::string alias;
      std::string name =
          declaration->using_alias()->qualified_ident()->getText();
      if (declaration->using_alias()->IDENT() != nullptr) {
        alias = declaration->using_alias()->IDENT()->getText();
      } else {
        auto pos = name.find_last_of('.');
        if (pos != std::string::npos) {
          alias = name.substr(pos + 1);
        } else {
          alias = name;
        }
      }
      if (using_decl_map_.find(name) != using_decl_map_.end()) {
        error_listener()->semanticError(
            declaration->start, absl::StrCat("Redefinition of '", name, "'"));
      }
      using_decl_map_.emplace(alias, name);
      continue;
    }
    // Create a map from decoder definitions to their parse contexts.
    if (declaration->decoder_def() != nullptr) {
      auto* decoder = declaration->decoder_def();
      auto name = decoder->name->getText();
      auto iter = decoder_decl_map_.find(name);
      if (iter != decoder_decl_map_.end()) {
        error_listener()->semanticError(
            decoder->start, absl::StrCat("Multiple definitions of decoder '",
                                         name, "' first defined at line: ",
                                         iter->second->start->getLine()));
        continue;
      }
      decoder_decl_map_.emplace(name, decoder);
      continue;
    }
    // Capture include files.
    include_files.push_back(declaration->include_file());
  }
  // Visit all the include files captured above.
  for (auto* include_file_ctx : include_files) {
    VisitIncludeFile(include_file_ctx);
  }
}

void ProtoFormatVisitor::VisitIncludeFile(IncludeFileCtx* ctx) {
  // The literal includes the double quotes.
  std::string literal = ctx->STRING_LITERAL()->getText();
  // Remove the double quotes from the literal and construct the full file name.
  std::string file_name = literal.substr(1, literal.length() - 2);
  // Check for recursive include by comparing the current name to those on the
  // current include file stack.
  for (auto const& name : include_file_stack_) {
    if (name == file_name) {
      error_listener()->semanticError(
          ctx->start,
          absl::StrCat(": ", "Recursive include of '", file_name, "'"));
      return;
    }
  }
  ParseIncludeFile(ctx, file_name, include_dir_vec_);
}

void ProtoFormatVisitor::ParseIncludeFile(
    antlr4::ParserRuleContext* ctx, const std::string& file_name,
    const std::vector<std::string>& dirs) {
  std::fstream include_file;
  // Open include file.
  include_file.open(file_name, std::fstream::in);
  if (!include_file.is_open()) {
    // Try each of the include file directories.
    for (auto const& dir : dirs) {
      std::string include_name = absl::StrCat(dir, "/", file_name);
      include_file.open(include_name, std::fstream::in);
      if (include_file.is_open()) break;
    }
    if (!include_file.is_open()) {
      // Try a local file.
      include_file.open(file_name, std::fstream::in);
      if (!include_file.is_open()) {
        error_listener()->semanticError(
            ctx->start, absl::StrCat("Failed to open '", file_name, "'"));
        return;
      }
    }
  }
  std::string previous_file_name = error_listener()->file_name();
  error_listener()->set_file_name(std::string(file_name));
  auto* include_parser = new ProtoFmtAntlrParserWrapper(&include_file);
  // We need to save the parser state so it's available for analysis after
  // we are done with building the parse trees.
  antlr_parser_wrappers_.push_back(include_parser);
  // Add the error listener.
  include_parser->parser()->removeErrorListeners();
  include_parser->parser()->addErrorListener(error_listener());
  // Start parsing at the top_level rule.
  auto declaration_vec = include_parser->parser()->top_level()->declaration();
  include_file.close();
  error_listener()->set_file_name(previous_file_name);
  if (error_listener()->syntax_error_count() > 0) {
    return;
  }
  include_file_stack_.emplace_back(file_name);
  // Process the declarations.
  PreProcessDeclarations(include_parser->parser()->top_level()->declaration());
  include_file_stack_.pop_back();
}

std::unique_ptr<ProtoEncodingInfo> ProtoFormatVisitor::ProcessTopLevel(
    const std::string& decoder_name) {
  // Look up the decoder declaration that matches the decoder name for which
  // to generate C++ code.
  auto decoder_iter = decoder_decl_map_.find(decoder_name);
  if (decoder_iter == decoder_decl_map_.end()) {
    error_listener()->semanticError(
        nullptr, absl::StrCat("Decoder '", decoder_name, "' not declared"));
    return nullptr;
  }
  auto proto_encoding_info = VisitDecoderDef(decoder_iter->second);
  return proto_encoding_info;
}

// Process instruction groups.
ProtoInstructionGroup* ProtoFormatVisitor::VisitInstructionGroupDef(
    InstructionGroupDefCtx* ctx, ProtoEncodingInfo* encoding_info) {
  if (ctx == nullptr) return nullptr;

  std::string group_name = ctx->name->getText();
  // Verify that the message type exists.
  std::string message_name = Expand(ctx->message_name->getText());
  auto const* message_desc =
      descriptor_pool_->FindMessageTypeByName(message_name);
  // If the message type doesn't exist, its an error
  if (message_desc == nullptr) {
    error_listener_->semanticError(
        ctx->start,
        absl::StrCat("Undefined proto message type: '", message_name, "'"));
    return nullptr;
  }
  // Create the named instruction group.
  auto inst_group_res =
      encoding_info->AddInstructionGroup(group_name, message_desc);
  if (!inst_group_res.ok()) {
    error_listener_->semanticError(ctx->start,
                                   inst_group_res.status().message());
    return nullptr;
  }

  auto* inst_group = inst_group_res.value();
  // First visit all the setter declarations.
  for (auto* group_def : ctx->setter_group_def()) {
    VisitSetterGroupDef(group_def, inst_group, encoding_info);
  }
  // Parse the instruction encoding definitions in the instruction group.
  for (auto* inst_def : ctx->instruction_def()) {
    VisitInstructionDef(inst_def, inst_group, encoding_info);
  }
  return inst_group;
}

// Process instruction definitions.
void ProtoFormatVisitor::VisitInstructionDef(InstructionDefCtx* ctx,
                                             ProtoInstructionGroup* inst_group,
                                             ProtoEncodingInfo* encoding_info) {
  if (ctx == nullptr) return;

  // If it is a generator, call the generator parsing function.
  if (ctx->generate != nullptr) {
    ProcessInstructionDefGenerator(ctx, inst_group, encoding_info);
    return;
  }
  // Get the instruction name.
  std::string name = ctx->name->getText();
  auto* inst_encoding = inst_group->AddInstructionEncoding(name);
  // Add Constraints to the instruction encoding.
  for (auto* constraint : ctx->field_constraint_list()->field_constraint()) {
    VisitFieldConstraint(constraint, inst_encoding, inst_group);
  }
  // Visit references to setters defined in the instruction group.
  for (auto* setter : ctx->setter_ref()) {
    VisitSetterRef(setter, inst_encoding, inst_group, encoding_info);
  }
  // Visit locally (to the instruction) defined setters.
  for (auto* setter : ctx->setter_def()) {
    VisitSetterDef(setter, inst_encoding, inst_group, encoding_info);
  }
  // Generate the setter code template.
  inst_encoding->GenerateSetterCode();
}

void ProtoFormatVisitor::VisitFieldConstraint(
    FieldConstraintCtx* ctx, ProtoInstructionEncoding* inst_encoding,
    const ProtoInstructionGroup* inst_group) {
  if (ctx == nullptr) return;

  // Constraints are based on field names ==/!=/>/>=/</<= to a value, or
  // HAS(field_name) to indicate mandated presence of submessage field_name. The
  // value may be an enumeration, so will have to check for that too.
  // TODO(torerik): In the future, enable expressions on the right hand side.

  absl::Status status;
  if (ctx->HAS() != nullptr) {
    std::string field_name = ctx->qualified_ident()->getText();
    std::vector<const google::protobuf::FieldDescriptor*> one_of_fields;
    auto* field_desc =
        GetField(field_name, inst_group->message_type(), one_of_fields);
    if (field_desc == nullptr) {
      error_listener()->semanticError(
          ctx->start,
          absl::StrCat("Field '", field_name, "' not found in message '",
                       inst_group->message_type()->name(), "'"));
      return;
    }
    // If the last field in one_of_fields is the field with the kHas constraint
    // remove that field from the vector, as it will be handled in the
    // constraint.
    if (field_desc == one_of_fields.back()) {
      one_of_fields.pop_back();
    }
    status = inst_encoding->AddConstraint(ctx, ConstraintType::kHas, field_desc,
                                          one_of_fields, nullptr);
  } else {
    // The field name is relative to the message type, but may refer to fields
    // in sub-messages contained within that message.
    std::string field_name = ctx->field->getText();
    std::vector<const google::protobuf::FieldDescriptor*> one_of_fields;
    auto* field_desc =
        GetField(field_name, inst_group->message_type(), one_of_fields);
    if (field_desc == nullptr) {
      error_listener()->semanticError(
          ctx->start,
          absl::StrCat("Field '", field_name, "' not found in message '",
                       inst_group->message_type()->name(), "'"));
      return;
    }
    std::string op = ctx->constraint_op()->getText();

    auto* constraint_expr = VisitConstraintExpression(ctx->constraint_expr(),
                                                      field_desc, inst_group);

    // Add the constraint according to the type.
    if (op == "==") {
      status = inst_encoding->AddConstraint(
          ctx, ConstraintType::kEq, field_desc, one_of_fields, constraint_expr);
    } else if (op == "!=") {
      status = inst_encoding->AddConstraint(
          ctx, ConstraintType::kNe, field_desc, one_of_fields, constraint_expr);
    } else if (op == ">") {
      status = inst_encoding->AddConstraint(
          ctx, ConstraintType::kGt, field_desc, one_of_fields, constraint_expr);
    } else if (op == ">=") {
      status = inst_encoding->AddConstraint(
          ctx, ConstraintType::kGe, field_desc, one_of_fields, constraint_expr);
    } else if (op == "<") {
      status = inst_encoding->AddConstraint(
          ctx, ConstraintType::kLt, field_desc, one_of_fields, constraint_expr);
    } else if (op == "<=") {
      status = inst_encoding->AddConstraint(
          ctx, ConstraintType::kLe, field_desc, one_of_fields, constraint_expr);
    }
  }
  if (!status.ok()) {
    error_listener()->semanticError(ctx->start, status.message());
  }
}

ProtoConstraintExpression* ProtoFormatVisitor::VisitConstraintExpression(
    ConstraintExprCtx* ctx, const google::protobuf::FieldDescriptor* field_desc,
    const ProtoInstructionGroup* inst_group) {
  if (ctx == nullptr) return nullptr;
  ProtoConstraintExpression* constraint_expr = nullptr;
  if (ctx->value() != nullptr) {
    constraint_expr = VisitValue(ctx->value());
  } else if (ctx->qualified_ident() != nullptr) {
    constraint_expr =
        VisitQualifiedIdent(ctx->qualified_ident(), field_desc, inst_group);
  }
  return constraint_expr;
}

ProtoConstraintExpression* ProtoFormatVisitor::VisitValue(ValueCtx* ctx) {
  if (ctx == nullptr) return nullptr;
  if (ctx->number() != nullptr) {
    return VisitNumber(ctx->number());
  }
  if (ctx->bool_value()) {
    bool value;
    if (!absl::SimpleAtob(ctx->bool_value()->getText(), &value)) {
      error_listener()->semanticError(ctx->start, "Invalid boolean literal");
      return nullptr;
    }
    return new ProtoConstraintValueExpression(value);
  }
  return new ProtoConstraintValueExpression(
      StripQuotes(ctx->string->getText()));
  return nullptr;
}

ProtoConstraintExpression* ProtoFormatVisitor::VisitNumber(NumberCtx* ctx) {
  std::string num_str = ctx->getText();
  // Convert to lower case to avoid capital chars in suffix.
  for (int i = 0; i < num_str.size(); ++i) num_str[i] = tolower(num_str[i]);
  bool is_signed = !absl::StrContains(num_str, "u");
  bool is_long_long = absl::StrContains(num_str, "ll");
  bool is_long = !is_long_long && absl::StrContains(num_str, "l");
  bool is_hex = num_str.substr(0, 2) == "0x";
  // If neither 'l' nor 'll' is specified, try using the 32 bit wide integer if
  // the value fits, otherwise use 64 bit.
  if (is_signed) {  // Either int32 or int64
    int32_t int32_val;
    int64_t int64_val;
    if (is_hex) {
      if (!is_long && !is_long_long &&
          absl::SimpleHexAtoi(num_str, &int32_val)) {
        return new ProtoConstraintValueExpression(int32_val);
      } else if (absl::SimpleHexAtoi(num_str, &int64_val)) {
        return new ProtoConstraintValueExpression(int64_val);
      } else {
        error_listener()->semanticError(ctx->start, "Invalid number literal");
        return nullptr;
      }
    } else {
      if (!is_long && !is_long_long && absl::SimpleAtoi(num_str, &int32_val)) {
        return new ProtoConstraintValueExpression(int32_val);
      } else if (absl::SimpleAtoi(num_str, &int64_val)) {
        return new ProtoConstraintValueExpression(int64_val);
      } else {
        error_listener()->semanticError(ctx->start, "Invalid number literal");
        return nullptr;
      }
    }
  } else {  // Either uint32 or uint64
    uint32_t uint32_val;
    uint64_t uint64_val;
    if (is_hex) {
      if (!is_long && !is_long_long &&
          absl::SimpleHexAtoi(num_str, &uint32_val)) {
        return new ProtoConstraintValueExpression(uint32_val);
      } else if (absl::SimpleHexAtoi(num_str, &uint64_val)) {
        return new ProtoConstraintValueExpression(uint64_val);
      } else {
        error_listener()->semanticError(ctx->start, "Invalid number literal");
        return nullptr;
      }
    } else {
      if (!is_long && !is_long_long && absl::SimpleAtoi(num_str, &uint32_val)) {
        return new ProtoConstraintValueExpression(uint32_val);
      } else if (absl::SimpleAtoi(num_str, &uint64_val)) {
        return new ProtoConstraintValueExpression(uint64_val);
      } else {
        error_listener()->semanticError(ctx->start, "Invalid number literal");
        return nullptr;
      }
    }
  }
}

// Visits a qualified identifier that specifies an enumerator value.
ProtoConstraintExpression* ProtoFormatVisitor::VisitQualifiedIdent(
    QualifiedIdentCtx* ctx, const google::protobuf::FieldDescriptor* field_desc,
    const ProtoInstructionGroup* inst_group) {
  if (ctx == nullptr) return nullptr;
  // Verify that the field is an enum.
  if (field_desc->enum_type() == nullptr) {
    error_listener()->semanticError(
        ctx->start,
        absl::StrCat("Field '", field_desc->name(), "' is not enum type"));
    return nullptr;
  }
  // Look up the value (if it exists).
  auto const* enum_value_desc =
      field_desc->enum_type()->FindValueByName(ctx->getText());
  if (enum_value_desc == nullptr) {
    error_listener()->semanticError(
        ctx->start,
        absl::StrCat("Enum value not found: '", ctx->getText(), "'"));
    return nullptr;
  }
  return new ProtoConstraintEnumExpression(enum_value_desc);
}

// Process the instruction group setters.
void ProtoFormatVisitor::VisitSetterGroupDef(SetterGroupDefCtx* ctx,
                                             ProtoInstructionGroup* inst_group,
                                             ProtoEncodingInfo* encoding_info) {
  if (ctx == nullptr) return;
  auto group_name = ctx->name->getText();
  for (auto* setter_def : ctx->setter_def()) {
    std::string name = setter_def->name->getText();
    std::string field_name = setter_def->qualified_ident()->getText();
    std::vector<const google::protobuf::FieldDescriptor*> one_of_fields;
    auto const* field_desc =
        GetField(field_name, inst_group->message_type(), one_of_fields);
    if (field_desc == nullptr) {
      error_listener()->semanticError(
          setter_def->start,
          absl::StrCat("Field '", field_name, "' not found in message '",
                       inst_group->message_type()->name(), "'"));
      return;
    }

    IfNotCtx* if_not = setter_def->if_not();
    auto status = encoding_info->CheckSetterType(name, field_desc);
    if (!status.ok()) {
      error_listener()->semanticError(setter_def->start, status.message());
      return;
    }
    status = inst_group->AddSetter(group_name, setter_def, name, field_desc,
                                   one_of_fields, if_not);
    if (!status.ok()) {
      error_listener()->semanticError(setter_def->start, status.message());
      return;
    }
  }
}

// Process local (to instruction) setters.
void ProtoFormatVisitor::VisitSetterDef(SetterDefCtx* ctx,
                                        ProtoInstructionEncoding* inst_encoding,
                                        ProtoInstructionGroup* inst_group,
                                        ProtoEncodingInfo* encoding_info) {
  if (ctx == nullptr) return;
  std::string name = ctx->name->getText();
  std::string field_name = ctx->qualified_ident()->getText();
  std::vector<const google::protobuf::FieldDescriptor*> one_of_fields;
  auto const* field_desc =
      GetField(field_name, inst_group->message_type(), one_of_fields);
  if (field_desc == nullptr) {
    error_listener()->semanticError(
        ctx->start,
        absl::StrCat("Field '", field_name, "' not found in message '",
                     inst_group->message_type()->name(), "'"));
    return;
  }

  IfNotCtx* if_not = ctx->if_not();
  auto status = encoding_info->CheckSetterType(name, field_desc);
  if (!status.ok()) {
    error_listener()->semanticError(ctx->start, status.message());
    return;
  }
  status =
      inst_encoding->AddSetter(ctx, name, field_desc, one_of_fields, if_not);
  if (!status.ok()) {
    error_listener()->semanticError(ctx->start, status.message());
    return;
  }
}

// Process references to instruction group setters.
void ProtoFormatVisitor::VisitSetterRef(SetterRefCtx* ctx,
                                        ProtoInstructionEncoding* inst_encoding,
                                        ProtoInstructionGroup* inst_group,
                                        ProtoEncodingInfo* encoding_info) {
  if (ctx == nullptr) return;
  auto res = inst_group->GetSetterGroup(ctx->name->getText());
  if (!res.ok()) {
    error_listener()->semanticError(ctx->start, res.status().message());
    return;
  }
  auto [iter, end] = res.value();
  while (iter != end) {
    auto* setter_info = iter->second;
    auto status = inst_encoding->AddSetter(
        setter_info->ctx, setter_info->name, setter_info->field_desc,
        setter_info->one_of_fields, setter_info->if_not);
    if (!status.ok()) {
      error_listener()->semanticError(setter_info->ctx->start,
                                      status.message());
      return;
    }
    iter++;
  }
}

// Process the instruction definition generators.
void ProtoFormatVisitor::ProcessInstructionDefGenerator(
    InstructionDefCtx* ctx, ProtoInstructionGroup* inst_group,
    ProtoEncodingInfo* encoding_info) {
  if (ctx == nullptr) return;
  absl::flat_hash_set<std::string> range_variable_names;
  std::vector<RangeAssignmentInfo*> range_info_vec;

  // Process range assignment lists. The range assignment is either a single
  // value or a structured binding assignment. If it's a binding assignment we
  // need to make sure each tuple has the same number of values as there are
  // idents to assign them to.
  for (auto* assign_ctx : ctx->range_assignment()) {
    auto* range_info = new RangeAssignmentInfo();
    range_info_vec.push_back(range_info);
    for (auto* ident_ctx : assign_ctx->IDENT()) {
      std::string name = ident_ctx->getText();
      if (range_variable_names.contains(name)) {
        error_listener()->semanticError(
            assign_ctx->start,
            absl::StrCat("Duplicate binding variable name '", name, "'"));
        continue;
      }
      range_variable_names.insert(name);
      range_info->range_names.push_back(ident_ctx->getText());
      range_info->range_values.push_back({});
      range_info->range_regexes.emplace_back(
          absl::StrCat("\\$\\(", name, "\\)"));
      // Verify that the range variable is used in the string.
      if (!RE2::PartialMatch(ctx->generator_instruction_def_list()->getText(),
                             range_info->range_regexes.back())) {
        error_listener()->semanticWarning(
            assign_ctx->start,
            absl::StrCat("Unreferenced binding variable '", name, "'."));
      }
    }
    // See if it's a list of simple values.
    if (!assign_ctx->gen_value().empty()) {
      for (auto* gen_value_ctx : assign_ctx->gen_value()) {
        if (gen_value_ctx->IDENT() != nullptr) {
          range_info->range_values[0].push_back(
              gen_value_ctx->IDENT()->getText());
        } else if ((gen_value_ctx->value() != nullptr) &&
                   (gen_value_ctx->value()->number() != nullptr)) {
          range_info->range_values[0].push_back(
              gen_value_ctx->value()->number()->getText());
        } else if ((gen_value_ctx->value() != nullptr) &&
                   (gen_value_ctx->value()->bool_value() != nullptr)) {
          range_info->range_values[0].push_back(
              gen_value_ctx->value()->bool_value()->getText());
        } else {
          // Strip off double quotes.
          std::string value = gen_value_ctx->value()->string->getText();
          range_info->range_values[0].push_back(
              value.substr(1, value.size() - 2));
        }
      }
      continue;
    }
    // It's a list of tuples with a structured binding assignment.
    for (auto* tuple_ctx : assign_ctx->tuple()) {
      if (tuple_ctx->gen_value().size() != range_info->range_names.size()) {
        // Clean up.
        for (auto* info : range_info_vec) delete info;
        error_listener_->semanticError(
            assign_ctx->start,
            "Number of values differs from number of identifiers");
        return;
      }
      for (int i = 0; i < tuple_ctx->gen_value().size(); ++i) {
        if (tuple_ctx->gen_value(i)->IDENT() != nullptr) {
          range_info->range_values[i].push_back(
              tuple_ctx->gen_value(i)->IDENT()->getText());
        } else if ((tuple_ctx->gen_value(i)->value() != nullptr) &&
                   (tuple_ctx->gen_value(i)->value()->number() != nullptr)) {
          range_info->range_values[i].push_back(
              tuple_ctx->gen_value(i)->value()->number()->getText());
        } else if ((tuple_ctx->gen_value(i)->value() != nullptr) &&
                   (tuple_ctx->gen_value(i)->value()->bool_value() !=
                    nullptr)) {
          range_info->range_values[i].push_back(
              tuple_ctx->gen_value(i)->value()->bool_value()->getText());
        } else {
          // Strip off double quotes.
          std::string value =
              tuple_ctx->gen_value(i)->value()->string->getText();
          range_info->range_values[i].push_back(
              value.substr(1, value.size() - 2));
        }
      }
    }
  }
  // Check that all binding variable references are valid.
  std::string input_text = ctx->generator_instruction_def_list()->getText();
  std::string::size_type start_pos = 0;
  auto pos = input_text.find_first_of('$', start_pos);
  while (pos != std::string::npos) {
    // Skip past the '$('
    start_pos = pos + 2;
    auto end_pos = input_text.find_first_of(')', pos);
    // Extract the ident.
    auto ident = input_text.substr(start_pos, end_pos - start_pos);
    if (!range_variable_names.contains(ident)) {
      error_listener()->semanticError(
          ctx->generator_instruction_def_list()->start,
          absl::StrCat("Undefined binding variable '", ident, "'"));
    }
    start_pos = end_pos;
    pos = input_text.find_first_of('$', start_pos);
  }
  if (error_listener()->HasError()) {
    // Clean up.
    for (auto* info : range_info_vec) delete info;
    return;
  }

  // Now we need to iterate over the range_info instances and substitution
  // ranges. This will produce new text that will be parsed and processed.
  std::string generated_text =
      GenerateInstructionDefList(range_info_vec, 0, input_text);
  // Parse and process the generated text.
  auto* parser = new ProtoFmtAntlrParserWrapper(generated_text);
  antlr_parser_wrappers_.push_back(parser);
  // Parse the text starting at the opcode_spec_list rule.
  auto instruction_defs =
      parser->parser()->instruction_group_def()->instruction_def();
  // Process the opcode spec.
  for (auto* inst_def : instruction_defs) {
    VisitInstructionDef(inst_def, inst_group, encoding_info);
  }
  // Clean up.
  for (auto* info : range_info_vec) delete info;
}

std::string ProtoFormatVisitor::GenerateInstructionDefList(
    const std::vector<RangeAssignmentInfo*>& range_info_vec, int index,
    const std::string& template_str_in) const {
  std::string generated;
  // Iterate for the number of values.
  for (int i = 0; i < range_info_vec[index]->range_values[0].size(); ++i) {
    // Copy the template string.
    std::string template_str = template_str_in;
    // For each ident, perform substitutions in the template copy with the
    // current set of values.
    int var_index = 0;
    int replace_count = 0;
    for (auto& re : range_info_vec[index]->range_regexes) {
      replace_count += RE2::GlobalReplace(
          &template_str, re,
          range_info_vec[index]->range_values[var_index++][i]);
    }
    // If there are multiple range specifications, then recursively call to
    // generate the cartesian product with the values of the next value range
    // substitutions.
    if (range_info_vec.size() > index + 1) {
      absl::StrAppend(&generated, GenerateInstructionDefList(
                                      range_info_vec, index + 1, template_str));
    } else {
      absl::StrAppend(&generated, template_str);
    }
    // If there were no replacements, then the range variables weren't used,
    // and the template string won't change for any other values in the range.
    // This can happen if there range variables aren't referenced in the string.
    // Thus, break out of the loop.
    if (replace_count == 0) break;
  }
  return generated;
}

std::unique_ptr<ProtoEncodingInfo> ProtoFormatVisitor::VisitDecoderDef(
    DecoderDefCtx* ctx) {
  if (ctx == nullptr) return nullptr;

  // First get the opcode enum.
  int opcode_count = 0;
  std::string opcode_enum;
  for (auto* attr_ctx : ctx->decoder_attribute()) {
    if (attr_ctx->opcode_enum_decl() != nullptr) {
      auto opcode_enum_literal =
          attr_ctx->opcode_enum_decl()->STRING_LITERAL()->getText();
      opcode_enum =
          opcode_enum_literal.substr(1, opcode_enum_literal.size() - 2);
      if (opcode_enum.empty()) {
        error_listener_->semanticError(attr_ctx->start,
                                       "Empty opcode enum string");
      }
      if (opcode_count > 0) {
        error_listener_->semanticError(attr_ctx->start,
                                       "More than one opcode enum declaration");
      }
      opcode_count++;
    }
  }

  // Instantiate encoding info class.
  std::string name = ctx->name->getText();
  auto encoding_info =
      std::make_unique<ProtoEncodingInfo>(opcode_enum, error_listener_.get());
  auto* decoder = encoding_info->SetProtoDecoder(name);
  if (decoder == nullptr) return nullptr;
  absl::flat_hash_set<std::string> group_name_set;
  int namespace_count = 0;
  // Iterate over the decoder attributes.
  for (auto* attr_ctx : ctx->decoder_attribute()) {
    // Include files.
    if (attr_ctx->include_files() != nullptr) {
      for (auto* file_ctx : attr_ctx->include_files()->include_file()) {
        auto include_text = file_ctx->STRING_LITERAL()->getText();
        encoding_info->AddIncludeFile(include_text);
      }
      continue;
    }
    // Namespace declaration.
    if (attr_ctx->namespace_decl() != nullptr) {
      auto decl = attr_ctx->namespace_decl();
      for (auto* namespace_name : decl->namespace_ident()) {
        decoder->namespaces().push_back(namespace_name->getText());
      }
      if (namespace_count > 0) {
        error_listener_->semanticError(attr_ctx->start,
                                       "More than one namespace declaration");
      }
      namespace_count++;
      continue;
    }

    // Instruction groups are listed as either a single instruction group,
    // or a parent group that contains several individual groups.

    // If it is a single group process it here. It's a single instruction
    // group if it does not contain an ident_list.
    if ((attr_ctx->group_name() != nullptr) &&
        (attr_ctx->group_name()->ident_list() == nullptr)) {
      ProcessSingleGroup(attr_ctx, encoding_info.get(), group_name_set);
      continue;
    }

    // If it is a parent group, process it here.
    if ((attr_ctx->group_name() != nullptr) &&
        (attr_ctx->group_name()->ident_list() != nullptr)) {
      ProcessParentGroup(attr_ctx, encoding_info.get(), group_name_set);
      continue;
    }
  }
  if (group_name_set.empty()) {
    error_listener_->semanticError(ctx->start, "No instruction groups found");
  }
  return encoding_info;
}

void ProtoFormatVisitor::ProcessSingleGroup(
    DecoderAttributeCtx* attr_ctx, ProtoEncodingInfo* encoding_info,
    absl::flat_hash_set<std::string>& group_name_set) {
  if (attr_ctx == nullptr) return;
  std::string group_name = attr_ctx->group_name()->IDENT()->getText();

  // If this group has been listed already, signal an error.
  if (group_name_set.contains(group_name)) {
    error_listener_->semanticError(
        attr_ctx->start,
        absl::StrCat("Instruction group '", group_name, "' listed twice"));
    return;
  }

  auto map_iter = encoding_info->instruction_group_map().find(group_name);
  ProtoInstructionGroup* inst_group = nullptr;

  // Check if the group has been visited before. If so, no need to visit.
  if (map_iter != encoding_info->instruction_group_map().end()) {
    inst_group = map_iter->second;
  } else {
    // Check if there is a group declaration for the group name. If not,
    // signal an error for undefined group.
    auto iter = group_decl_map_.find(group_name);
    if (iter == group_decl_map_.end()) {
      error_listener_->semanticError(
          attr_ctx->start,
          absl::StrCat("No such instruction group: '", group_name, "'"));
      return;
    }
    inst_group = VisitInstructionGroupDef(iter->second, encoding_info);
  }
  // Return if there was an error visiting the instruction group.
  if (inst_group == nullptr) return;

  group_name_set.insert(group_name);
  encoding_info->decoder()->AddInstructionGroup(inst_group);
}

void ProtoFormatVisitor::ProcessParentGroup(
    DecoderAttributeCtx* attr_ctx, ProtoEncodingInfo* encoding_info,
    absl::flat_hash_set<std::string>& group_name_set) {
  if (attr_ctx == nullptr) return;
  // Check each of the child groups. Visit any that hasn't been visited
  // yet, and make sure all use the same encoding proto message.
  std::string group_name = attr_ctx->group_name()->IDENT()->getText();
  // It's an error if the instruction group as already been listed.
  if (group_name_set.contains(group_name)) {
    error_listener_->semanticError(
        attr_ctx->start, absl::StrCat("Instruction group '", group_name,
                                      "' listed twice - ignored"));
    return;
  }
  std::vector<ProtoInstructionGroup*> child_groups;
  std::string group_format_name;
  // Iterate through the list of named "child" groups to combine.
  for (auto* ident : attr_ctx->group_name()->ident_list()->IDENT()) {
    auto child_name = ident->getText();
    // Make sure the child group hasn't been listed already.
    if (group_name_set.contains(child_name)) {
      error_listener_->semanticError(
          attr_ctx->start, absl::StrCat("Instruction group listed twice: '",
                                        child_name, "' - ignored"));
      return;
    }
    ProtoInstructionGroup* child_group = nullptr;
    auto map_iter = encoding_info->instruction_group_map().find(child_name);
    if (map_iter != encoding_info->instruction_group_map().end()) {
      // The instruction group has been visited already. Make sure it
      // hasn't bin listed twice in the list of child groups.
      child_group = map_iter->second;
      bool exists = false;
      for (auto const* group : child_groups) {
        if (child_name == group->name()) {
          exists = true;
          break;
        }
      }
      if (exists) {
        error_listener_->semanticError(
            attr_ctx->start,
            absl::StrCat("Instruction group '", child_name, "' listed twice"));
        // Clean up.
        for (auto* child_group : child_groups) delete child_group;
        return;
      }
    } else {
      // The instruction group hasn't been visited yet, so look up the
      // declaration and visit it now.
      auto iter = group_decl_map_.find(child_name);
      if (iter != group_decl_map_.end()) {
        child_group = VisitInstructionGroupDef(iter->second, encoding_info);
      }
      if (child_group == nullptr) {
        error_listener_->semanticError(
            attr_ctx->start,
            absl::StrCat("Instruction group '", child_name, "' not found"));
        // Clean up.
        for (auto* child_group : child_groups) delete child_group;
        continue;
      }
    }
    // If this is the first child group, get the proto message type name.
    if (child_groups.empty()) {
      group_format_name = child_group->message_type()->name();
    }
    // Check that the child groups all use the same proto message type
    // name.
    if (group_format_name != child_group->message_type()->name()) {
      error_listener_->semanticError(
          attr_ctx->start,
          absl::StrCat("Instruction group '", child_name, "' must use format '",
                       group_format_name, ", to be merged into group '",
                       group_name, "'"));
      // Clean up.
      for (auto* child_group : child_groups) delete child_group;
      return;
    }
    child_groups.push_back(child_group);
  }

  if (child_groups.empty()) {
    error_listener_->semanticError(attr_ctx->start, "No child groups");
    // Clean up.
    for (auto* child_group : child_groups) delete child_group;
    return;
  }
  // Create the "parent" instruction group.
  const google::protobuf::Descriptor* group_format =
      message_finder_(Expand(group_format_name));
  if (group_format == nullptr) {
    error_listener_->semanticError(
        attr_ctx->start,
        absl::StrCat("Could not find proto message type '", group_format_name,
                     "' in proto descriptor pool"));
    // Clean up.
    for (auto* child_group : child_groups) delete child_group;
    return;
  }
  auto res = encoding_info->AddInstructionGroup(group_name, group_format);
  if (!res.ok()) {
    error_listener_->semanticError(attr_ctx->start, res.status().message());
    // Clean up.
    for (auto* child_group : child_groups) delete child_group;
    return;
  }
  auto parent_group = res.value();
  // For each child group, add all of it's encodings to the parent group.
  for (auto* child_group : child_groups) {
    for (auto* encoding : child_group->encodings()) {
      parent_group->CopyInstructionEncoding(
          new ProtoInstructionEncoding(*encoding));
    }
    delete child_group;
  }
  // Add the parent instruction group to the decoder.
  group_name_set.insert(parent_group->name());
  encoding_info->decoder()->AddInstructionGroup(parent_group);
}

StringPair ProtoFormatVisitor::EmitCode(ProtoEncodingInfo* encoding_info) {
  return encoding_info->GenerateDecoderClass();
}

}  // namespace proto_fmt
}  // namespace decoder
}  // namespace sim
}  // namespace mpact
