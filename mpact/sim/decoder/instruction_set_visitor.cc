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

#include "mpact/sim/decoder/instruction_set_visitor.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <filesystem>  // NOLINT: third party.
#include <fstream>
#include <iostream>
#include <istream>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/flags/flag.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "antlr4-runtime/ParserRuleContext.h"
#include "mpact/sim/decoder/bundle.h"
#include "mpact/sim/decoder/decoder_error_listener.h"
#include "mpact/sim/decoder/format_name.h"
#include "mpact/sim/decoder/instruction_set.h"
#include "mpact/sim/decoder/instruction_set_contexts.h"
#include "mpact/sim/decoder/opcode.h"
#include "mpact/sim/decoder/slot.h"
#include "mpact/sim/decoder/template_expression.h"
#include "re2/re2.h"

// This flag is used to set the version of the generated code. Version 1 is
// the default version. Version 2 adds an instruction pointer to the resource
// and operator functions in the EncodingInterface.
ABSL_FLAG(unsigned, generator, 1, "Version of generated code");

namespace mpact {
namespace sim {
namespace machine_description {
namespace instruction_set {

static absl::StatusOr<TemplateValue> AbsoluteValueTemplateFunc(
    TemplateInstantiationArgs* args) {
  if (args->size() != 1) {
    return absl::InternalError(absl::StrCat(
        "Wrong number of arguments, expected 1, was given ", args->size()));
  }
  auto result = (*args)[0]->GetValue();
  if (!result.ok()) return result.status();

  auto* value_ptr = std::get_if<int>(&result.value());
  if (value_ptr == nullptr) {
    return absl::InternalError("Type mismatch - int expected");
  }
  int return_value = (*value_ptr < 0) ? -(*value_ptr) : *value_ptr;
  return TemplateValue(return_value);
}

InstructionSetVisitor::InstructionSetVisitor() {
  template_function_evaluators_.insert(std::make_pair(
      "abs", TemplateFunctionEvaluator(AbsoluteValueTemplateFunc, 1)));
}

InstructionSetVisitor::~InstructionSetVisitor() {
  for (auto& [unused, expr_ptr] : constant_map_) {
    delete expr_ptr;
  }
  constant_map_.clear();
  for (auto* wrapper : antlr_parser_wrappers_) {
    delete wrapper;
  }
  antlr_parser_wrappers_.clear();
  for (auto* expr : disasm_field_widths_) delete expr;
  disasm_field_widths_.clear();
}

// Main entry point for processing the file.
absl::Status InstructionSetVisitor::Process(
    const std::vector<std::string>& file_names, const std::string& prefix,
    const std::string& isa_name, const std::vector<std::string>& include_roots,
    absl::string_view directory) {
  generator_version_ = absl::GetFlag(FLAGS_generator);
  // Create and add the error listener.
  set_error_listener(std::make_unique<decoder::DecoderErrorListener>());
  if (isa_name.empty()) {
    error_listener()->semanticError(nullptr, "Isa name cannot be empty");
    return absl::InvalidArgumentError("Isa name cannot be empty");
  }

  for (auto& include_root : include_roots) {
    include_dir_vec_.push_back(include_root);
  }

  // Add the directory of the input file to the include roots.
  if (!file_names.empty()) {
    std::string dir =
        std::filesystem::path(file_names[0]).parent_path().string();
    auto it = std::find(include_dir_vec_.begin(), include_dir_vec_.end(), dir);
    if (it == include_dir_vec_.end()) {
      include_dir_vec_.push_back(dir);
    }
  }

  std::string isa_prefix = prefix;
  std::istream* source_stream = &std::cin;

  if (!file_names.empty()) {
    source_stream = new std::fstream(file_names[0], std::fstream::in);
  }
  // Create an antlr4 stream from the input stream.
  IsaAntlrParserWrapper parser_wrapper(source_stream);
  error_listener()->set_file_name(file_names[0]);
  file_names_.push_back(file_names[0]);
  parser_wrapper.parser()->removeErrorListeners();
  parser_wrapper.parser()->addErrorListener(error_listener());

  // Parse the file and then create the data structures.
  TopLevelCtx* top_level = parser_wrapper.parser()->top_level();

  if (!file_names.empty()) {
    delete source_stream;
    source_stream = nullptr;
  }

  if (error_listener()->HasError() > 0) {
    return absl::InternalError("Errors encountered - terminating.");
  }

  // Visit the parse tree starting at the namespaces declaration.
  VisitTopLevel(top_level);
  // Process additional source files.
  for (int i = 1; i < file_names.size(); ++i) {
    // Add the directory of the input file to the include roots if not already
    // present.
    std::string dir =
        std::filesystem::path(file_names[i]).parent_path().string();
    auto it = std::find(include_dir_vec_.begin(), include_dir_vec_.end(), dir);
    if (it == include_dir_vec_.end()) {
      include_dir_vec_.push_back(dir);
    }
    ParseIncludeFile(top_level, file_names[i], {});
  }
  // Now process the parse tree.
  auto instruction_set = ProcessTopLevel(isa_name);
  // Include files may generate additional syntax errors.
  if (instruction_set == nullptr) {
    return absl::InternalError("Errors encountered - terminating.");
  }
  // Verify that all referenced bundles and slots were defined.
  PerformBundleReferenceChecks(instruction_set.get(),
                               instruction_set->bundle());
  if (error_listener()->HasError() > 0) {
    return absl::InternalError("Errors encountered - terminating.");
  }
  // Analyze resource use and partition resources into simple and complex
  // resources.
  auto status = instruction_set->AnalyzeResourceUse();
  if (!status.ok()) {
    error_listener()->semanticError(nullptr, status.message());
  }

  // If the prefix is empty, use the source file name.
  if (isa_prefix.empty() && file_names.empty()) {
    error_listener()->semanticError(nullptr,
                                    "No prefix or file name specified");
  } else if (isa_prefix.empty()) {
    isa_prefix =
        ToSnakeCase(std::filesystem::path(file_names[0]).stem().string());
  }
  // Check for additional errors.
  if (error_listener()->HasError() > 0) {
    return absl::InternalError("Errors encountered - terminating.");
  }
  std::string encoding_type_name =
      absl::StrCat(ToPascalCase(isa_name), "EncodingBase");

  // Create output streams for .h and .cc files.
  std::string dec_dot_h_name = absl::StrCat(isa_prefix, "_decoder.h");
  std::string dec_dot_cc_name = absl::StrCat(isa_prefix, "_decoder.cc");
  std::string enc_dot_h_name = absl::StrCat(isa_prefix, "_encoder.h");
  std::string enc_dot_cc_name = absl::StrCat(isa_prefix, "_encoder.cc");
  std::string enum_h_name = absl::StrCat(isa_prefix, "_enums.h");
  std::string enum_cc_name = absl::StrCat(isa_prefix, "_enums.cc");
  std::ofstream dec_dot_h_file(absl::StrCat(directory, "/", dec_dot_h_name));
  std::ofstream dec_dot_cc_file(absl::StrCat(directory, "/", dec_dot_cc_name));
  std::ofstream enc_dot_h_file(absl::StrCat(directory, "/", enc_dot_h_name));
  std::ofstream enc_dot_cc_file(absl::StrCat(directory, "/", enc_dot_cc_name));
  std::ofstream enum_h_file(absl::StrCat(directory, "/", enum_h_name));
  std::ofstream enum_cc_file(absl::StrCat(directory, "/", enum_cc_name));
  // Generate the code, close the files and return.
  std::string guard_name = ToHeaderGuard(dec_dot_h_name);
  // Decoder .h file.
  dec_dot_h_file << GenerateHdrFileProlog(dec_dot_h_name, enum_h_name,
                                          guard_name, encoding_type_name,
                                          instruction_set->namespaces());
  dec_dot_h_file << instruction_set->GenerateClassDeclarations(
      dec_dot_h_name, enum_h_name, encoding_type_name);
  dec_dot_h_file << GenerateHdrFileEpilog(guard_name,
                                          instruction_set->namespaces());
  dec_dot_h_file.close();
  // Decoder .cc file.
  dec_dot_cc_file << GenerateCcFileProlog(dec_dot_h_name, /*use_includes=*/true,
                                          instruction_set->namespaces());
  dec_dot_cc_file << instruction_set->GenerateClassDefinitions(
      dec_dot_h_name, encoding_type_name);
  dec_dot_cc_file << GenerateNamespaceEpilog(instruction_set->namespaces());
  dec_dot_cc_file.close();

  // Enum files.
  enum_h_file << GenerateSimpleHdrProlog(ToHeaderGuard(enum_h_name),
                                         instruction_set->namespaces());
  enum_cc_file << GenerateCcFileProlog(enum_h_name, /*use_includes=*/false,
                                       instruction_set->namespaces());
  auto [h_output, cc_output] = instruction_set->GenerateEnums(enum_h_name);
  enum_h_file << h_output;
  enum_cc_file << cc_output;
  enum_h_file << GenerateHdrFileEpilog(ToHeaderGuard(enum_h_name),
                                       instruction_set->namespaces());
  enum_cc_file << GenerateNamespaceEpilog(instruction_set->namespaces());
  enum_h_file.close();
  enum_cc_file.close();

  // Encoder files
  guard_name = ToHeaderGuard(enc_dot_h_name);
  auto [enc_dot_h_prolog, enc_dot_cc_prolog] =
      GenerateEncFilePrologs(enc_dot_h_name, guard_name, enum_h_name,
                             encoding_type_name, instruction_set->namespaces());
  enc_dot_h_file << enc_dot_h_prolog;
  enc_dot_cc_file << enc_dot_cc_prolog;
  auto [h_enc, cc_enc] = instruction_set->GenerateEncClasses(
      enc_dot_h_name, enum_h_name, encoding_type_name);
  enc_dot_h_file << h_enc;
  enc_dot_cc_file << cc_enc;
  enc_dot_h_file << GenerateHdrFileEpilog(guard_name,
                                          instruction_set->namespaces());
  enc_dot_cc_file << GenerateNamespaceEpilog(
      instruction_set->namespaces());  // Enum .h  and .cc files.
  enc_dot_h_file.close();
  enc_dot_cc_file.close();

  return absl::OkStatus();
}

void InstructionSetVisitor::PerformBundleReferenceChecks(
    InstructionSet* instruction_set, Bundle* bundle) {
  // Verify that all referenced bundles were declared.
  for (const auto& bundle_name : bundle->bundle_names()) {
    Bundle* bundle_ref = instruction_set->GetBundle(bundle_name);
    // Perform the check recursively on the referenced bundles.
    PerformBundleReferenceChecks(instruction_set, bundle_ref);
  }
  // Verify that all the slot uses were declared.
  for (auto& [slot_name, instance_vec] : bundle->slot_uses()) {
    Slot* slot = instruction_set->GetSlot(slot_name);
    // Verify that the instance number of the slot falls within valid range.
    for (auto& instance_number : instance_vec) {
      if (instance_number >= slot->size()) {
        auto* token = bundle->ctx() == nullptr ? nullptr : bundle->ctx()->start;
        error_listener()->semanticError(
            token,
            absl::StrCat("Index ", instance_number, " out of range for slot ",
                         slot_name, "' referenced in bundle '", bundle->name(),
                         "'"));
        continue;
      }
    }
    if (slot->is_referenced()) continue;
    if ((slot->default_instruction() == nullptr) ||
        slot->default_instruction()->semfunc_code_string().empty()) {
      error_listener()->semanticError(
          slot->ctx()->start,
          absl::StrCat("Slot '", slot->name(),
                       "' lacks a default semantic action"));
    }
    slot->set_is_referenced(true);
  }
  instruction_set->ComputeSlotAndBundleOrders();
}

void InstructionSetVisitor::VisitTopLevel(TopLevelCtx* ctx) {
  auto declarations = ctx->declaration();

  // Process disasm widths. Only the one in the top level file is used if there
  // are additional ones in included files.
  int count = 0;
  DisasmWidthsCtx* disasm_ctx = nullptr;
  for (auto* decl : declarations) {
    context_file_map_[decl] = current_file_index_;
    if (decl->disasm_widths() == nullptr) continue;
    if (count > 0) {
      error_listener()->semanticError(
          decl->disasm_widths()->start,
          absl::StrCat("Only one `disasm width` declaration allowed - "
                       "previous declaration on line: ",
                       disasm_ctx->start->getLine()));
    }
    disasm_ctx = decl->disasm_widths();
    context_file_map_[disasm_ctx] = context_file_map_.at(decl);
    VisitDisasmWidthsDecl(decl->disasm_widths());
    count++;
  }

  // Parse, but don't process all the slots, bundles, isas and include files.
  PreProcessDeclarations(declarations);
}

std::unique_ptr<InstructionSet> InstructionSetVisitor::ProcessTopLevel(
    absl::string_view isa_name) {
  // At this point we have the contexts for all isas, bundles, and slots.
  // First make sure that the named isa has been defined.
  auto isa_ptr = isa_decl_map_.find(isa_name);
  if (isa_ptr == isa_decl_map_.end()) {
    error_listener()->semanticError(
        nullptr, absl::StrCat("No isa '", isa_name, "' declared"));
    return nullptr;
  }
  // Visit the Isa.
  auto isa = VisitIsaDeclaration(isa_ptr->second);
  return isa;
}

void InstructionSetVisitor::PreProcessDeclarations(
    const std::vector<DeclarationCtx*>& ctx_vec) {
  std::vector<IncludeFileCtx*> include_files;
  // Get handles to the slot, bundle and isa declarations.

  // Create map from slot name to slot ctx.
  for (auto* decl : ctx_vec) {
    if (decl->slot_declaration() != nullptr) {
      auto* slot_ctx = decl->slot_declaration();
      context_file_map_.insert({slot_ctx, current_file_index_});
      auto name = slot_ctx->slot_name->getText();
      auto ptr = slot_decl_map_.find(name);
      if (ptr != slot_decl_map_.end()) {
        error_listener()->semanticError(
            slot_ctx->start,
            absl::StrCat("Slot '", name,
                         "' already declared - previous declaration on line: ",
                         slot_ctx->start->getLine()));
      }
      slot_decl_map_.emplace(name, slot_ctx);
    }
    // Create map from bundle name to bundle ctx.
    if (decl->bundle_declaration() != nullptr) {
      auto* bundle_ctx = decl->bundle_declaration();
      context_file_map_.insert({bundle_ctx, current_file_index_});
      auto name = bundle_ctx->bundle_name->getText();
      auto ptr = bundle_decl_map_.find(name);
      if (ptr != bundle_decl_map_.end()) {
        error_listener()->semanticError(
            bundle_ctx->start,
            absl::StrCat("Bundle '", name,
                         "' already declared - previous declaration on line: ",
                         bundle_ctx->start->getLine()));
        continue;
      }
      bundle_decl_map_.emplace(name, bundle_ctx);
    }
    // Create map from isa name to isa ctx.
    if (decl->isa_declaration() != nullptr) {
      auto* isa_ctx = decl->isa_declaration();
      context_file_map_.insert({isa_ctx, current_file_index_});
      auto name = isa_ctx->instruction_set_name->getText();
      auto ptr = isa_decl_map_.find(name);
      if (ptr != isa_decl_map_.end()) {
        error_listener()->semanticError(
            isa_ctx->start,
            absl::StrCat("Isa '", name,
                         "' already declared - previous declaration on line: ",
                         isa_ctx->start->getLine()));
        continue;
      }
      isa_decl_map_.emplace(name, isa_ctx);
    }

    // Process global include file specifications.
    if (decl->include_file_list() != nullptr) {
      for (auto* include_file : decl->include_file_list()->include_file()) {
        // Insert the string - the call will always succeed, but the insertion
        // does not happen if it already exists.
        include_files_.insert(include_file->STRING_LITERAL()->getText());
      }
    }

    // Process global constants.
    if (decl->constant_def() != nullptr) {
      context_file_map_.insert({decl->constant_def(), current_file_index_});
      VisitConstantDef(decl->constant_def());
    }

    // Process .isa include file.
    if (decl->include_file() != nullptr) {
      include_files.push_back(decl->include_file());
    }
  }
  // Process all include files - this adds to all isa, slot and bundle
  // context maps, as well as all global variables, etc.
  for (auto* include_file_ctx : include_files) {
    VisitIncludeFile(include_file_ctx);
  }
}

std::unique_ptr<InstructionSet> InstructionSetVisitor::VisitIsaDeclaration(
    IsaDeclCtx* ctx) {
  if (ctx == nullptr) return nullptr;
  auto instruction_set =
      std::make_unique<InstructionSet>(ctx->instruction_set_name->getText());
  // An InstructionSet also acts as (has-a bundle) - it's the top level
  // bundle.
  instruction_set->set_bundle(
      new Bundle(instruction_set->name(), instruction_set.get(), nullptr));
  // Visit the namespace declaration, and the bundle and slot references that
  // are part of the instruction_set declaration.
  VisitNamespaceDecl(ctx->namespace_decl(), instruction_set.get());
  context_file_map_[ctx->bundle_list()] = context_file_map_.at(ctx);
  VisitBundleList(ctx->bundle_list(), instruction_set->bundle());
  context_file_map_[ctx->slot_list()] = context_file_map_.at(ctx);
  VisitSlotList(ctx->slot_list(), instruction_set->bundle());
  return instruction_set;
}

void InstructionSetVisitor::VisitNamespaceDecl(NamespaceDeclCtx* ctx,
                                               InstructionSet* isa) {
  if (ctx == nullptr) return;
  for (auto* namespace_name : ctx->namespace_ident()) {
    isa->namespaces().push_back(namespace_name->getText());
  }
}

void InstructionSetVisitor::VisitBundleList(BundleListCtx* ctx,
                                            Bundle* bundle) {
  if (ctx == nullptr) return;
  // Append the list of named bundles referenced within the containing bundle.
  auto bundle_specs_vec = ctx->bundle_spec();
  for (auto bundle_spec : bundle_specs_vec) {
    auto bundle_name = bundle_spec->IDENT()->getText();
    auto iter = bundle_decl_map_.find(bundle_name);
    // If the name hasn't been seen, flag an error.
    if (iter == bundle_decl_map_.end()) {
      error_listener()->semanticError(
          file_names_[context_file_map_.at(ctx)], bundle_spec->start,
          absl::StrCat("Reference to undefined bundle: '", bundle_name, "'"));
      continue;
    }
    // If the bundle hasn't been processed yet, visit its declaration.
    if (!bundle->instruction_set()->bundle_map().contains(bundle_name)) {
      VisitBundleDeclaration(iter->second, bundle->instruction_set());
    }
    bundle->AppendBundleName(bundle_spec->IDENT()->getText());
  }
}

void InstructionSetVisitor::VisitSlotList(SlotListCtx* ctx, Bundle* bundle) {
  if (ctx == nullptr) return;
  // Append the list of named slots referenced within the containing bundle.
  auto slot_specs_vec = ctx->slot_spec();
  for (auto slot_spec : slot_specs_vec) {
    auto slot_name = slot_spec->IDENT()->getText();
    auto iter = slot_decl_map_.find(slot_name);
    // If the slot hasn't been seen, flag an error.
    if (iter == slot_decl_map_.end()) {
      error_listener()->semanticError(
          file_names_[context_file_map_.at(ctx)], slot_spec->start,
          absl::StrCat("Reference to undefined slot: '", slot_name, "'"));
      continue;
    }
    // If the slot hasn't been processed yet, visit its declaration.
    if (!bundle->instruction_set()->slot_map().contains(slot_name)) {
      VisitSlotDeclaration(iter->second, bundle->instruction_set());
    }
    // First obtain the vector of instance indices specified before appending.
    std::vector<int> instances = VisitArraySpec(slot_spec->array_spec());
    bundle->AppendSlot(slot_name, instances);
  }
}

std::vector<int> InstructionSetVisitor::VisitArraySpec(ArraySpecCtx* ctx) {
  std::vector<int> instances;

  // If there are not array specifications, return the empty vector.
  if (ctx == nullptr) return instances;

  auto range_spec_vec = ctx->range_spec();
  for (auto range_spec : range_spec_vec) {
    // The range spec is on the form of n, or m..n. Add the appropriate
    // indices to the instances vector.
    int range_start;
    int range_end;
    range_end = range_start = std::stoi(range_spec->range_start->getText());
    if (range_spec->range_end != nullptr) {
      range_end = std::stoi(range_spec->range_end->getText());
    }
    for (int instance = range_start; instance <= range_end; instance++) {
      instances.push_back(instance);
    }
  }
  return instances;
}

void InstructionSetVisitor::VisitConstantDef(ConstantDefCtx* ctx) {
  if (ctx == nullptr) return;
  std::string ident = ctx->ident()->getText();
  std::string type = ctx->template_parameter_type()->getText();
  context_file_map_.insert({ctx->expression(), context_file_map_.at(ctx)});
  auto* expr = VisitExpression(ctx->expression(), nullptr, nullptr);
  auto status = AddConstant(ident, type, expr);
  if (!status.ok()) {
    delete expr;
    error_listener()->semanticError(ctx->ident()->start, status.message());
  }
}

void InstructionSetVisitor::VisitIncludeFile(IncludeFileCtx* ctx) {
  // The literal includes the double quotes.
  std::string literal = ctx->STRING_LITERAL()->getText();
  // Remove the double quotes from the literal and construct the full file
  // name.
  std::string file_name = literal.substr(1, literal.length() - 2);
  // Check for recursive include.
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

void InstructionSetVisitor::ParseIncludeFile(
    antlr4::ParserRuleContext* ctx, const std::string& file_name,
    const std::vector<std::string>& dirs) {
  std::fstream include_file;
  // Open include file.
  include_file.open(file_name, std::fstream::in);
  std::string include_name;
  if (!include_file.is_open()) {
    // Try each of the include file directories.
    for (auto const& dir : dirs) {
      include_name = dir + "/" + file_name;
      include_file.open(include_name, std::fstream::in);
      if (include_file.is_open()) break;
    }
    if (!include_file.is_open()) {
      error_listener()->semanticError(
          ctx == nullptr ? nullptr : ctx->start,
          absl::StrCat("Failed to open '", file_name, "'", " ", dirs.size()));
      return;
    }
  }
  // Add the directory of the include file to the include roots if not already
  // present.
  std::string dir = std::filesystem::path(include_name).parent_path().string();
  auto it = std::find(include_dir_vec_.begin(), include_dir_vec_.end(), dir);
  if (it == include_dir_vec_.end()) {
    include_dir_vec_.push_back(dir);
  }
  std::string previous_file_name = error_listener()->file_name();
  int previous_file_index_ = current_file_index_;
  error_listener()->set_file_name(file_name);
  file_names_.push_back(file_name);
  current_file_index_ = file_names_.size() - 1;
  // Create an antlr4 stream from the input stream.
  auto* include_parser = new IsaAntlrParserWrapper(&include_file);
  // We need to save the parser state so it's available for analysis after
  // we are done with building the parse trees.
  antlr_parser_wrappers_.push_back(include_parser);
  // Add the error listener.
  include_parser->parser()->removeErrorListeners();
  include_parser->parser()->addErrorListener(error_listener());
  // Start parsing at the include_top_level rule.
  auto declaration_vec =
      include_parser->parser()->include_top_level()->declaration();
  include_file.close();
  if (error_listener()->syntax_error_count() > 0) {
    error_listener()->set_file_name(previous_file_name);
    current_file_index_ = previous_file_index_;
    return;
  }
  include_file_stack_.push_back(file_name);
  PreProcessDeclarations(declaration_vec);
  include_file_stack_.pop_back();
  error_listener()->set_file_name(previous_file_name);
  current_file_index_ = previous_file_index_;
}

void InstructionSetVisitor::VisitBundleDeclaration(
    BundleDeclCtx* ctx, InstructionSet* instruction_set) {
  if (ctx == nullptr) return;
  Bundle* bundle =
      new Bundle(ctx->bundle_name->getText(), instruction_set, ctx);
  instruction_set->AddBundle(bundle);
  int num_slot_lists = 0;
  int num_bundle_lists = 0;
  int num_include_file_lists = 0;
  int num_semfunc_specs = 0;
  for (auto* part : ctx->bundle_parts()) {
    if (part->slot_list() != nullptr) {
      if (num_slot_lists > 0) {
        error_listener()->semanticError(file_names_[context_file_map_.at(ctx)],
                                        part->start,
                                        "Multiple slot lists in bundle");
        return;
      }
      context_file_map_[part->slot_list()] = context_file_map_.at(ctx);
      VisitSlotList(part->slot_list(), bundle);
      num_slot_lists++;
      continue;
    }
    if (part->bundle_list() != nullptr) {
      if (num_bundle_lists > 0) {
        error_listener()->semanticError(file_names_[context_file_map_.at(ctx)],
                                        part->start,
                                        "Multiple bundle lists in bundle");
        return;
      }
      context_file_map_[part->bundle_list()] = context_file_map_.at(ctx);
      VisitBundleList(part->bundle_list(), bundle);
      num_bundle_lists++;
      continue;
    }
    if (part->include_file_list() != nullptr) {
      if (num_include_file_lists > 0) {
        error_listener()->semanticError(
            file_names_[context_file_map_.at(ctx)], part->start,
            "Multiple include file lists in bundle");
        return;
      }
      for (auto* include_file : part->include_file_list()->include_file()) {
        // Insert the string - the call will always succeed, but the insertion
        // does not happen if it already exists.
        include_files_.insert(include_file->STRING_LITERAL()->getText());
      }
      num_include_file_lists++;
      continue;
    }
    if (part->semfunc_spec() != nullptr) {
      if (num_semfunc_specs > 0) {
        error_listener()->semanticError(file_names_[context_file_map_.at(ctx)],
                                        part->start,
                                        "Multiple semfunc specs in bundle");
        return;
      }
      std::string string_literal =
          part->semfunc_spec()->STRING_LITERAL(0)->getText();
      // Strip double quotes.
      std::string code_string =
          string_literal.substr(1, string_literal.length() - 2);
      bundle->set_semfunc_code_string(code_string);
      num_semfunc_specs++;
      continue;
    }
    error_listener()->semanticError(file_names_[context_file_map_.at(ctx)],
                                    part->start, "Unhandled bundle part type");
    return;
  }
}

void InstructionSetVisitor::VisitSlotDeclaration(
    SlotDeclCtx* ctx, InstructionSet* instruction_set) {
  bool is_templated = ctx->template_decl() != nullptr;
  Slot* slot = new Slot(ctx->slot_name->getText(), instruction_set,
                        is_templated, ctx, generator_version_);
  if (is_templated) {
    for (auto const& param : ctx->template_decl()->template_parameter_decl()) {
      auto status = slot->AddTemplateFormal(param->IDENT()->getText());
      if (!status.ok()) {
        error_listener()->semanticError(
            file_names_[context_file_map_.at(slot->ctx())], param->start,
            status.message());
      }
    }
  }
  // Set the base slot if it inherits.
  if (ctx->base_item_list() != nullptr) {
    // For each entry in the list of slots to derive from.
    for (auto* base_item : ctx->base_item_list()->base_item()) {
      std::string base_name = base_item->IDENT()->getText();
      // If the base slot does has not been seen - undefined error.
      auto slot_iter = slot_decl_map_.find(base_name);
      if (slot_iter == slot_decl_map_.end()) {
        error_listener()->semanticError(
            file_names_[context_file_map_.at(ctx)], base_item->start,
            absl::StrCat("Undefined base slot: ", base_name));
        continue;
      }
      // If the slot hasn't been visited, visit it.
      auto* base = instruction_set->GetSlot(base_name);
      if (base == nullptr) {
        VisitSlotDeclaration(slot_iter->second, instruction_set);
        base = instruction_set->GetSlot(base_name);
      }
      // Now check if the base slot is templated or not, and if the template
      // arguments are present or not.
      auto* template_spec = base_item->template_spec();
      if ((template_spec != nullptr) && !base->is_templated()) {
        // Template arguments are present but the slot isn't templated.
        error_listener()->semanticError(
            file_names_[context_file_map_.at(ctx)], base_item->start,
            absl::StrCat("'", base_name, "' is not a templated slot"));
        continue;
      }
      if ((template_spec == nullptr) && base->is_templated()) {
        // The slot is templated, but no template arguments.
        error_listener()->semanticError(
            file_names_[context_file_map_.at(ctx)], base_item->start,
            absl::StrCat("Missing template arguments for slot '", base_name,
                         "'"));
        continue;
      }
      if (template_spec != nullptr) {
        // Check that the number of arguments match.
        int arg_count = template_spec->expression().size();
        int param_count = base->template_parameters().size();
        if (arg_count != param_count) {
          error_listener()->semanticError(
              file_names_[context_file_map_.at(ctx)], template_spec->start,
              absl::StrCat("Wrong number of arguments: ", param_count,
                           " were expected, ", arg_count, " were provided"));
          continue;
        }
        bool has_error = false;
        // Build up the argument vector.
        auto* arguments = new TemplateInstantiationArgs;
        for (auto* template_arg : template_spec->expression()) {
          context_file_map_.insert({template_arg, context_file_map_.at(ctx)});
          auto* expr = VisitExpression(template_arg, slot, nullptr);
          if (expr == nullptr) {
            error_listener()->semanticError(
                file_names_[context_file_map_.at(ctx)], template_arg->start,
                "Error in template expression");
            has_error = true;
            break;
          }
          arguments->push_back(expr);
        }
        if (has_error) {
          for (auto* expr : *arguments) {
            delete expr;
          }
          delete arguments;
          continue;
        }
        auto result = slot->AddBase(base, arguments);
        if (!result.ok()) {
          error_listener()->semanticError(
              file_names_[context_file_map_.at(ctx)], base_item->start,
              result.message());
        }
      } else {
        // No template arguments.
        auto result = slot->AddBase(base);
        if (!result.ok()) {
          error_listener()->semanticError(
              file_names_[context_file_map_.at(ctx)], base_item->start,
              result.message());
        }
      }
    }
  }
  // Set the size if it is replicated.
  if (ctx->size_spec() != nullptr) {
    int size = std::stoi(ctx->size_spec()->NUMBER()->getText(), nullptr, 0);
    slot->set_size(size);
  }
  // Add the slot to the ISA.
  instruction_set->AddSlot(slot);
  for (auto* decl_ctx : ctx->const_and_default_decl()) {
    context_file_map_[decl_ctx] = context_file_map_.at(ctx);
    VisitConstAndDefaultDecls(decl_ctx, slot);
  }
  context_file_map_[ctx->opcode_list()] = context_file_map_.at(ctx);
  VisitOpcodeList(ctx->opcode_list(), slot);
}

void InstructionSetVisitor::VisitDisasmWidthsDecl(DisasmWidthsCtx* ctx) {
  for (auto* expr : ctx->expression()) {
    context_file_map_.insert({expr, context_file_map_.at(ctx)});
    auto* width_expr = VisitExpression(expr, nullptr, nullptr);
    if ((width_expr == nullptr) || (!width_expr->IsConstant())) {
      error_listener()->semanticError(expr->start,
                                      "Expression must be constant");
      continue;
    }
    disasm_field_widths_.push_back(width_expr);
  }
}

void InstructionSetVisitor::VisitConstAndDefaultDecls(ConstAndDefaultCtx* ctx,
                                                      Slot* slot) {
  if (ctx == nullptr) return;
  // A constant declaration.
  auto* const_def = ctx->constant_def();
  if (const_def != nullptr) {  // Constant declaration.
    std::string ident = const_def->ident()->getText();
    std::string type = const_def->template_parameter_type()->getText();
    context_file_map_.insert(
        {const_def->expression(), context_file_map_.at(ctx)});
    auto* expr = VisitExpression(const_def->expression(), slot, nullptr);
    if (expr == nullptr) {
      delete expr;
      error_listener()->semanticError(file_names_[context_file_map_.at(ctx)],
                                      const_def->expression()->start,
                                      "Error in expression");
      return;
    }
    auto status = slot->AddConstant(ident, type, expr);
    if (!status.ok()) {
      delete expr;
      error_listener()->semanticError(file_names_[context_file_map_.at(ctx)],
                                      const_def->ident()->start,
                                      status.message());
    }
    return;
  }
  if (ctx->SIZE() != nullptr) {  // Default size.
    int value = std::stoi(ctx->NUMBER()->getText(), nullptr, 0);
    slot->set_default_instruction_size(value);
    return;
  }
  if (ctx->LATENCY() != nullptr) {  // Default latency.
    context_file_map_.insert({ctx->expression(), context_file_map_.at(ctx)});
    auto* expr = VisitExpression(ctx->expression(), slot, nullptr);
    if (expr == nullptr) {
      delete expr;
      error_listener()->semanticError(file_names_[context_file_map_.at(ctx)],
                                      ctx->expression()->start,
                                      "Error in expression");
      return;
    }
    slot->set_default_latency(expr);
    return;
  }
  if (ctx->ATTRIBUTES() != nullptr) {  // Default attributes.
    context_file_map_.insert(
        {ctx->instruction_attribute_list(), context_file_map_.at(ctx)});
    VisitInstructionAttributeList(ctx->instruction_attribute_list(), slot,
                                  nullptr);
    return;
  }
  // Add any include files to our set of includes.
  if (ctx->include_file_list() != nullptr) {
    for (auto* include_file : ctx->include_file_list()->include_file()) {
      // Insert the string - the call will always succeed, but the insertion
      // does not happen if it already exists.
      include_files_.insert(include_file->STRING_LITERAL()->getText());
    }
  }
  if (ctx->OPCODE() != nullptr) {  // Default opcode.
    // Process the "default" instruction, which is used to specify disassembly
    // and semantic function for when no valid opcode is found during decode.
    if (slot->default_instruction() != nullptr) {
      error_listener()->semanticError(
          file_names_[context_file_map_.at(ctx)], ctx->start,
          "Multiple definitions of 'default' opcode");
      return;
    }
    auto* default_instruction = new Instruction(
        slot->instruction_set()->opcode_factory()->CreateDefaultOpcode(), slot);
    bool has_disasm = false;
    bool has_semfunc = false;
    for (auto* attribute : ctx->opcode_attribute_list()->opcode_attribute()) {
      // Disasm spec.
      if (attribute->disasm_spec() != nullptr) {
        if (has_disasm) {
          error_listener()->semanticError(
              file_names_[context_file_map_.at(ctx)], attribute->start,
              "Duplicate disasm declaration");
          continue;
        }
        has_disasm = true;
        for (auto* format_str : attribute->disasm_spec()->STRING_LITERAL()) {
          std::string format = format_str->getText();
          // Trim the double quotes.
          format.erase(format.size() - 1, 1);
          format.erase(0, 1);
          auto status = ParseDisasmFormat(format, default_instruction);
          if (!status.ok()) {
            error_listener()->semanticError(attribute->disasm_spec()->start,
                                            status.message());
          }
        }
        continue;
      }
      // Semfunc spec.
      // If a semfunc has already been declared, signal an error.
      if (has_semfunc) {
        error_listener()->semanticError(file_names_[context_file_map_.at(ctx)],
                                        attribute->start,
                                        "Duplicate semfunc declaration");
        continue;
      }
      has_semfunc = true;
      auto semfunc_code = attribute->semfunc_spec()->STRING_LITERAL();
      // Only one semfunc specification (no child instructions) for default
      // opcode.
      if (semfunc_code.size() > 1) {
        error_listener()->semanticError(
            file_names_[context_file_map_.at(ctx)], ctx->start,
            "Only one semfunc specification per default opcode");
        continue;
      }
      std::string string_literal = semfunc_code[0]->getText();
      // Strip double quotes.
      std::string code_string =
          string_literal.substr(1, string_literal.length() - 2);
      default_instruction->set_semfunc_code_string(code_string);
    }
    if (has_semfunc) {
      slot->set_default_instruction(default_instruction);
    } else {
      delete default_instruction;
      error_listener()->semanticError(
          file_names_[context_file_map_.at(ctx)], ctx->start,
          "Default opcode lacks mandatory semfunc specification");
    }
  }
  if (ctx->resource_details() != nullptr) {
    std::string ident = ctx->ident()->getText();
    if (slot->resource_spec_map().contains(ident)) {
      error_listener()->semanticError(
          file_names_[context_file_map_.at(ctx)], ctx->ident()->start,
          absl::StrCat("Resources '", ident, "': duplicate definition"));
      return;
    }
    // Save the context. It will be re-visited at each point of use.
    slot->resource_spec_map().insert(
        std::make_pair(ident, ctx->resource_details()));
  }
}

// Visit the template argument recursively to create an expression tree that
// can be evaluated later. No need to coalesce constant expression trees, the
// savings aren't that great.
TemplateExpression* InstructionSetVisitor::VisitExpression(ExpressionCtx* ctx,
                                                           Slot* slot,
                                                           Instruction* inst) {
  if (ctx == nullptr) return nullptr;
  if (ctx->negop() != nullptr) {
    context_file_map_.insert({ctx->expr, context_file_map_.at(ctx)});
    TemplateExpression* expr = VisitExpression(ctx->expr, slot, inst);
    if (expr == nullptr) return nullptr;
    return new TemplateNegate(expr);
  }

  if (ctx->mulop() != nullptr) {
    std::string op = ctx->mulop()->getText();
    context_file_map_.insert({ctx->lhs, context_file_map_.at(ctx)});
    TemplateExpression* lhs = VisitExpression(ctx->lhs, slot, inst);
    if (lhs == nullptr) return nullptr;
    context_file_map_.insert({ctx->rhs, context_file_map_.at(ctx)});
    TemplateExpression* rhs = VisitExpression(ctx->rhs, slot, inst);
    if (rhs == nullptr) {
      delete lhs;
      return nullptr;
    }
    if (op == "*") return new TemplateMultiply(lhs, rhs);
    return new TemplateDivide(lhs, rhs);
  }

  if (ctx->addop() != nullptr) {
    std::string op = ctx->addop()->getText();
    context_file_map_.insert({ctx->lhs, context_file_map_.at(ctx)});
    TemplateExpression* lhs = VisitExpression(ctx->lhs, slot, inst);
    if (lhs == nullptr) return nullptr;
    context_file_map_.insert({ctx->rhs, context_file_map_.at(ctx)});
    TemplateExpression* rhs = VisitExpression(ctx->rhs, slot, inst);
    if (rhs == nullptr) {
      delete lhs;
      return nullptr;
    }
    if (op == "+") return new TemplateAdd(lhs, rhs);
    return new TemplateSubtract(lhs, rhs);
  }

  if (ctx->func != nullptr) {
    std::string function = ctx->func->getText();
    auto iter = template_function_evaluators_.find(function);
    if (iter == template_function_evaluators_.end()) {
      error_listener()->semanticError(
          file_names_[context_file_map_.at(ctx)], ctx->start,
          absl::StrCat("No function '", function, "' supported"));
      return nullptr;
    }
    auto evaluator = iter->second;
    if (ctx->expression().size() != evaluator.arity) {
      error_listener()->semanticError(
          file_names_[context_file_map_.at(ctx)], ctx->start,
          absl::StrCat("Function '", function, "' takes ", evaluator.arity,
                       " parameters, but ", ctx->expression().size(),
                       " were given"));
    }
    auto* args = new TemplateInstantiationArgs;
    bool has_error = false;
    for (auto* expr_ctx : ctx->expression()) {
      context_file_map_.insert({expr_ctx, context_file_map_.at(ctx)});
      auto* expr = VisitExpression(expr_ctx, slot, inst);
      if (expr == nullptr) {
        has_error = true;
        break;
      }
      args->push_back(expr);
    }
    if (has_error) {
      for (auto* expr : *args) {
        delete expr;
      }
      delete args;
      return nullptr;
    }
    return new TemplateFunction(evaluator.function, args);
  }

  if (ctx->paren_expr != nullptr) {
    context_file_map_.insert({ctx->paren_expr, context_file_map_.at(ctx)});
    return VisitExpression(ctx->paren_expr, slot, inst);
  }

  if (ctx->NUMBER() != nullptr) {
    auto* expr =
        new TemplateConstant(std::stoi(ctx->NUMBER()->getText(), nullptr, 0));
    return expr;
  }

  if (ctx->IDENT() != nullptr) {
    std::string ident = ctx->IDENT()->getText();
    // Four possibilities. A global constant, a slot local constant, a
    // template formal, or a reference to a destination operand.
    if (slot != nullptr) {
      TemplateFormal* param = slot->GetTemplateFormal(ident);
      if (param != nullptr) return new TemplateParam(param);

      // Check if it's a slot const expression.
      auto* expr = slot->GetConstExpression(ident);
      if (expr != nullptr) return expr->DeepCopy();
    }

    // It should be an opcode operand term, which means it should be a
    // destination operand with a latency. That is the value/expression that
    // is needed here.
    if (inst != nullptr) {
      auto* op = inst->GetDestOp(ident);
      if (op == nullptr) {
        error_listener()->semanticError(
            file_names_[context_file_map_.at(ctx)], ctx->start,
            absl::StrCat("'", ident,
                         "' is not a valid destination operand for opcode '",
                         inst->opcode()->name(), "'"));
        return nullptr;
      }

      auto* expr = op->expression();
      if (expr != nullptr) return expr->DeepCopy();

      // expr is null, this means that the destination operand has a decode
      // time computed latency. This is a special case that will be addressed
      // later. For now, signal an unsupported error.
      error_listener()->semanticError(file_names_[context_file_map_.at(ctx)],
                                      ctx->start,
                                      "Decode time evaluation of latency "
                                      "expression not supported for resources");
      return nullptr;
    }

    auto* expr = GetConstExpression(ident);
    if (expr != nullptr) return expr->DeepCopy();

    error_listener()->semanticError(
        file_names_[context_file_map_.at(ctx)], ctx->start,
        absl::StrCat("Unable to evaluate expression: ", "'", ctx->getText(),
                     "'"));
  }

  return nullptr;
}

DestinationOperand* InstructionSetVisitor::FindDestinationOpInExpression(
    ExpressionCtx* ctx, const Slot* slot, const Instruction* inst) {
  if (ctx == nullptr) return nullptr;
  if (ctx->negop() != nullptr) {
    context_file_map_.insert({ctx->expr, context_file_map_.at(ctx)});
    return FindDestinationOpInExpression(ctx->expr, slot, inst);
  }
  if ((ctx->mulop() != nullptr) || (ctx->addop() != nullptr)) {
    context_file_map_.insert({ctx->lhs, context_file_map_.at(ctx)});
    context_file_map_.insert({ctx->rhs, context_file_map_.at(ctx)});
    auto* lhs = FindDestinationOpInExpression(ctx->lhs, slot, inst);
    auto* rhs = FindDestinationOpInExpression(ctx->rhs, slot, inst);
    if (lhs == nullptr) return rhs;
    if (rhs == nullptr) return lhs;
    if (lhs == rhs) return lhs;

    error_listener()->semanticError(
        ctx->start,
        "Resource reference can only reference a single "
        "destination operand");
    return nullptr;
  }
  if (ctx->paren_expr != nullptr) {
    context_file_map_.insert({ctx->paren_expr, context_file_map_.at(ctx)});
    return FindDestinationOpInExpression(ctx->paren_expr, slot, inst);
  }
  if (ctx->NUMBER() != nullptr) {
    return nullptr;
  }
  if (ctx->func != nullptr) {
    DestinationOperand* dest_op = nullptr;
    DestinationOperand* tmp_op;
    for (auto* expr_ctx : ctx->expression()) {
      context_file_map_.insert({expr_ctx, context_file_map_.at(ctx)});
      tmp_op = FindDestinationOpInExpression(expr_ctx, slot, inst);
      if (dest_op == nullptr) {
        dest_op = tmp_op;
        continue;
      }
      if (tmp_op != nullptr) {
        if (dest_op != tmp_op) {
          error_listener()->semanticError(
              ctx->start,
              "Resource reference can only reference a single "
              "destination operand");
        }
      }
    }
    return dest_op;
  }
  std::string ident = ctx->IDENT()->getText();
  // It is either a slot local constant, a template formal, or a reference
  // to a destination operand.
  TemplateFormal* param = slot->GetTemplateFormal(ident);
  if (param != nullptr) return nullptr;
  // Check if it's a slot const expression.
  auto* expr = slot->GetConstExpression(ident);
  if (expr != nullptr) return nullptr;
  // It should be an opcode operand term.
  return inst->GetDestOp(ident);
}

void InstructionSetVisitor::VisitOpcodeList(OpcodeListCtx* ctx, Slot* slot) {
  absl::flat_hash_set<std::string> deleted_ops_set;
  absl::flat_hash_set<OpcodeSpecCtx*> overridden_ops_set;
  std::vector<Instruction*> instruction_vec;
  if (ctx != nullptr) {
    ProcessOpcodeList(ctx, slot, instruction_vec, deleted_ops_set,
                      overridden_ops_set);
  }
  // For all base slots, and all opcodes that aren't excluded, add the opcodes
  // to the current slot. When adding the instruction, pass in any template
  // instantiation arguments to the base slot so that any expressions for
  // destination operand latencies can be evaluated.
  for (auto const& base_slot : slot->base_slots()) {
    if (base_slot.base->min_instruction_size() < slot->min_instruction_size()) {
      slot->set_min_instruction_size(base_slot.base->min_instruction_size());
    }
    // Copy over the instructions that were not deleted.
    for (auto& [unused, inst_ptr] : base_slot.base->instruction_map()) {
      if (!deleted_ops_set.contains(inst_ptr->opcode()->name())) {
        absl::Status status =
            slot->AppendInheritedInstruction(inst_ptr, base_slot.arguments);
        if (!status.ok()) {
          error_listener()->semanticError(
              file_names_[context_file_map_.at(ctx)], ctx->start,
              status.message());
        }
      }
    }
    // Perform the overrides.
    PerformOpcodeOverrides(overridden_ops_set, slot);
  }
  // Add the declared opcodes.
  for (auto* inst : instruction_vec) {
    absl::Status status = slot->AppendInstruction(inst);
    if (!status.ok()) {
      error_listener()->semanticError(file_names_[context_file_map_.at(ctx)],
                                      ctx->start, status.message());
    }
  }
}

void InstructionSetVisitor::PerformOpcodeOverrides(
    absl::flat_hash_set<OpcodeSpecCtx*> overridden_ops_set, Slot* slot) {
  for (auto* override_ctx : overridden_ops_set) {
    std::string name = override_ctx->name->getText();
    auto* inst = slot->instruction_map().at(name);
    VisitOpcodeAttributes(override_ctx->opcode_attribute_list(), inst, slot);
  }
}

void InstructionSetVisitor::VisitOpcodeAttributes(OpcodeAttributeListCtx* ctx,
                                                  Instruction* inst,
                                                  Slot* slot) {
  if (ctx == nullptr) return;
  // These flags are used to detect multiple instances of each attribute.
  bool has_disasm = false;
  bool has_semfunc = false;
  bool has_resources = false;
  bool has_attributes = false;
  // Visit the opcode attributes.
  for (auto* attribute_ctx : ctx->opcode_attribute()) {
    // Process any disassembly specifications.
    if (attribute_ctx->disasm_spec() != nullptr) {
      // In case of override, need to clear any disasm info in instruction.
      inst->ClearDisasmFormat();
      // Signal error if there is more than one disassembly spec.
      if (has_disasm) {
        error_listener()->semanticError(
            file_names_[context_file_map_.at(slot->ctx())],
            attribute_ctx->start, "Multiple disasm specifications");
        continue;
      }
      has_disasm = true;
      for (auto* disasm_fmt : attribute_ctx->disasm_spec()->STRING_LITERAL()) {
        std::string format = disasm_fmt->getText();
        // Trim the double quotes.
        format.erase(format.size() - 1, 1);
        format.erase(0, 1);
        auto status = ParseDisasmFormat(format, inst);
        if (!status.ok()) {
          error_listener()->semanticError(
              file_names_[context_file_map_.at(slot->ctx())],
              attribute_ctx->disasm_spec()->start, status.message());
          has_disasm = false;
          break;
        }
      }
      continue;
    }

    // Process the semantic function specification.
    if (attribute_ctx->semfunc_spec() != nullptr) {
      // In case of override, need to clear the semantic function string.
      inst->ClearSemfuncCodeString();
      // Signal error if there is more than one semantic function spec.
      if (has_semfunc) {
        error_listener()->semanticError(
            file_names_[context_file_map_.at(slot->ctx())],
            attribute_ctx->start, "Multiple semfunc specifications");
        continue;
      }
      has_semfunc = true;
      VisitSemfuncSpec(attribute_ctx->semfunc_spec(), inst);
      continue;
    }

    // Process resource specification.
    if (attribute_ctx->resource_spec() != nullptr) {
      // In case of override, need to clear the resource specifications.
      inst->ClearResourceSpecs();
      // Signal error if there is more than one resource specification.
      if (has_resources) {
        error_listener()->semanticError(
            file_names_[context_file_map_.at(slot->ctx())],
            attribute_ctx->start, "Multiple resource specifications");
        continue;
      }
      has_resources = true;
      VisitResourceDetails(attribute_ctx->resource_spec()->resource_details(),
                           inst, slot);
      continue;
    }

    // Process instruction attribute specification.
    if (attribute_ctx->instruction_attribute_spec() != nullptr) {
      // In case of override, need to clear the attribute specification.
      inst->ClearAttributeSpecs();
      // Signal error if there is more than one attribute specification.
      if (has_attributes) {
        error_listener()->semanticError(
            file_names_[context_file_map_.at(slot->ctx())],
            attribute_ctx->start, "Multiple attribute specifications");
        continue;
      }
      has_attributes = true;
      auto attr_list_ctx = attribute_ctx->instruction_attribute_spec()
                               ->instruction_attribute_list();
      VisitInstructionAttributeList(attr_list_ctx, slot, inst);
      continue;
    }

    // Unknown attribute type.
    error_listener()->semanticError(
        file_names_[context_file_map_.at(slot->ctx())], attribute_ctx->start,
        "Unknown attribute type");
  }
}

void InstructionSetVisitor::VisitInstructionAttributeList(
    InstructionAttributeListCtx* ctx, Slot* slot, Instruction* inst) {
  absl::flat_hash_map<std::string, TemplateExpression*> attributes;
  for (auto* attribute : ctx->instruction_attribute()) {
    std::string name = attribute->IDENT()->getText();
    if (attributes.find(name) != attributes.end()) {
      error_listener()->semanticError(
          file_names_[context_file_map_.at(slot->ctx())], attribute->start,
          absl::StrCat("Duplicate attribute name '", name, "' in list"));
      continue;
    }
    InstructionSet::AddAttributeName(name);
    if (attribute->expression() != nullptr) {
      context_file_map_.insert(
          {attribute->expression(), context_file_map_.at(slot->ctx())});
      auto* expr = VisitExpression(attribute->expression(), slot, inst);
      attributes.emplace(name, expr);
      continue;
    }
    attributes.emplace(name, new TemplateConstant(1));
  }
  // Are we parsing attributes for an instruction?
  if (inst != nullptr) {
    for (auto* child = inst; child != nullptr; child = child->child()) {
      for (auto& [name, expr] : attributes) {
        child->AddInstructionAttribute(name, expr);
      }
    }
    // Ownership of expr objects transferred to opcode.
    attributes.clear();
    return;
  }
  // Attributes are default attributes for the current slot.
  for (auto& [name, expr] : attributes) {
    slot->AddInstructionAttribute(name, expr);
  }
  attributes.clear();
}

void InstructionSetVisitor::VisitSemfuncSpec(SemfuncSpecCtx* semfunc_spec,
                                             Instruction* inst) {
  auto* child = inst;
  // Parse each string in the list of semantic function specifications. There
  // should be one the opcode and one for each child opcode.
  for (auto* sem_func : semfunc_spec->STRING_LITERAL()) {
    if (child == nullptr) {
      error_listener()->semanticWarning(
          file_names_[context_file_map_.at(inst->slot()->ctx())],
          sem_func->getSymbol(), "Ignoring extra semfunc spec");
      break;
    }
    std::string literal = sem_func->getText();
    std::string code_string = literal.substr(1, literal.length() - 2);
    child->set_semfunc_code_string(code_string);
    child = child->child();
  }
  // Are there fewer specifier strings than child instructions?
  if (child != nullptr) {
    error_listener()->semanticError(
        file_names_[context_file_map_.at(inst->slot()->ctx())],
        semfunc_spec->start,
        absl::StrCat("Fewer semfunc specifiers than expected for opcode '",
                     inst->opcode()->name(), "'"));
  }
}

void InstructionSetVisitor::VisitResourceDetails(ResourceDetailsCtx* ctx,
                                                 Instruction* inst,
                                                 Slot* slot) {
  if (ctx->ident() != nullptr) {
    // This is a reference to a resource spec defined earlier.
    std::string name = ctx->ident()->getText();
    auto iter = slot->resource_spec_map().find(name);
    if (iter == slot->resource_spec_map().end()) {
      // This should never happen.
      error_listener()->semanticError(
          file_names_[context_file_map_.at(slot->ctx())], ctx->start,
          absl::StrCat("Internal error: Undefined resources name: '", name,
                       "'"));
      return;
    }
    ctx = iter->second;
  }
  ResourceSpec spec;
  VisitResourceDetailsLists(ctx, slot, inst, &spec);
  for (auto* use : spec.use_vec) {
    inst->AppendResourceUse(use);
  }
  for (auto* acquire : spec.acquire_vec) {
    inst->AppendResourceAcquire(acquire);
  }
}

std::optional<ResourceReference*>
InstructionSetVisitor::ProcessResourceReference(
    Slot* slot, Instruction* inst, ResourceItemCtx* resource_item) {
  // Empty optional object.
  std::optional<ResourceReference*> return_value;

  auto* factory = slot->instruction_set()->resource_factory();
  DestinationOperand* dest_op = nullptr;
  // Extract the text from the resource reference.
  std::string ident_text;
  bool is_array;
  if (resource_item->name != nullptr) {
    ident_text = resource_item->name->getText();
    is_array = false;
  } else {
    // Prepend the name with [] so that it doesn't conflict with a non-array
    // resource of the same name.
    ident_text = resource_item->array_name->getText();
    is_array = true;
  }
  dest_op = inst->GetDestOp(ident_text);
  auto* resource = factory->GetOrInsertResource(ident_text);
  resource->set_is_array(is_array);
  // Compute begin and end values.
  TemplateExpression* begin_expr;
  TemplateExpression* end_expr;
  DestinationOperand* tmp_op;
  if (resource_item->begin_cycle == nullptr) {
    begin_expr = new TemplateConstant(0);
  } else {
    context_file_map_.insert(
        {resource_item->begin_cycle, context_file_map_.at(slot->ctx())});
    tmp_op =
        FindDestinationOpInExpression(resource_item->begin_cycle, slot, inst);
    if (tmp_op != nullptr) {
      if (dest_op == nullptr) {
        dest_op = tmp_op;
      } else if (dest_op != tmp_op) {
        error_listener()->semanticError(
            file_names_[context_file_map_.at(slot->ctx())],
            resource_item->start,
            "Resource reference can only reference a single "
            "destination operand");
        return return_value;
      }
    }
    context_file_map_.insert(
        {resource_item->begin_cycle, context_file_map_.at(slot->ctx())});
    begin_expr = VisitExpression(resource_item->begin_cycle, slot, inst);
  }

  if (resource_item->end_cycle == nullptr) {
    // If there is no end_cycle specified, then it inherits from the dest_op
    // if available.
    // TODO(torerik): Handle case where the dest latency is '*'.
    if ((dest_op != nullptr) && (dest_op->expression() != nullptr)) {
      end_expr = dest_op->expression()->DeepCopy();
    } else {
      end_expr = new TemplateConstant(0);
    }
  } else {
    context_file_map_.insert(
        {resource_item->end_cycle, context_file_map_.at(slot->ctx())});
    tmp_op =
        FindDestinationOpInExpression(resource_item->end_cycle, slot, inst);
    if (tmp_op != nullptr) {
      if (dest_op == nullptr) {
        dest_op = tmp_op;
      } else if (dest_op != tmp_op) {
        error_listener()->semanticError(
            resource_item->start,
            "Resource reference can only reference a single "
            "destination operand");
        delete begin_expr;
        return return_value;
      }
    }
    context_file_map_.insert(
        {resource_item->end_cycle, context_file_map_.at(slot->ctx())});
    end_expr = VisitExpression(resource_item->end_cycle, slot, inst);
  }
  auto* ref =
      new ResourceReference(resource, is_array, dest_op, begin_expr, end_expr);
  return std::optional<ResourceReference*>(ref);
}

void InstructionSetVisitor::VisitResourceDetailsLists(ResourceDetailsCtx* ctx,
                                                      Slot* slot,
                                                      Instruction* inst,
                                                      ResourceSpec* spec) {
  if (ctx == nullptr) return;

  if ((ctx->use_list == nullptr) && (ctx->acquire_list == nullptr) &&
      (ctx->hold_list == nullptr))
    return;

  // The resource details consists of three lists: use, acquire, and
  // hold. The "use" list specifies the resources that have to only be
  // available at instruction dispatch time. The "acquire" list specifies the
  // list of resources that the instruction must be available to acquire, and
  // hold for a certain set of cycles. The hold list specifies the list of
  // resources that will be marked held (regardless of their status) for the
  // set of cycles specified.

  // Use list.
  auto* use_list = ctx->use_list;
  if (use_list != nullptr) {
    for (auto* resource_item : use_list->resource_item()) {
      auto ref_optional = ProcessResourceReference(slot, inst, resource_item);
      if (!ref_optional) continue;
      spec->use_vec.push_back(ref_optional.value());
    }
  }

  // Reserve list.
  auto* acquire_list = ctx->acquire_list;
  if (acquire_list != nullptr) {
    for (auto* resource_item : acquire_list->resource_item()) {
      auto ref_optional = ProcessResourceReference(slot, inst, resource_item);
      if (!ref_optional) continue;
      // Only add to use_vec if it isn't already there.
      bool found = false;
      for (auto* ref : spec->use_vec) {
        if (ref->resource->name() == ref_optional.value()->resource->name()) {
          found = true;
          break;
        }
      }
      if (!found) {
        spec->use_vec.push_back(new ResourceReference(*ref_optional.value()));
      }
      spec->acquire_vec.push_back(ref_optional.value());
    }
  }

  // Hold list.
  auto* hold_list = ctx->hold_list;
  if (hold_list != nullptr) {
    for (auto* resource_item : hold_list->resource_item()) {
      auto ref_optional = ProcessResourceReference(slot, inst, resource_item);
      if (!ref_optional) continue;
      spec->acquire_vec.push_back(ref_optional.value());
    }
  }
}

void InstructionSetVisitor::ProcessOpcodeList(
    OpcodeListCtx* ctx, Slot* slot, std::vector<Instruction*>& instruction_vec,
    absl::flat_hash_set<std::string>& deleted_ops_set,
    absl::flat_hash_set<OpcodeSpecCtx*>& overridden_ops_set) {
  // Obtain the list of opcodes specifications.
  auto opcode_spec = ctx->opcode_spec();
  for (auto* opcode_ctx : opcode_spec) {
    ProcessOpcodeSpec(opcode_ctx, slot, instruction_vec, deleted_ops_set,
                      overridden_ops_set);
  }
}

void InstructionSetVisitor::ProcessOpcodeSpec(
    OpcodeSpecCtx* opcode_ctx, Slot* slot,
    std::vector<Instruction*>& instruction_vec,
    absl::flat_hash_set<std::string>& deleted_ops_set,
    absl::flat_hash_set<OpcodeSpecCtx*>& overridden_ops_set) {
  auto* opcode_factory = slot->instruction_set()->opcode_factory();
  if (opcode_ctx->generate != nullptr) {
    auto status = ProcessOpcodeGenerator(opcode_ctx, slot, instruction_vec,
                                         deleted_ops_set, overridden_ops_set);
    if (!status.ok()) {
      error_listener()->semanticError(opcode_ctx->name, status.message());
    }
    return;
  }
  // Process the regular opcode specification.
  std::string opcode_name = opcode_ctx->name->getText();
  // Check to see if this opcode is deleted, meaning it should not be
  // inherited from a base slot.
  if (opcode_ctx->deleted != nullptr) {
    // If there is no base slot, this is an error.
    if (slot->base_slots().empty()) {
      error_listener()->semanticError(
          file_names_[context_file_map_.at(slot->ctx())], opcode_ctx->deleted,
          absl::StrCat("Invalid deleted opcode '", opcode_name, "', slot '",
                       slot->name(), "' does not inherit from a base slot"));
      return;
    }
    bool found = false;
    // Check to see if one of the base slots has this opcode.
    for (auto const& base_slot : slot->base_slots()) {
      found |= base_slot.base->HasInstruction(opcode_name);
      if (found) break;
    }
    // If the opcode was not defined in any of the base slots, it is an error.
    if (!found) {
      error_listener()->semanticError(
          file_names_[context_file_map_.at(slot->ctx())], opcode_ctx->deleted,
          absl::StrCat("Base slot does not define or inherit opcode '",
                       opcode_name, "'"));
      return;
    }
    deleted_ops_set.insert(opcode_name);
    return;
  }

  // Check to see if this opcode is overridden, this means that some of the
  // "attributes" (semantic function, disasm, etc.) are changed.
  if (opcode_ctx->overridden != nullptr) {
    int found = 0;
    for (auto const& base_slot : slot->base_slots()) {
      found += base_slot.base->HasInstruction(opcode_name);
    }
    // Check that the opcode is indeed inherited from one base class only.
    // Multiple inheritance is not supported.
    if (found == 0) {
      error_listener()->semanticError(
          file_names_[context_file_map_.at(slot->ctx())], opcode_ctx->deleted,
          absl::StrCat("Base slot does not define or inherit opcode '",
                       opcode_name, "'"));
      return;
    } else if (found > 1) {
      error_listener()->semanticError(
          file_names_[context_file_map_.at(slot->ctx())], opcode_ctx->deleted,
          absl::StrCat("Multiple inheritance of opcodes is not supported: ",
                       opcode_name));
      return;
    }
    overridden_ops_set.insert(opcode_ctx);
    return;
  }

  // This is a new opcode, so let's create it. Signal failure if error.
  absl::StatusOr<Opcode*> result = opcode_factory->CreateOpcode(opcode_name);
  if (!result.ok()) {
    error_listener()->semanticError(
        file_names_[context_file_map_.at(slot->ctx())], opcode_ctx->name,
        result.status().message());
    return;
  }

  Opcode* top = result.value();
  auto inst = new Instruction(top, slot);
  slot->instruction_set()->AddInstruction(inst);

  // Get the size of the instruction if specified, otherwise use default size.
  if (opcode_ctx->size_spec() != nullptr) {
    int size =
        std::stoi(opcode_ctx->size_spec()->NUMBER()->getText(), nullptr, 0);
    top->set_instruction_size(size);
  } else {
    top->set_instruction_size(slot->default_instruction_size());
  }
  if (top->instruction_size() < slot->min_instruction_size()) {
    slot->set_min_instruction_size(top->instruction_size());
  }

  int op_spec_number = 0;
  auto op_spec = opcode_ctx->operand_spec();
  // Process the top instruction.
  for (auto& [name, expr] : slot->attribute_map()) {
    inst->AddInstructionAttribute(name, expr->DeepCopy());
  }

  // Visit the opcode specification of the top instruction.
  if (op_spec->opcode_operands() != nullptr) {
    VisitOpcodeOperands(op_spec->opcode_operands(), op_spec_number, inst, inst,
                        slot);
  } else {
    VisitOpcodeOperands(op_spec->opcode_operands_list()->opcode_operands()[0],
                        op_spec_number, inst, inst, slot);
  }
  op_spec_number++;

  // If there are child instructions process them.
  if (opcode_ctx->operand_spec()->opcode_operands_list() != nullptr) {
    Opcode* parent = top;
    auto opcode_operands = op_spec->opcode_operands_list()->opcode_operands();

    Instruction* child_inst = nullptr;
    // Process child instructions.
    for (size_t i = 1; i < opcode_operands.size(); ++i) {
      // Create child opcode.
      auto* op = opcode_factory->CreateChildOpcode(parent);
      // Create child instruction.
      child_inst = new Instruction(op, slot);
      inst->AppendChild(child_inst);
      // Add default attributes.
      for (auto& [name, expr] : slot->attribute_map()) {
        child_inst->AddInstructionAttribute(name, expr->DeepCopy());
      }
      VisitOpcodeOperands(opcode_operands[i], op_spec_number, inst, child_inst,
                          slot);
      op_spec_number++;
    }
  }
  instruction_vec.push_back(inst);
  VisitOpcodeAttributes(opcode_ctx->opcode_attribute_list(), inst, slot);
}

void InstructionSetVisitor::VisitOpcodeOperands(OpcodeOperandsCtx* ctx,
                                                int op_spec_number,
                                                Instruction* parent,
                                                Instruction* child,
                                                Slot* slot) {
  if (ctx == nullptr) return;
  if (ctx->pred != nullptr) {
    std::string name = ctx->pred->getText();
    child->opcode()->set_predicate_op_name(name);
    parent->opcode()->op_locator_map().insert(std::make_pair(
        name, OperandLocator(op_spec_number, 'p', /*is_reloc=*/false,
                             /*instance=*/0)));
  }
  if (ctx->source != nullptr) {
    int instance = 0;
    for (auto* source_op : ctx->source->source_operand()) {
      std::string name;
      bool is_array = false;
      bool is_reloc = false;
      if (source_op->operand() != nullptr) {
        name = source_op->operand()->op_name->getText();
        if (source_op->operand()->op_attribute != nullptr) {
          auto attr = source_op->operand()->op_attribute->getText();
          if (attr == "%reloc") {
            is_reloc = true;
          } else {
            error_listener()->semanticError(
                file_names_[context_file_map_.at(slot->ctx())],
                source_op->operand()->op_attribute,
                absl::StrCat("Invalid operand attribute '", attr, "'"));
          }
        }
      } else {
        name = source_op->array_source->getText();
        is_array = true;
      }
      child->opcode()->AppendSourceOp(name, is_array, is_reloc);
      parent->opcode()->op_locator_map().insert(std::make_pair(
          name, OperandLocator(op_spec_number, is_array ? 't' : 's', is_reloc,
                               instance)));
      instance++;
    }
  }
  if (ctx->dest_list() != nullptr) {
    int instance = 0;
    for (auto* dest_op : ctx->dest_list()->dest_operand()) {
      std::string ident;
      bool is_array = false;
      bool is_reloc = false;
      if (dest_op->operand() != nullptr) {
        ident = dest_op->operand()->op_name->getText();
        if (dest_op->operand()->op_attribute != nullptr) {
          auto attr = dest_op->operand()->op_attribute->getText();
          if (attr == "%reloc") {
            is_reloc = true;
          } else {
            error_listener()->semanticError(
                file_names_[context_file_map_.at(slot->ctx())],
                dest_op->operand()->op_attribute,
                absl::StrCat("Invalid operand attribute '", attr, "'"));
          }
        }
      } else {
        ident = dest_op->array_dest->getText();
        is_array = true;
      }
      // The latency of the destination operand is either specified by an
      // expression, by '*' (wildcard), or omitted, in which case it
      // defaults to 1.
      if (dest_op->expression() != nullptr) {
        context_file_map_.insert(
            {dest_op->expression(), context_file_map_.at(slot->ctx())});
        child->opcode()->AppendDestOp(
            ident, is_array, is_reloc,
            VisitExpression(dest_op->expression(), slot, child));
      } else if (dest_op->wildcard != nullptr) {
        child->opcode()->AppendDestOp(ident, is_array, is_reloc);
      } else if (slot->default_latency() != nullptr) {
        child->opcode()->AppendDestOp(ident, is_array, is_reloc,
                                      slot->default_latency()->DeepCopy());
      } else {
        child->opcode()->AppendDestOp(ident, is_array, is_reloc,
                                      new TemplateConstant(1));
      }
      parent->opcode()->op_locator_map().insert(std::make_pair(
          ident, OperandLocator(op_spec_number, is_array ? 'e' : 'd', is_reloc,
                                instance)));
      instance++;
    }
  }
}

absl::Status InstructionSetVisitor::ProcessOpcodeGenerator(
    OpcodeSpecCtx* ctx, Slot* slot, std::vector<Instruction*>& instruction_vec,
    absl::flat_hash_set<std::string>& deleted_ops_set,
    absl::flat_hash_set<OpcodeSpecCtx*>& overridden_ops_set) {
  if (ctx == nullptr) return absl::InternalError("OpcodeSpecCtx is null");
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
            file_names_[context_file_map_.at(slot->ctx())], assign_ctx->start,
            absl::StrCat("Duplicate binding variable name '", name, "'"));
        continue;
      }
      range_variable_names.insert(name);
      range_info->range_names.push_back(ident_ctx->getText());
      range_info->range_values.push_back({});
      range_info->range_regexes.emplace_back(
          absl::StrCat("\\$\\(", name, "\\)"));
      // Verify that the range variable is used in the string.
      if (!RE2::PartialMatch(ctx->generator_opcode_spec_list()->getText(),
                             range_info->range_regexes.back())) {
        error_listener()->semanticWarning(
            file_names_[context_file_map_.at(slot->ctx())], assign_ctx->start,
            absl::StrCat("Unreferenced binding variable '", name, "'"));
      }
    }
    // See if it's a list of simple values.
    if (!assign_ctx->gen_value().empty()) {
      for (auto* gen_value_ctx : assign_ctx->gen_value()) {
        if (gen_value_ctx->simple != nullptr) {
          range_info->range_values[0].push_back(
              gen_value_ctx->simple->getText());
        } else {
          // Strip off double quotes.
          std::string value = gen_value_ctx->string->getText();
          range_info->range_values[0].push_back(
              value.substr(1, value.size() - 2));
        }
      }
      continue;
    }
    // It's a list of tuples with a structured binding assignment.
    for (auto* tuple_ctx : assign_ctx->tuple()) {
      if (tuple_ctx->gen_value().size() != range_info->range_names.size()) {
        for (auto* info : range_info_vec) delete info;
        return absl::InternalError(
            "Number of values differs from number of identifiers");
      }
      for (int i = 0; i < tuple_ctx->gen_value().size(); ++i) {
        if (tuple_ctx->gen_value(i)->simple != nullptr) {
          range_info->range_values[i].push_back(
              tuple_ctx->gen_value(i)->simple->getText());
        } else {
          // Strip off double quotes.
          std::string value = tuple_ctx->gen_value(i)->string->getText();
          range_info->range_values[i].push_back(
              value.substr(1, value.size() - 2));
        }
      }
    }
  }
  // Check that all binding variable references are valid.
  std::string input_text = ctx->generator_opcode_spec_list()->getText();
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
          file_names_[context_file_map_.at(slot->ctx())],
          ctx->generator_opcode_spec_list()->start,
          absl::StrCat("Undefined binding variable '", ident, "'"));
    }
    start_pos = end_pos;
    pos = input_text.find_first_of('$', start_pos);
  }
  if (error_listener()->HasError()) {
    for (auto* info : range_info_vec) delete info;
    return absl::InternalError("Found undefined binding variable name(s)");
  }
  // Now we need to iterate over the range_info instances and substitution
  // ranges. This will produce new text that will be parsed and processed.
  std::string generated_text =
      GenerateOpcodeSpec(range_info_vec, 0, input_text);
  // Parse and process the generated text.
  auto* parser = new IsaAntlrParserWrapper(generated_text);
  antlr_parser_wrappers_.push_back(parser);
  // Parse the text starting at the opcode_spec_list rule.
  auto opcode_spec_vec = parser->parser()->opcode_spec_list()->opcode_spec();
  // Process the opcode spec.
  for (auto* opcode_spec : opcode_spec_vec) {
    ProcessOpcodeSpec(opcode_spec, slot, instruction_vec, deleted_ops_set,
                      overridden_ops_set);
  }
  // Clean up.
  for (auto* info : range_info_vec) delete info;
  return absl::OkStatus();
}

