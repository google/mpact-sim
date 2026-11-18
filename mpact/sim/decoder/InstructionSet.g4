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


// This ANTLR4 file specifies the representation-independent structure of an
// instruction set architecture (.isa files) for MPACT-Sim's decoder generator
// (decoder_gen). It does not describe how instructions are encoded in bits,
// protobuf messages, or text; instead, decoder_gen generates an abstract
// encoding interface class (<IsaName>EncodingBase) whose virtual methods
// extract opcodes, predicates, source/destination operands, latencies, and
// hardware resource operands from whatever concrete encoding representation the
// simulator uses (for example, code generated from BinFormat.g4).
//
// The language models the issue and word structure of an ISA:
//   - Scalar / sequential ISAs (such as RISC-V, Arm, or x86) typically consist
//     of a single top-level 'slot' containing all valid opcodes.
//   - VLIW ISAs pack multiple instructions into a wide instruction word, or
//     'bundle', issued in parallel. Each instruction position in a bundle is a
//     'slot' (or an indexed instance of a replicated 'slot[N]'). Slots within a
//     bundle may share opcodes or restrict opcodes by slot type/instance.
//   - Complex VLIW ISAs may further divide a top-level bundle into nested
//     sub-'bundles' that are fetched together as a unit but dispatched
//     separately (possibly in different cycles).
//
// Key constructs in a .isa file:
//   - 'isa <Name>': Top-level target specifying the C++ 'namespace' and the
//     top-level 'bundles' or 'slots' of the architecture.
//   - 'bundle <Name>': Groups child 'bundles' and/or 'slots' (with optional
//     instance slices such as 'alu[0..1, 3]') and binds a bundle-level
//     'semfunc' responsible for dispatching them.
//   - 'slot <Name>[N]?': Defines valid 'opcodes' plus slot defaults ('default
//     size', 'default latency', 'default opcode', 'default attributes'), local
//     'int' constants, reusable 'resources', and C++ header 'includes'. Slots
//     can be parameterized ('template <int ...> slot ...') and composed via
//     tree-structured inheritance (': base1, base2<expr>'), selectively
//     removing ('= delete') or customizing ('= override') inherited opcodes.
//   - 'opcodes': Declare instructions, their byte size, operand triplets
//     ('{ pred : sources : dests }') or multi-phase parent/child operand lists
//     ('{ (parent_ops), (child_ops), ... }'), scalar or list ('[op]') operands,
//     '%reloc(op)' annotations, compile-time or dynamic ('(*)') destination
//     latencies, 'GENERATE' template expansions, and opcode attributes
//     ('semfunc', 'disasm', 'resources', 'attributes', 'latency').
//
// From a .isa file, decoder_gen generates:
//   - <prefix>_enums.{h,cc}: SlotEnum, OpcodeEnum, PredOpEnum, SourceOpEnum,
//     ListSourceOpEnum, DestOpEnum, ListDestOpEnum, SimpleResourceEnum,
//     ComplexResourceEnum, ListComplexResourceEnum, string name arrays, and
//     per-slot AttributeEnum types.
//   - <prefix>_decoder.{h,cc}: <IsaName>EncodingBase, per-slot and per-bundle
//     decoder classes, <IsaName>InstructionSetFactory, and
//     <IsaName>InstructionSet.
//   - <prefix>_encoder.{h,cc}: <IsaName>EncoderInterfaceBase and regex-based
//     <SlotName>Matcher classes derived from 'disasm' format strings.

grammar InstructionSet;

start
  : top_level
  ;

top_level
  : declaration* EOF
  ;

include_top_level
  : declaration* EOF
  ;

// Top-level declarations: '.isa' file imports ('#include'), global C++ header
// blocks ('includes'), ISA targets ('isa'), VLIW 'bundle' and 'slot'
// definitions, disassembly column widths ('disasm widths'), and global 'int'
// constants.
declaration
  : include_file
  | include_file_list
  | isa_declaration
  | bundle_declaration
  | slot_declaration
  | disasm_widths
  | constant_def
  ;

