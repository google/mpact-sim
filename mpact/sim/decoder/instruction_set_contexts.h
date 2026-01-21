// Copyright 2024 Google LLC
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

#ifndef MPACT_SIM_DECODER_INSTRUCTION_SET_CONTEXTS_H_
#define MPACT_SIM_DECODER_INSTRUCTION_SET_CONTEXTS_H_

#include "mpact/sim/decoder/InstructionSetParser.h"

namespace mpact {
namespace sim {
namespace machine_description {
namespace instruction_set {

using InstructionSetParser = ::sim::machine_description::instruction_set::
    generated::InstructionSetParser;

// Type aliases for antlr4 context types.
using TopLevelCtx = InstructionSetParser::Top_levelContext;
using NamespaceDeclCtx = InstructionSetParser::Namespace_declContext;
using ConstantDefCtx = InstructionSetParser::Constant_defContext;
using IncludeFileCtx = InstructionSetParser::Include_fileContext;
using DeclarationCtx = InstructionSetParser::DeclarationContext;
using IsaDeclCtx = InstructionSetParser::Isa_declarationContext;
using BundleDeclCtx = InstructionSetParser::Bundle_declarationContext;
using SlotDeclCtx = InstructionSetParser::Slot_declarationContext;
using BaseItemListCtx = InstructionSetParser::Base_item_listContext;
using ExpressionCtx = InstructionSetParser::ExpressionContext;
using ArraySpecCtx = InstructionSetParser::Array_specContext;
using OpcodeSpecCtx = InstructionSetParser::Opcode_specContext;
using BundleListCtx = InstructionSetParser::Bundle_listContext;
using SlotListCtx = InstructionSetParser::Slot_listContext;
using OpcodeListCtx = InstructionSetParser::Opcode_listContext;
using OpcodeOperandsCtx = InstructionSetParser::Opcode_operandsContext;
using IdentListCtx = InstructionSetParser::Ident_listContext;
using DisasmWidthsCtx = InstructionSetParser::Disasm_widthsContext;
using ConstAndDefaultCtx = InstructionSetParser::Const_and_default_declContext;
using OpcodeAttributeListCtx =
    InstructionSetParser::Opcode_attribute_listContext;
using InstructionAttributeListCtx =
    InstructionSetParser::Instruction_attribute_listContext;
using SemfuncSpecCtx = InstructionSetParser::Semfunc_specContext;
using ResourceItemCtx = InstructionSetParser::Resource_itemContext;
using ResourceDetailsCtx = InstructionSetParser::Resource_detailsContext;
// Type aliases for antlr4 types for generator related contexts.
using GeneratorOpcodeSpecListCtx =
    InstructionSetParser::Generator_opcode_spec_listContext;
using RangeAssignmentCtx = InstructionSetParser::Range_assignmentContext;
using ValueListCtx = InstructionSetParser::Value_listContext;
using GenValueCtxt = InstructionSetParser::Gen_valueContext;

}  // namespace instruction_set
}  // namespace machine_description
}  // namespace sim
}  // namespace mpact

#endif  // MPACT_SIM_DECODER_INSTRUCTION_SET_CONTEXTS_H_