// Helper function to recursively generate the text for the GENERATE opcode
// spec.
std::string InstructionSetVisitor::GenerateOpcodeSpec(
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
      absl::StrAppend(&generated, GenerateOpcodeSpec(range_info_vec, index + 1,
                                                     template_str));
    } else {
      absl::StrAppend(&generated, template_str);
    }
    // If there were no replacements, then the range variables weren't used,
    // and the template string won't change for any other values in the range.
    // This can happen if the range variables aren't referenced in the string.
    // Thus, break out of the loop.
    if (replace_count == 0) break;
  }
  return generated;
}

static absl::StatusOr<std::pair<std::string, std::string::size_type>> get_ident(
    std::string str, std::string::size_type pos) {
  // If the next character is not an alpha or '_' it's an error.
  if (!std::isalpha(str[pos]) && (str[pos] != '_')) {
    return absl::InternalError(
        absl::StrCat("Invalid character in operand name at position ", pos,
                     " in '", str, "'"));
  }
  std::string op_name;
  while (std::isalnum(str[pos]) || (str[pos] == '_')) {
    op_name.push_back(str[pos]);
    pos++;
    if (pos >= str.size()) {
      pos = std::string::npos;
      break;
    }
  }
  return std::make_pair(op_name, pos);
}
// This method parses the disasm format string.
absl::Status InstructionSetVisitor::ParseDisasmFormat(std::string format,
                                                      Instruction* inst) {
  std::string::size_type pos = 0;
  std::string::size_type prev = 0;
  std::string::size_type length = format.size();
  FormatInfo* format_info = nullptr;
  // Extract raw text without (between) the '%' specifiers.
  DisasmFormat* disasm_fmt = new DisasmFormat();
  while ((pos != std::string::npos) &&
         ((pos = format.find_first_of('%', pos)) != std::string::npos)) {
    std::string text = format.substr(prev, pos - prev);
    std::string new_text;
    for (auto& c : text) {
      if (c == '\\') continue;
      new_text.push_back(c);
    }
    disasm_fmt->format_fragment_vec.push_back(new_text);
    pos++;
    if (pos >= length) break;

    // See if it is a simple %opname specifier or an expression.
    if (format[pos] == '(') {  // This is an expression.
      pos++;
      if (pos >= length) break;

      // Find end of the expression.
      std::string::size_type end_pos = pos;
      int paren_count = 0;
      while (true) {
        if (end_pos >= length) break;
        if (format[end_pos] == ':') break;
        if (format[end_pos] == '(') paren_count++;
        if (format[end_pos] == ')') {
          if (paren_count == 0) break;
          paren_count--;
        }
        end_pos++;
      }

      if (end_pos >= length) break;

      auto res = ParseFormatExpression(format.substr(pos, end_pos - pos),
                                       inst->opcode());
      if (!res.ok()) {
        delete disasm_fmt;
        return res.status();
      }

      format_info = res.value();

      pos = end_pos;

      format_info->number_format = "%d";  // Default number format.

      if (format[pos] == ':') {
        pos++;
        if (pos >= length) break;

        end_pos = format.find_first_of(')', pos);
        if (end_pos == std::string::npos) break;
        auto res = ParseNumberFormat(format.substr(pos, end_pos - pos));
        if (!res.ok()) {
          delete format_info;
          delete disasm_fmt;
          return res.status();
        }
        format_info->number_format = res.value();
        pos = end_pos;
      }
      pos++;
      if (pos >= format.size()) {
        pos = std::string::npos;
      } else if (format[pos] == '?') {
        format_info->is_optional = true;
        pos++;
        if (pos >= format.size()) {
          pos = std::string::npos;
        }
      }
      format_info->is_formatted = true;
      disasm_fmt->format_info_vec.push_back(format_info);

    } else {  // Simple %opname specifier.
      auto res = get_ident(format, pos);
      if (!res.ok()) {
        delete disasm_fmt;
        return res.status();
      }
      auto [op_name, end_pos] = res.value();
      pos = end_pos;
      if (!inst->opcode()->op_locator_map().contains(op_name)) {
        delete disasm_fmt;
        return absl::InternalError(absl::StrCat(
            "Invalid operand '", op_name, "' used in format '", format, "'"));
      }
      auto* format_info = new FormatInfo();
      format_info->op_name = op_name;
      format_info->is_formatted = false;
      if ((pos != std::string::npos) && (format[pos] == '?')) {
        format_info->is_optional = true;
        disasm_fmt->num_optional++;
        pos++;
        if (pos >= format.size()) {
          pos = std::string::npos;
        }
      }
      disasm_fmt->format_info_vec.push_back(format_info);
    }

    prev = pos;
  }
  if (pos != std::string::npos) {
    delete format_info;
    delete disasm_fmt;
    return absl::InternalError(
        absl::StrCat("Unexpected end of format string in '", format, "'"));
  }
  if (prev != std::string::npos) {
    std::string text = format.substr(prev);
    std::string new_text;
    for (auto& c : text) {
      if (c == '\\') continue;
      new_text.push_back(c);
    }
    disasm_fmt->format_fragment_vec.push_back(new_text);
  }
  std::string str;
  for (auto& s : disasm_fmt->format_fragment_vec) {
    absl::StrAppend(&str, s, ":");
  }
  int width = 0;
  auto count = inst->disasm_format_vec().size();
  if (count < disasm_field_widths_.size()) {
    auto result = disasm_field_widths_[count]->GetValue();
    if (result.ok()) {
      auto* value_ptr = std::get_if<int>(&result.value());
      width = *value_ptr;
    }
  }
  disasm_fmt->width = width;
  inst->AppendDisasmFormat(disasm_fmt);
  return absl::OkStatus();
}