// Lists C++ '#include' files to emit into the generated '*_decoder.cc' file
// (for example, headers declaring semantic functions). This block may appear at
// global scope, inside a 'bundle', or inside a 'slot'. Global 'includes' are
// always emitted; bundle- and slot-local 'includes' are merged and emitted only
// if that bundle or slot is reachable from the selected 'isa' (directly or via
// inheritance).

include_file_list
  : INCLUDE_FILES '{' include_file* '}'
  ;

// Defines a named integer constant ('int name = expression;') at global scope
// or inside a slot.
constant_def
  : template_parameter_type ident '=' expression ';'
  ;

// Specifies disassembler column widths and alignments. Each 'disasm' attribute
// on an opcode provides one or more comma-separated format fragments (typically
// a mnemonic fragment followed by an operands fragment). This declaration sets
// the character width for each fragment from left to right, and whether each
// fragment is left-justified (negative width, e.g., -18) or right-justified
// (positive width, e.g., 18) within its column.
disasm_widths
  : DISASM WIDTHS '=' '{' (expression (',' expression) *)? '}' ';'
  ;

// Defines a top-level ISA target, its generated C++ namespace, and either its
// top-level 'bundles' or its top-level 'slots'. 'instruction_set_name'
// determines generated class prefixes such as <IsaName>EncodingBase,
// <IsaName>InstructionSet, and <IsaName>InstructionSetFactory. Multiple 'isa'
// declarations may appear; if more than one is defined, pass '--isa_name' to
// decoder_gen to select which one to generate.
isa_declaration
  : ISA instruction_set_name=IDENT '{' namespace_decl (bundle_list | slot_list) '}'
  ;

// Specifies the C++ namespace in which code for this ISA is generated.
namespace_decl
  : NAMESPACE namespace_ident ('::' namespace_ident) * ';'
  ;

// Matches '#include "<path>"'. At top level, parses and imports declarations
// from another '.isa' file; inside 'includes { ... }', emits a C++ '#include'
// directive into the generated '*_decoder.cc' file.

include_file
  : INCLUDE STRING_LITERAL
  ;

// Defines a VLIW bundle containing child 'bundles' and/or 'slots', optional
// bundle-local C++ 'includes', and a bundle-level 'semfunc' responsible for
// executing/dispatching its constituent sub-bundles and slots. At least one
// child bundle or slot must be specified in the bundle body.
bundle_declaration
  : BUNDLE bundle_name=IDENT '{' bundle_parts* '}'
  ;

bundle_parts
  : include_file_list
  | bundle_list
  | slot_list
  | semfunc_spec ';'
  ;

// Lists sub-bundles contained in an 'isa' or 'bundle' block.

bundle_list
  : BUNDLES '{' (bundle_spec ';')* ','? '}'
  ;

bundle_spec
  : IDENT
  ;

// Lists slots (and optional instance ranges) contained in an 'isa' or 'bundle'.

slot_list
  : SLOTS '{' (slot_spec ';')* ','? '}'
  ;

// References a declared slot, optionally selecting specific instance indices or
// ranges when the slot was declared with multiple instances ('slot name[N]').

slot_spec
  : IDENT array_spec?
  ;

// Comma-separated list of slot instance indices or inclusive 'start..end'
// ranges (0-based).

array_spec
  : '[' range_spec (',' range_spec)* ']'
  ;

// A single 0-based instance index, or an inclusive range of indices.

range_spec
  : range_start=NUMBER (DOTDOT range_end=NUMBER)?
  ;

// Declares either a templated slot ('template <int ...> slot name') or a plain
// slot ('slot name[count]?'), optionally inheriting from one or more base slots
// or instantiated template slots (tree-structured inheritance only; diamond
// inheritance of the same ancestor or duplicate inherited opcodes is an error).
// Notes:
//   - Only plain slots referenced directly by an 'isa' or 'bundle' may specify
//     an instance count ('[count]'); unreferenced base slots and template slots
//     may not have a size specification.
//   - When a slot specifies '[count]', all instances 0..count-1 must be
//     referenced across the ISA/bundles.

slot_declaration
  // Template slot.
  : template_decl SLOT slot_name=IDENT
    (':' base_item_list )? '{' const_and_default_decl* opcode_list? '}'
  // Plain slot.
  | SLOT slot_name=IDENT size_spec? (':' base_item_list )?
    '{' const_and_default_decl* opcode_list? '}'
  ;

