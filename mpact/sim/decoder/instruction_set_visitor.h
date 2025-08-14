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

#ifndef MPACT_SIM_DECODER_INSTRUCTION_SET_VISITOR_H_
#define MPACT_SIM_DECODER_INSTRUCTION_SET_VISITOR_H_

#include <cstddef>
#include <deque>
#include <list>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "absl/container/btree_set.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "antlr4-runtime/ParserRuleContext.h"
#include "mpact/sim/decoder/InstructionSetLexer.h"
#include "mpact/sim/decoder/InstructionSetParser.h"
#include "mpact/sim/decoder/antlr_parser_wrapper.h"
#include "mpact/sim/decoder/bundle.h"
#include "mpact/sim/decoder/decoder_error_listener.h"
#include "mpact/sim/decoder/instruction.h"
#include "mpact/sim/decoder/instruction_set.h"
#include "mpact/sim/decoder/instruction_set_contexts.h"
#include "mpact/sim/decoder/opcode.h"
#include "mpact/sim/decoder/slot.h"
#include "mpact/sim/decoder/template_expression.h"
#include "re2/re2.h"

// This file declares the classes that interact with the antlr4 library
// to parse an input stream and generate the parse tree, then visiting the
// parse tree to build up the internal representation from which further
// processing and eventual c++ code generation is done.
namespace mpact {
namespace sim {
namespace machine_description {
namespace instruction_set {

// This struct holds information about a range assignment in an instruction
// generator.
struct RangeAssignmentInfo {
  std::vector<std::string> range_names;
  std::list<RE2> range_regexes;
  std::vector<std::vector<std::string>> range_values;
};

using InstructionSetParser = ::sim::machine_description::instruction_set::
    generated::InstructionSetParser;
using ::sim::machine_description::instruction_set::generated::
    InstructionSetLexer;

using IsaAntlrParserWrapper =
    decoder::AntlrParserWrapper<InstructionSetParser, InstructionSetLexer>;

// Visitor class for the Antlr4 parse tree. It does not derive from Antlr4
// base classes so that the return types and method parameters could be
// customized.
class InstructionSetVisitor {
 public:
  InstructionSetVisitor();
  ~InstructionSetVisitor();

  // Entry point for processing a source_stream input, generating any output
  // files in the given directory. Returns OK if no errors were encountered.
  absl::Status Process(const std::vector<std::string>& file_names,
                       const std::string& prefix, const std::string& isa_name,
                       const std::vector<std::string>& include_roots,
                       absl::string_view directory);

  // Global const expressions.
  absl::Status AddConstant(absl::string_view name, absl::string_view type,
                           TemplateExpression* expr);
  TemplateExpression* GetConstExpression(absl::string_view name) const;
  // The current isa name.
  const std::string& isa_name() const { return isa_name_; }

 private:
  struct TemplateFunctionEvaluator {
    TemplateFunction::Evaluator function;
    size_t arity;
    TemplateFunctionEvaluator(TemplateFunction::Evaluator function_,
                              size_t arity_)
        : function(std::move(function_)), arity(arity_) {}
  };

  // Checks that any references to slots or bundles within a bundle
  // declaration are to valid slots/bundles.
  void PerformBundleReferenceChecks(InstructionSet* instruction_set,
                                    Bundle* bundle);
  // The following methods visits the parts of the parse tree indicated by
  // the method name and builds up the internal representation used for
  // decoder generation.
  void VisitTopLevel(TopLevelCtx* ctx);