static std::string::size_type skip_space(const std::string& str,
                                         std::string::size_type pos) {
  if (pos == std::string::npos) return pos;
  if (pos >= str.size()) {
    return std::string::npos;
  }
  while ((str[pos] == ' ') || (str[pos] == '\t')) {
    pos++;
    if (pos >= str.size()) return std::string::npos;
  }
  return pos;
}

absl::StatusOr<FormatInfo*> InstructionSetVisitor::ParseFormatExpression(
    std::string expr, Opcode* op) {
  // The format expression is very simple. It is of the form:
  // [@+/-] ident | '(' ident <</>> number ')'
  // where @ signifies the current instruction address.
  // In short, the value of the field can be shifted left or right, then added
  // to, or subtracted from, the instruction address.

  FormatInfo* format_info = new FormatInfo();

  std::string::size_type pos = 0;
  pos = skip_space(expr, pos);
  if (pos == std::string::npos) {
    delete format_info;
    return absl::InternalError("Empty format expression");
  }

  if (expr[pos] == '@') {
    pos++;
    format_info->use_address = true;
    pos = skip_space(expr, pos);
    if (pos == std::string::npos) {
      return format_info;
    }

    if (expr[pos] == '-') {
      format_info->operation = "-";
    } else if (expr[pos] == '+') {
      format_info->operation = "+";
    } else {
      delete format_info;
      return absl::InternalError(
          absl::StrCat("@ must be followed by a '+' or a '-' in '", expr, "'"));
    }
    pos++;
  }

  pos = skip_space(expr, pos);
  if (pos == std::string::npos) {
    delete format_info;
    return absl::InternalError(
        absl::StrCat("Malformed expression '", expr, "'"));
  }

  if (expr[pos] != '(') {  // No shift expression.
    // Get the field identifier.
    auto res = get_ident(expr, pos);
    if (!res.ok()) {
      delete format_info;
      return res.status();
    }
    auto [ident, new_pos] = res.value();
    if (!op->op_locator_map().contains(ident)) {
      delete format_info;
      return absl::InternalError(absl::StrCat("Invalid operand '", ident,
                                              "' used in format for opcode'",
                                              op->name(), "'"));
    }
    format_info->op_name = ident;
    // Verify that there are no more characters in the expression.
    pos = skip_space(expr, new_pos);
    if (pos != std::string::npos) {
      delete format_info;
      return absl::InternalError(
          absl::StrCat("Malformed expression '", expr, "'"));
    }
  } else {  // expr[pos] == '('
    pos++;
    pos = skip_space(expr, pos);
    // The input expression has balanced parens, so we don't have to check for
    // end of string.

    // Get the field identifier.
    auto res = get_ident(expr, pos);
    if (!res.ok()) {
      delete format_info;
      return res.status();
    }
    auto [ident, new_pos] = res.value();
    format_info->op_name = ident;

    pos = skip_space(expr, new_pos);

    // Get the shift direction.
    if (expr.substr(pos, 2) == "<<") {
      format_info->do_left_shift = true;
    } else if (expr.substr(pos, 2) != ">>") {
      delete format_info;
      return absl::InternalError(
          absl::StrCat("Missing shift in expression '", expr, "'"));
    }
    pos += 2;

    // Get the shift amount.
    pos = skip_space(expr, pos);

    std::string num;
    while (std::isdigit(expr[pos])) {
      num.push_back(expr[pos]);
      pos++;
    }
    if (num.empty()) {
      delete format_info;
      return absl::InternalError(
          absl::StrCat("Malformed expression - no shift amount '", expr, "'"));
    }
    format_info->shift_amount = std::stoi(num);

    // Verify close paren, and that there aren't any other characters after
    // that.
    pos = skip_space(expr, pos);
    if (expr[pos] != ')') {
      delete format_info;
      return absl::InternalError(
          absl::StrCat("Malformed expression - expected ')' '", expr, "'"));
    }
    pos++;
    pos = skip_space(expr, pos);
    if (pos != std::string::npos) {
      delete format_info;
      return absl::InternalError(absl::StrCat(
          "Malformed expression - extra characters after ')' '", expr, "'"));
    }
  }
  return format_info;
}