template_decl
  : TEMPLATE '<' template_parameter_decl (',' template_parameter_decl)* '>'
  ;

template_parameter_decl
  :  template_parameter_type IDENT
  ;

// Only integer-valued template parameters and constants are currently
// supported.

template_parameter_type
  : INT
  ;

// Comma-separated list of base slots or template slot instantiations.

base_item_list
  : base_item (',' base_item)*
  ;

base_item
  : IDENT template_spec?
  ;

template_spec
  : '<' expression (',' expression) * '>'
  ;

// Compile-time integer expression supporting numeric literals, global/slot
// constants, template parameters, destination operand latency references (in
// resource cycle expressions), unary '-', '+', '-', '*', '/', parentheses, and
// built-in functions (currently 'abs(expr)').

expression
  : negop expr=expression
  | lhs=expression mulop rhs=expression
  | lhs=expression addop rhs=expression
  | func=IDENT '(' (expression (',' expression)* )? ')'
  | '(' paren_expr=expression ')'
  | NUMBER
  | IDENT
  ;

negop
  : '-'
  ;

mulop
  : '*' | '/'
  ;

addop
  : '+' | '-'
  ;

// Number of slot instances (for 'slot name[N]') or instruction byte size (for
// 'opcode_name[bytes]').

size_spec
  : '[' NUMBER ']'
  ;

// Slot-scoped defaults, constants, named resource groups, and C++ includes:
//   - 'default latency = expr;': Default destination operand latency in cycles
//     when an opcode's destination omits '(...)'.
//   - 'default size = N;': Default byte size for opcodes in this slot.
//   - 'default opcode = ...;': Attributes (must include 'semfunc:') used for
//     unmatched/invalid opcodes (OpcodeEnum::kNone) and as fallback 'semfunc'
//     when an opcode omits 'semfunc:'. Required on every referenced slot.
//   - 'default attributes = { ... };': Default instruction attribute values
//     applied to every instruction in this slot unless overridden per-opcode.
//   - 'int ident = expr;': Slot-local integer constant.
//   - 'resources ident = ...;': Named resource specification reusable by
//     opcodes in this slot via 'resources: ident'.
//   - 'includes { ... }': Slot-local C++ '#include' headers.
const_and_default_decl
  : DEFAULT LATENCY '=' expression ';'
  | DEFAULT SIZE '=' NUMBER ';'
  | DEFAULT OPCODE '=' opcode_attribute_list ';'
  | DEFAULT ATTRIBUTES '=' instruction_attribute_list ';'
  | constant_def
  | RESOURCES ident '=' resource_details ';'
  | include_file_list
  ;

// List of opcode specifications for the slot in question.

opcode_list
  : OPCODES '{' (opcode_spec ';')* '}'
  ;

// This rule is used as a start rule when parsing generated opcode specs.
opcode_spec_list
  : (opcode_spec ';') *
  ;

// Specifies an opcode entry inside a slot's 'opcodes { ... }' block:
//   1. Definition: 'name [byte_size]? { operand_spec } (, attributes)?'
//      Opcode names must be unique across the ISA and generate 'k<PascalName>'
//      enumerators in OpcodeEnum.
//   2. Deletion: 'name = delete'
//      Excludes an opcode that would otherwise be inherited from a base slot.
//   3. Override: 'name = override, opcode_attribute_list'
//      Modifies attributes ('semfunc', 'disasm', 'resources', 'attributes', or
//      'latency') of an opcode inherited from a base slot while preserving its
//      operands and size.
//   4. Generator: 'GENERATE(range_assignments) { ... };'
//      Expands parameterized opcode definitions across the Cartesian product of
//      variable or tuple value lists using '$(var)' substitution.

opcode_spec
  : name=IDENT
    (
        '=' deleted=DELETE
      | size_spec? '{' operand_spec '}' (',' opcode_attribute_list)?
      | '=' overridden=OVERRIDE ',' opcode_attribute_list
    )
  | generate=GENERATE '(' range_assignment (',' range_assignment)* ')'
   '{' generator_opcode_spec_list '}'
  ;