  std::unique_ptr<InstructionSet> VisitIsaDeclaration(IsaDeclCtx* ctx);
  void VisitConstantDef(ConstantDefCtx* ctx);
  void VisitBundleList(BundleListCtx* ctx, Bundle* bundle);
  void VisitOpcodeList(OpcodeListCtx* ctx, Slot* slot);
  void VisitOpcodeOperands(OpcodeOperandsCtx* ctx, int op_spec_number,
                           Instruction* parent, Instruction* child, Slot* slot);
  void VisitSlotList(SlotListCtx* ctx, Bundle*);
  void VisitIncludeFile(IncludeFileCtx* ctx);
  void VisitSlotDeclaration(SlotDeclCtx* ctx, InstructionSet* instruction_set);
  void VisitConstAndDefaultDecls(ConstAndDefaultCtx* ctx, Slot* slot);
  TemplateExpression* VisitExpression(ExpressionCtx* ctx, Slot* slot,
                                      Instruction* inst);
  std::vector<int> VisitArraySpec(ArraySpecCtx* ctx);
  void VisitNamespaceDecl(NamespaceDeclCtx* ctx, InstructionSet* isa);
  void VisitBundleDeclaration(BundleDeclCtx* ctx,
                              InstructionSet* instruction_set);
  void VisitDisasmWidthsDecl(DisasmWidthsCtx* ctx);
  void VisitInstructionAttributeList(InstructionAttributeListCtx* ctx,
                                     Slot* slot, Instruction* inst);
  void VisitOpcodeAttributes(OpcodeAttributeListCtx* ctx, Instruction* inst,
                             Slot* slot);
  void VisitSemfuncSpec(SemfuncSpecCtx* semfunc_spec, Instruction* inst);
  void VisitResourceDetails(ResourceDetailsCtx* ctx, Instruction* inst,
                            Slot* slot);
  std::optional<ResourceReference*> ProcessResourceReference(
      Slot* slot, Instruction* inst, ResourceItemCtx* resource_item);
  void VisitResourceDetailsLists(ResourceDetailsCtx* ctx, Slot* slot,
                                 Instruction* inst, ResourceSpec* spec);
  std::unique_ptr<InstructionSet> ProcessTopLevel(absl::string_view isa_name);
  void ParseIncludeFile(antlr4::ParserRuleContext* ctx,
                        const std::string& file_name,
                        const std::vector<std::string>& dirs);
  DestinationOperand* FindDestinationOpInExpression(ExpressionCtx* ctx,
                                                    const Slot* slot,
                                                    const Instruction* inst);
  void PerformOpcodeOverrides(
      absl::flat_hash_set<OpcodeSpecCtx*> overridden_ops_set, Slot* slot);
  void PreProcessDeclarations(const std::vector<DeclarationCtx*>& ctx_vec);
  void ProcessOpcodeList(
      OpcodeListCtx* ctx, Slot* slot,
      std::vector<Instruction*>& instruction_vec,
      absl::flat_hash_set<std::string>& deleted_ops_set,
      absl::flat_hash_set<OpcodeSpecCtx*>& overridden_ops_set);
  void ProcessOpcodeSpec(
      OpcodeSpecCtx* opcode_ctx, Slot* slot,
      std::vector<Instruction*>& instruction_vec,
      absl::flat_hash_set<std::string>& deleted_ops_set,
      absl::flat_hash_set<OpcodeSpecCtx*>& overridden_ops_set);
  // Process the GENERATE() directive.
  absl::Status ProcessOpcodeGenerator(
      OpcodeSpecCtx* ctx, Slot* slot,
      std::vector<Instruction*>& instruction_vec,
      absl::flat_hash_set<std::string>& deleted_ops_set,
      absl::flat_hash_set<OpcodeSpecCtx*>& overridden_ops_set);
  // Helper function fused by ProcessOpcodeGenerator.
  std::string GenerateOpcodeSpec(
      const std::vector<RangeAssignmentInfo*>& range_info_vec, int index,
      const std::string& template_str_in) const;
  // These methods parses the disassembly format string.
  absl::Status ParseDisasmFormat(std::string format, Instruction* inst);
  absl::StatusOr<std::string> ParseNumberFormat(std::string format);
  absl::StatusOr<FormatInfo*> ParseFormatExpression(std::string expr,
                                                    Opcode* op);

  // Generate file prologues/epilogues.
  std::string GenerateHdrFileProlog(absl::string_view file_name,
                                    absl::string_view opcode_file_name,
                                    absl::string_view guard_name,
                                    absl::string_view encoding_base_name,
                                    const std::vector<std::string>& namespaces);
  std::tuple<std::string, std::string> GenerateEncFilePrologs(
      absl::string_view file_name, absl::string_view guard_name,
      absl::string_view opcode_file_name, absl::string_view encoding_type_name,
      const std::vector<std::string>& namespaces);
  std::string GenerateHdrFileEpilog(absl::string_view guard_name,
                                    const std::vector<std::string>& namespaces);
  std::string GenerateCcFileProlog(absl::string_view hdr_file_name,
                                   bool use_includes,
                                   const std::vector<std::string>& namespaces);
  std::string GenerateNamespaceEpilog(
      const std::vector<std::string>& namespaces);
  std::string GenerateSimpleHdrProlog(
      absl::string_view guard_name, const std::vector<std::string>& namespaces);

  // Error handler.
  decoder::DecoderErrorListener* error_listener() const {
    return error_listener_.get();
  }
  void set_error_listener(
      std::unique_ptr<decoder::DecoderErrorListener> listener) {
    error_listener_ = std::move(listener);
  }

  std::string isa_name_;

  // Slot and bundle maps - these point to the contexts for every slot and
  // bundle that have been declared.
  absl::flat_hash_map<std::string, SlotDeclCtx*> slot_decl_map_;
  absl::flat_hash_map<std::string, BundleDeclCtx*> bundle_decl_map_;
  absl::flat_hash_map<std::string, IsaDeclCtx*> isa_decl_map_;

  // Constant map
  absl::flat_hash_map<std::string, TemplateExpression*> constant_map_;
  // Include file strings.
  absl::btree_set<std::string> include_files_;

  int current_file_index_ = 0;
  unsigned generator_version_;
  // Vector of file names.
  std::vector<std::string> file_names_;
  // Map from context pointer to file index.
  absl::flat_hash_map<antlr4::ParserRuleContext*, int> context_file_map_;
  // Include file roots.
  std::vector<std::string> include_dir_vec_;
  // Keep track of files that are included in case there is recursive includes.
  std::deque<std::string> include_file_stack_;
  // Error listening object passed to the parser.
  std::unique_ptr<decoder::DecoderErrorListener> error_listener_;
  // For template functions evaluators.
  absl::flat_hash_map<std::string, TemplateFunctionEvaluator>
      template_function_evaluators_;
  // Disassembler field widths.
  std::vector<TemplateExpression*> disasm_field_widths_;
  // AntlrParserWrapper vector.
  std::vector<IsaAntlrParserWrapper*> antlr_parser_wrappers_;
};

}  // namespace instruction_set
}  // namespace machine_description
}  // namespace sim
}  // namespace mpact

#endif  // MPACT_SIM_DECODER_INSTRUCTION_SET_VISITOR_H_