absl::StatusOr<std::string> InstructionSetVisitor::ParseNumberFormat(
    std::string format) {
  std::string::size_type pos = 0;
  std::string format_string = "%";
  bool leading_zero = false;
  if (format[pos] == '0') {
    leading_zero = true;
    format_string.push_back('0');
    pos++;
  }
  // If there's a leading zero, there has to be a width. Signal error
  // otherwise.
  if (leading_zero && !std::isdigit(format[pos])) {
    return absl::InternalError(
        absl::StrCat("Format width required when a leading 0 is specified - '",
                     format.substr(0, pos + 1), "'"));
  }
  // Read the format width. It's an error if it's three digits, otherwise,
  // just roll with it.
  if (std::isdigit(format[pos])) {
    std::string number = format.substr(pos++, 1);
    if (std::isdigit(format[pos])) {
      number.push_back(format[pos++]);
      if (std::isdigit(format[pos])) {
        return absl::InternalError(
            absl::StrCat("Format width > than 3 digits not allowed '",
                         format.substr(0, pos + 1), "'"));
      }
    }
    format_string.append(number);
  }
  // Read the number base.
  if ((format[pos] != 'o') && (format[pos] != 'd') && (format[pos] != 'x') &&
      (format[pos] != 'X')) {
    return absl::InternalError(
        absl::StrCat("Illegal format specifier '", std::string(1, format[pos]),
                     "' in '", format.substr(0, pos + 1), "'"));
  }
  format_string.push_back(format[pos++]);
  if (pos < format.size()) {
    return absl::InternalError(
        absl::StrCat("Too many characters in format specifier '", format, "'"));
  }
  return format_string;
}