// Either a single operand specification (for a single-phase instruction) or a
// comma-separated list of parenthesized operand specifications where the first
// tuple describes the parent Instruction and each subsequent tuple describes a
// chained child Instruction (e.g., load address issue followed by writeback).
operand_spec
  : opcode_operands
  | opcode_operands_list
  ;

opcode_operands_list
  : '(' opcode_operands ')' (',' '(' opcode_operands ')' )*
  ;

// Operand triplet: optional predicate operand, optional ':' + source operand
// list, and optional ':' + destination operand list. Examples:
//   - '{}'                       (no operands)
//   - '{ pred }'                 (predicate only)
//   - '{ : rs1, imm12 }'         (sources only)
//   - '{ : rs1, rs2 : rd }'      (sources and destination, no predicate)
//   - '{ pred : rs1 : rd(2) }'   (predicate, source, and destination)
opcode_operands
  : pred=IDENT? (':' source=source_list? ( ':' dest_list? )? )?
  ;

source_list
  : source_operand (',' source_operand)*
  ;

// A scalar source operand (mapped to SourceOpEnum and GetSource()) or a
// bracketed list/array source operand '[ident]' (mapped to ListSourceOpEnum and
// GetSources(), e.g., for register-list push/stm instructions).
source_operand
  : operand
  | '[' array_source=IDENT ']'
  ;

// Optional '%reloc(op_name)' marks an operand as carrying relocation metadata
// for encoder/assembler generation.
operand
  : op_attribute=OP_ATTRIBUTE '(' op_name=IDENT ')'
  | op_name=IDENT
  ;

// Destination operands may be scalar (DestOpEnum / GetDestination()) or
// bracketed list/array operands '[ident]' (ListDestOpEnum / GetDestinations()),
// and each must resolve to a writeback latency:
//   - '(expression)': Compile-time cycle latency (0 = immediate writeback,
//     >= 1 = buffered writeback after N cycles).
//   - '(*)': Dynamic latency queried at decode time via
//     <IsaName>EncodingBase::GetLatency(...).
//   - Omitted: Uses the enclosing slot's 'default latency' (error if none is
//     defined unless overridden via a 'latency:' attribute).

dest_list
  : dest_operand (',' dest_operand)*
  ;

dest_operand
  : (operand | '[' array_dest=IDENT ']')
    ( '(' (expression | wildcard='*' ) ')' )?
  ;

// Variable and tuple range bindings for 'GENERATE(...)':
//   - Single variable: 'var = [val1, val2, ...]'
//   - Tuple binding:   '[v1, v2] = [{a1, b1}, {a2, b2}, ...]'
range_assignment
  : IDENT '=' '[' gen_value (',' gen_value)* ']'
  | '[' IDENT (',' IDENT)* ']' '=' '[' value_list (',' value_list)* ']'
  ;

value_list
  : '{' gen_value (',' gen_value)* '}'
  ;

gen_value
  : simple=(IDENT | NUMBER)
  | string=STRING_LITERAL
  ;

generator_opcode_spec_list
  : generator_opcode_spec (generator_opcode_spec)*
  ;

generator_opcode_spec
  : name=(VAR_IDENT | IDENT) size_spec?
    '{' generator_operand_spec '}'
     (',' opcode_attribute_list)? ';'
  ;

generator_operand_spec
  : generator_opcode_operands
  | '(' generator_opcode_operands ')' (',' '(' generator_opcode_operands ')' )*
  ;

generator_opcode_operands
  : pred=(VAR_IDENT | IDENT)?
    (':' source=var_ident_list?
    ( ':' generator_dest_list? )? )?
  ;

generator_dest_list
  : dest=(VAR_IDENT | IDENT) ('(' (expression | wildcard='*' ) ')' )?
  ;

var_ident_list
  : var_ident=(VAR_IDENT | IDENT) (',' var_ident=(VAR_IDENT | IDENT)) *
  ;

// An opcode attribute list is a comma-separated list with at least one member.

opcode_attribute_list
  :  opcode_attribute (',' opcode_attribute)*
  ;

// An opcode attribute specifies disassembly formatting ('disasm:'), semantic
// function bindings ('semfunc:'), hardware resource usage ('resources:'),
// integer metadata tags ('attributes:'), or destination operand latencies
// ('latency:'). At most one attribute of each kind may be given per opcode.

