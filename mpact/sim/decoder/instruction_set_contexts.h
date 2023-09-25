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
using TupleCtx = InstructionSetParser::TupleContext;
using GenValueCtxt = InstructionSetParser::Gen_valueContext;

}  // namespace instruction_set
}  // namespace machine_description
}  // namespace sim
}  // namespace mpact

#endif  // MPACT_SIM_DECODER_INSTRUCTION_SET_CONTEXTS_H_