// The following methods are used to generate the prologs and epilogs in the
// emitted files.
std::string InstructionSetVisitor::GenerateHdrFileProlog(
    absl::string_view file_name, absl::string_view opcode_file_name,
    absl::string_view guard_name, absl::string_view encoding_base_name,
    const std::vector<std::string>& namespaces) {
  std::string output;
  absl::StrAppend(&output, "#ifndef ", guard_name,
                  "\n"
                  "#define ",
                  guard_name,
                  "\n"
                  "\n"
                  "#include <functional>\n"
                  "#include <map>\n"
                  "#include <vector>\n"
                  "\n"
                  "#include \"mpact/sim/generic/arch_state.h\"\n"
                  "#include "
                  "\"mpact/sim/generic/instruction.h\"\n"
                  "#include \"",
                  opcode_file_name,
                  "\"\n"
                  "\n");

  for (const auto& namespace_name : namespaces) {
    absl::StrAppend(&output, "namespace ", namespace_name, " {\n");
  }
  absl::StrAppend(
      &output,
      "\n"
      "using ::mpact::sim::generic::Instruction;\n"
      "using SemFunc = "
      "::mpact::sim::generic::Instruction::SemanticFunction;\n"
      "using ::mpact::sim::generic::ArchState;\n"
      "using ::mpact::sim::generic::PredicateOperandInterface;\n"
      "using ::mpact::sim::generic::SourceOperandInterface;\n"
      "using ::mpact::sim::generic::DestinationOperandInterface;\n"
      "using ::mpact::sim::generic::ResourceOperandInterface;\n"
      "using SimpleResourceVector = std::vector<SimpleResourceEnum>;\n"
      "\n");
  // Emit encoding base class.
  absl::StrAppend(&output, "class ", encoding_base_name, " {\n public:\n");
  absl::StrAppend(&output, "  virtual ~", encoding_base_name,
                  "() = default;\n\n");
  // Get opcode method.
  absl::StrAppend(
      &output,
      "  virtual OpcodeEnum GetOpcode(SlotEnum slot, int entry) = 0;\n");
  std::string optional_instruction;
  if (generator_version_ == 2) {
    optional_instruction = "Instruction *inst, ";
  }
  // Get resource methods.
  absl::StrAppend(
      &output, "  virtual ResourceOperandInterface *GetSimpleResourceOperand",
      "(", optional_instruction,
      "SlotEnum slot, int entry, OpcodeEnum opcode, SimpleResourceVector "
      "&resource_vec, int end) { return nullptr;}\n");
  absl::StrAppend(
      &output,
      "  virtual ResourceOperandInterface * "
      "GetComplexResourceOperand",
      "(", optional_instruction,
      "SlotEnum slot, int entry, OpcodeEnum opcode, ComplexResourceEnum "
      "resource_op, int begin, int end) { return nullptr; }\n");
  absl::StrAppend(
      &output,
      "  virtual std::vector<ResourceOperandInterface *> "
      "GetComplexResourceOperands",
      "(", optional_instruction,
      "SlotEnum slot, int entry, OpcodeEnum opcode, ComplexResourceEnum "
      "resource_op, int begin, int end) { return {}; }\n");
  // For each operand type, declare the pure virtual method that returns the
  // given operand.
  absl::StrAppend(&output,
                  "  virtual PredicateOperandInterface *GetPredicate"
                  "(",
                  optional_instruction,
                  "SlotEnum slot, int entry, OpcodeEnum opcode, PredOpEnum "
                  "pred_op) { return nullptr; }\n");
  absl::StrAppend(&output,
                  "  virtual SourceOperandInterface *GetSource"
                  "(",
                  optional_instruction,
                  "SlotEnum slot, int entry, OpcodeEnum opcode, SourceOpEnum "
                  "source_op, int source_no) { return nullptr;}\n");
  absl::StrAppend(
      &output,
      "  virtual std::vector<SourceOperandInterface *> GetSources"
      "(",
      optional_instruction,
      "SlotEnum slot, int entry, OpcodeEnum opcode, ListSourceOpEnum "
      "list_source_op, int source_no) { return {};}\n");
  absl::StrAppend(&output,
                  "  virtual DestinationOperandInterface *GetDestination"
                  "(",
                  optional_instruction,
                  "SlotEnum slot, int entry, OpcodeEnum opcode, "
                  "DestOpEnum list_dest_op, int dest_no, int latency)"
                  " { return nullptr; }\n");
  absl::StrAppend(
      &output,
      "  virtual std::vector<DestinationOperandInterface *> GetDestinations"
      "(",
      optional_instruction,
      "SlotEnum slot, int entry, OpcodeEnum opcode, "
      "ListDestOpEnum dest_op, int dest_no, const std::vector<int> &latency)"
      " { return {}; };\n");
  // Destination operand latency getter for destination operands with '*'
  // as latency.
  absl::StrAppend(
      &output, "  virtual int GetLatency(", optional_instruction,
      "SlotEnum slot, int entry, OpcodeEnum "
      "opcode, DestOpEnum dest_op, int dest_no) { return 0; };\n",
      "  virtual std::vector<int> GetLatency(SlotEnum slot, int entry, "
      "OpcodeEnum "
      "opcode, ListDestOpEnum dest_op, int dest_no) { return {0}; }\n");

  absl::StrAppend(&output, "};\n\n");
  absl::StrAppend(&output,
                  "using OperandSetter = "
                  "std::vector<void (*)(Instruction *, ",
                  encoding_base_name,
                  "*, OpcodeEnum, SlotEnum, int)>;\n"
                  "using DisassemblySetter = void(*)(Instruction *);\n"
                  "using ResourceSetter = void(*)(Instruction *, ",
                  encoding_base_name,
                  "*, SlotEnum, int);\n"
                  "using SemFuncSetter = std::vector<SemFunc>;\n"
                  "using AttributeSetter = void(*)(Instruction *);\n"
                  "struct InstructionInfo {\n"
                  "  OperandSetter operand_setter;\n"
                  "  DisassemblySetter disassembly_setter;\n"
                  "  ResourceSetter resource_setter;\n"
                  "  AttributeSetter attribute_setter;\n"
                  "  SemFuncSetter semfunc;\n"
                  "  int instruction_size;\n"
                  "};\n"
                  "\n");
  return output;
}