opcode_attribute
  : disasm_spec
  | semfunc_spec
  | resource_spec
  | instruction_attribute_spec
  | latency_spec
  ;

// The disassembly specifier lists one or more format strings (applied to the
// parent instruction). Each string is formatted within the corresponding column
// width and justification from the global 'disasm widths' declaration; any
// remaining strings beyond the declared widths are concatenated directly.
// Within format strings:
//   - '%op' substitutes the operand's AsString() text; '%op?' omits/formats
//     optional operands such as predicates.
//   - '%(expr:fmt)' formats integer expressions involving '@' (the instruction
//     address), '@ +/- op', parenthesized bit shifts ('(op << N)' or
//     '@ +/- (op >> N)'), and radix/width specifiers ':d', ':o', ':x' (which
//     automatically prepends '0x'), or ':X' (uppercase hex without '0x'), with
//     an optional 1- or 2-digit width such as ':08x' or ':04d'.
disasm_spec
  : DISASM ':' STRING_LITERAL ( ',' STRING_LITERAL )*
  ;

// Specifies or overrides latencies for existing destination operands of the
// opcode (useful with '= override' in derived slots or alongside operand
// declarations).
latency_spec
  : LATENCY ':' dest_operand_latency_list
  ;

dest_operand_latency_list
  : dest_operand_with_latency (',' dest_operand_with_latency)*
  ;

dest_operand_with_latency
  : (op_name=IDENT | '[' array_dest=IDENT ']')
    ('(' (expression | wildcard='*' ) ')')?
  ;

// The semantic function specifier lists C++ expressions convertible to
// Instruction::SemanticFunction (callable as 'void(Instruction *)'), used when
// executing the instruction. Provide one string for the parent instruction plus
// one string for each child instruction in order.
// E.g., given:
//
// void MySemFunc(Instruction *);
// void MyOtherFcn(int num_regs, Instruction *);
// void MyThirdFcn(Instruction *, int width);
//
// valid strings include:
//
// "&MySemFunc"
// "absl::bind_front(&MyOtherFcn, /*num_regs=*/8)"
// "std::bind(&MyThirdFcn, std::placeholders::_1, /*width=*/32)"
// "[](Instruction *inst) { /* inline semantics */ }"
//
semfunc_spec
  : SEMFUNC ':' STRING_LITERAL ( ',' STRING_LITERAL )*
  ;

// Specifies hardware hazard/pipeline resources used, acquired, or held by the
// instruction, either inline ('{ use : acquire : hold }') or by referencing a
// named 'resources' definition declared in the enclosing slot.
resource_spec
  : RESOURCES ':' resource_details
  ;

instruction_attribute_spec
  : ATTRIBUTES ':' instruction_attribute_list
  ;

// Resource details contain up to three colon-separated lists:
//   - 'use_list': Resources that must be free at dispatch/issue time (checked
//     only, not reserved).
//   - 'acquire_list': Resources that must be free at dispatch time AND are
//     reserved for their specified '[begin..end]' cycle window.
//   - 'hold_list': Resources reserved for their '[begin..end]' cycle window
//     without checking whether they are already free at dispatch time.
// Or 'ident' referencing a slot-level 'resources ident = ...;' declaration.
resource_details
  : '{' use_list=resource_item_list?
     (':' acquire_list=resource_item_list?
     (':' hold_list=resource_item_list? )? )? '}'
  | ident
  ;

resource_item_list
  : resource_item (',' resource_item)*
  ;

// Scalar ('name') or list/array ('[name]') resource reference with an optional
// cycle window '[begin_cycle..end_cycle]':
//   - If 'begin_cycle' is omitted, it defaults to cycle 0 (issue cycle). Any
//     resource acquired with a non-zero 'begin_cycle' in the ISA is classified
//     as a complex resource (ComplexResourceEnum / ListComplexResourceEnum);
//     resources acquired only from cycle 0 are simple resources
//     (SimpleResourceEnum).
//   - If 'end_cycle' is omitted, it defaults to the latency of the matching
//     destination operand (if 'name' names a destination operand or references
//     one in its cycle expression), or 0 otherwise.
// Examples:
//   x[1..3]:  reserved from cycle 1 (cycle after issue) through cycle 3.
//   x[..3]:   reserved from cycle 0 (issue) through cycle 3.
//   x[] or x: reserved from cycle 0 through matching dest latency (or 0).
//   x[2..]:   reserved from cycle 2 through matching dest latency (or 0).