std::tuple<std::string, std::string>
InstructionSetVisitor::GenerateEncFilePrologs(
    absl::string_view file_name, absl::string_view guard_name,
    absl::string_view opcode_file_name, absl::string_view encoding_type_name,
    const std::vector<std::string>& namespaces) {
  std::string h_output;
  std::string cc_output;
  absl::StrAppend(&h_output, "#ifndef ", guard_name,
                  "\n"
                  "#define ",
                  guard_name,
                  "\n"
                  "\n"
                  "#include <array>\n"
                  "#include <string>\n"
                  "#include <vector>\n"
                  "\n"
                  "#include \"absl/container/flat_hash_map.h\"\n"
                  "#include \"absl/status/status.h\"\n"
                  "#include \"absl/status/statusor.h\"\n"
                  "#include \"absl/strings/string_view.h\"\n"
                  "#include "
                  "\"mpact/sim/util/asm/opcode_assembler_interface.h\"\n"
                  "#include \"mpact/sim/util/asm/resolver_interface.h\"\n"
                  "#include \"re2/re2.h\"\n"
                  "#include \"re2/set.h\"\n"
                  "#include \"",
                  opcode_file_name,
                  "\"\n"
                  "\n");
  absl::StrAppend(&cc_output, "#include \"", file_name,
                  "\"\n"
                  "\n"
                  "#include <array>\n"
                  "#include <string>\n"
                  "#include <vector>\n"
                  "\n"
                  "#include \"absl/status/status.h\"\n"
                  "#include \"absl/status/statusor.h\"\n"
                  "#include \"absl/strings/str_cat.h\"\n"
                  "#include \"absl/strings/string_view.h\"\n"
                  "#include "
                  "\"mpact/sim/util/asm/opcode_assembler_interface.h\"\n"
                  "#include \"mpact/sim/util/asm/resolver_interface.h\"\n"
                  "#include \"re2/re2.h\"\n"
                  "#include \"re2/set.h\"\n"
                  "#include \"",
                  opcode_file_name,
                  "\"\n"
                  "\n");

  for (const auto& namespace_name : namespaces) {
    absl::StrAppend(&h_output, "namespace ", namespace_name, " {\n");
    absl::StrAppend(&cc_output, "namespace ", namespace_name, " {\n");
  }
  absl::StrAppend(&h_output, "\n");
  absl::StrAppend(&cc_output, "\n");
  return {h_output, cc_output};
}

std::string InstructionSetVisitor::GenerateHdrFileEpilog(
    absl::string_view guard_name, const std::vector<std::string>& namespaces) {
  std::string output;
  absl::StrAppend(&output, GenerateNamespaceEpilog(namespaces));
  absl::StrAppend(&output, "\n#endif  // ", guard_name, "\n");
  return output;
}

std::string InstructionSetVisitor::GenerateCcFileProlog(
    absl::string_view hdr_file_name, bool use_includes,
    const std::vector<std::string>& namespaces) {
  std::string output;
  // Include files.
  absl::StrAppend(&output, "#include \"", hdr_file_name, "\"\n");
  absl::StrAppend(&output,
                  "\n#include <array>\n\n"
                  "#include \"absl/strings/str_format.h\"\n\n");
  if (use_includes) {
    for (auto& include_file : include_files_) {
      absl::StrAppend(&output, "#include ", include_file, "\n");
    }
  }
  absl::StrAppend(&output, "\n");
  // Namespaces.
  for (const auto& namespace_name : namespaces) {
    absl::StrAppend(&output, "namespace ", namespace_name, " {\n");
  }
  absl::StrAppend(&output, "\n");
  return output;
}

std::string InstructionSetVisitor::GenerateSimpleHdrProlog(
    absl::string_view guard_name, const std::vector<std::string>& namespaces) {
  std::string output;
  absl::StrAppend(&output, "#ifndef ", guard_name, "\n#define ", guard_name,
                  "\n\n");

  for (const auto& namespace_name : namespaces) {
    absl::StrAppend(&output, "namespace ", namespace_name, " {\n");
  }
  absl::StrAppend(&output, "\n");
  return output;
}

std::string InstructionSetVisitor::GenerateNamespaceEpilog(
    const std::vector<std::string>& namespaces) {
  std::string output;
  // Close up namespaces.
  absl::StrAppend(&output, "\n");
  for (auto namespace_name = namespaces.crbegin();
       namespace_name != namespaces.crend(); ++namespace_name) {
    absl::StrAppend(&output, "}  // namespace ", *namespace_name, "\n");
  }
  return output;
}

absl::Status InstructionSetVisitor::AddConstant(absl::string_view name,
                                                absl::string_view,
                                                TemplateExpression* expr) {
  if (constant_map_.contains(name)) {
    return absl::AlreadyExistsError(
        absl::StrCat("Constant redefinition of '", name, "'"));
  }
  constant_map_.emplace(name, expr);
  return absl::OkStatus();
}

TemplateExpression* InstructionSetVisitor::GetConstExpression(
    absl::string_view name) const {
  auto iter = constant_map_.find(name);
  if (iter == constant_map_.end()) return nullptr;
  return iter->second;
}

}  // namespace instruction_set
}  // namespace machine_description
}  // namespace sim
}  // namespace mpact