resource_item
  : ( name=IDENT | '[' array_name=IDENT ']' )
        ('[' (begin_cycle=expression)? ('..' end_cycle=expression? )? ']')?
  ;

// Instruction attributes assign compile-time integer metadata values to an
// instruction, e.g., '{ is_branch = 1, priv = 2 }'. If '= expression' is
// omitted (e.g., '{ attr_a }' to declare an attribute in 'default attributes'),
// its value is 0. Attributes declared in a slot but omitted on an opcode
// inherit the slot's 'default attributes' value (or 0 if unset). For each slot
// with attributes, decoder_gen emits 'namespace <slot_snake> { enum class
// AttributeEnum { k<AttrPascal> = ..., kPastMaxValue = ... }; }' in
// '*_enums.h' to index into Instruction::Attributes().

instruction_attribute_list
  : '{' instruction_attribute (',' instruction_attribute)* '}'
  ;

instruction_attribute
  : IDENT ('=' expression)?
  ;

// Comma separated list of identifiers.

ident_list
  : IDENT (',' IDENT)*
  ;

// Don't have to exclude all the reserved words from the permissible namespace
// identifiers, just the obvious C++ ones.

namespace_ident
  : IDENT | 'latency' | 'size' | 'includes' | 'isa' | 'bundle'
  | 'bundles' | 'slot' | 'slots' | 'opcode' | 'opcodes' | 'disasm' | 'semfunc'
  ;

ident
  : IDENT
  ;

// Lexer specification

// Reserved words.
ATTRIBUTES : 'attributes';
BUNDLE : 'bundle';
BUNDLES : 'bundles';
DEFAULT : 'default';
DELETE : 'delete';
DISASM : 'disasm';
GENERATE: 'GENERATE';
WIDTHS : 'widths';
SIZE : 'size';
INCLUDE : '#include';
INCLUDE_FILES : 'includes';
INT : 'int';
ISA : 'isa';
LATENCY : 'latency';
OPCODE : 'opcode';
OPCODES : 'opcodes';
OVERRIDE : 'override';
NAMESPACE : 'namespace';
RESOURCES : 'resources';
SEMFUNC : 'semfunc';
SLOT : 'slot';
SLOTS : 'slots';
TEMPLATE : 'template';

OP_ATTRIBUTE : '%reloc';

// Identifiers, '$(var)' generator references, strings, numeric literals (which
// allow single-quote '\'' digit separators), range operator, and comments.
STRING_LITERAL : UNTERMINATED_STRING_LITERAL '"';
fragment UNTERMINATED_STRING_LITERAL : '"' (~["\\\r\n] | '\\' (. | EOF))*;
IDENT : [_a-zA-Z][_a-zA-Z0-9]*;
VAR_IDENT : ([_a-zA-Z] | VAR_REF) ([_a-zA-Z0-9] | VAR_REF)*;
fragment VAR_REF: '$(' IDENT ')';
NUMBER: HEX_NUMBER | OCT_NUMBER | DEC_NUMBER | BIN_NUMBER;
fragment HEX_NUMBER: '0x' HEX_DIGIT (HEX_DIGIT | '\'')*;
fragment HEX_DIGIT: [0-9a-fA-F];
fragment OCT_NUMBER: '0' (OCT_DIGIT | '\'')*;
fragment OCT_DIGIT: [0-7];
fragment DEC_NUMBER: ('0' | [1-9] ([0-9] | '\'')*);
fragment BIN_NUMBER: '0b' [0-1] ([0-1] | '\'')*;
DOTDOT : '..' ;

BLOCK_COMMENT : '/*' .*? '*/' -> channel(HIDDEN);
LINE_COMMENT : '//' ~[\n\r]* -> channel(HIDDEN);
WS : [ \t\r\n] -> channel(HIDDEN) ;
