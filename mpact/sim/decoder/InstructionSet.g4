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


// This grammar is used to specify the structure of the instruction decoder and
// is used by the parser to generate the encoding independent part of the basic
// instruction decoder. This grammar does not addess the actual encoding of the
// instruction as that may be expressed in multiple different forms, such as
// traditional binary, protobuff, etc. The code generated from this grammar
// will declare virtual methods to obtain opcodes and operands from the
// encoding that need to be overridden with methods cognizant of the actual
// instruction encoding used.
//
// The grammar specifies the instruction word structure of an ISA, i.e.,
// the grouping and structure of instruction words. At its simplest, the ISA
// consists of a single slot, where any opcode is valid. This is the case for
// most traditional architectures, where the ISA is really sequential (though
// the implementation may do parallel issue), such as the x86 and Arm ISAs.
//
// A VLIW based ISA supports specifying multiple instructions in a large
// instruction word, or bundle, that will be issued in parallel. Each
// instruction within the large instruction word occupies a slot. In the case
// of a binary instruction encoding, a slot refers to a specific bit range
// (or bit ranges if the slot is replicated) within the instruction word. In
// case of a protobuf based encoding, a slot refers to one or more message
// instances. The individual slots within a bundle may support the same set of
// opcodes, or the opcodes may be restricted by the slot instance. The latter
// allows for different instruction slots to have different layout and widths,
// optimizing for the specific opcodes (e.g., load/store vs alu) assigned to
// each slot.
//
// A yet more complex ISA may divide a top level bundle into more than one
// "sub-bundles". The idea here is that while the top level bundle is fetched
// and "issued" as a unit, the sub-bundles are then separated and issued
// separately, possibly in different cycles.
//
// The top level of the grammar is the specification of the ISA name and the
// name of the class that provides access to the opcode that is being decoded.
// The details of that "encoding" class is not used by the generated code,
// instead a pointer to that class is passed to pure virtual methods that
// the overall decoder will have to provide overriding implementations for
// to have a working decoder.
//
// The ISA is then broken down into one or more bundles and/or slots. A bundle
// typically contains a group of slots that correspond to the set of
// instructions that are fetched and issued together as part of a long
// instruction word. However, a bundle may also contain other bundles that
// are issued as separate groups of instructions.
//
// Each slot contains one or more opcodes that represent the set of valid
// instructions for that slot. Since the same opcode may be valid in multiple
// slots, it is possible to define slots that aren't used directly in any
// bundle, but are used as "base classes" for those that are, allowing common
// opcodes to be factored out for ease of expression and maintenance.
//
// An opcode represents an instruction and contains an optional predicate
// operand name, an optional list of source operand names, and an optional
// list of destination operand names. The opcode name is used to generate an
// enumeration type used by the decoder. The operand names are used to
// declare virtual getter methods in the slot class for creating source and
// destination operands. The operand names are intended to correspond to
// specific operand fields in the instruction encoding.

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

declaration
  : include_file
  | include_file_list
  | isa_declaration
  | bundle_declaration
  | slot_declaration
  | disasm_widths
  | constant_def
  ;

// The include_file_list lists files to include in the generated source. This
// may be specified at the global scope, or within each slot. Slot local Include
// files are only added to the generated code if that slot is used. This is
// to make it possible to avoid adding include files that will not be used
// in the final isa decoder. If there are multiple global include file lists,
// their content are merged.

include_file_list
  : INCLUDE_FILES '{' include_file* '}'
  ;

constant_def
  : template_parameter_type ident '=' expression ';'
  ;

// This rule specifies the disassembler field widths and alignments. A
// diasassembly string is specified by a number of fragments. Typically an
// opcode fragment followed by a fragment for the operands. This declaration
// specifies the field width for each fragment in order left to right, and
// whether the fragment is left justified (negative number) or right
// justified (positive number) within that field.
disasm_widths
  : DISASM WIDTHS '=' '{' (expression (',' expression) *)? '}' ';'
  ;

// This rule defines the name of an ISA description as well as the
// name of the type that wraps the encoding of the instruction. The ISA instance
// contains either a list of instruction slots or a list of instruction bundles.
// There is either one ISA description, or if there are more than one, only one
// can be selected for code generation.
isa_declaration
  : ISA instruction_set_name=IDENT '{' namespace_decl (bundle_list | slot_list) '}'
  ;

// The namespace_decl rule is used to specify which namespace in which to
// generate the code for the isa.
namespace_decl
  : NAMESPACE namespace_ident ('::' namespace_ident) * ';'
  ;

// Mactches #include "<path to file>".

include_file
  : INCLUDE STRING_LITERAL
  ;

// A bundle_declaration has a name and specifies a semantic function, which is
// is responsible for dispatching the instructions for the bundles and/or slots
// It also specifies the bundles and/or slots the set of bundles and/or slots
// it contains. At least one slot or bundle has to be specified.
bundle_declaration
  : BUNDLE bundle_name=IDENT '{' bundle_parts* '}'
  ;

bundle_parts
  : include_file_list
  | bundle_list
  | slot_list
  | semfunc_spec ';'
  ;

// A bundle list is a non-empty list of bundle identifiers

bundle_list
  : BUNDLES '{' (bundle_spec ';')* ','? '}'
  ;

bundle_spec
  : IDENT
  ;

// A slot list is a non-empty list of slot specifiers.

slot_list
  : SLOTS '{' (slot_spec ';')* ','? '}'
  ;

// A slot specifier is a slot name with an optional range specification
// to specify which instances are being used when the slot may occur multiple
// times in a bundle or across multiple bundles. See below for slot declaration.

slot_spec
  : IDENT array_spec?
  ;

// The list of ranges of slot instances used.

array_spec
  : '[' range_spec (',' range_spec)* ']'
  ;

// A single index, or range of indices.

range_spec
  : range_start=NUMBER (DOTDOT range_end=NUMBER)?
  ;

// Declares a slot with an optional size spec ([size]) indicating that it has
// multiple instances. It may optionally inherit from another slot. Note,
// it is an error for a base slot that isn't used directly in the ISA (i.e.,
// only by inheritance) to have a size specification. It is also an error
// if not all slot instances are referenced in the isa (either at the top
// level, from within a bundle, or in an inheritance specification).
// A template slot may not have a size specification.

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

// Only integer valued template parameters are allowed for now.

template_parameter_type
  : INT
  ;

// Can inherit from slots or templated slots.

base_item_list
  : base_item (',' base_item)*
  ;

base_item
  : IDENT template_spec?
  ;

template_spec
  : '<' expression (',' expression) * '>'
  ;

// Integer literals or template parameter names are allowed.

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

// Number of instances.

size_spec
  : '[' NUMBER ']'
  ;

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

// An opcode has a name, an optional predicate operand name, followed by
// optional lists of source and destination operand names. Each is separated
// by a colon. The colon between the predicate operand name and the source
// operand name list is mandatory even if there is no predicate operand name.
// The colon between the source and destination operand name lists is only
// required if there is a destination operand list. An opcode name is required
// to be unique. An opcode that would otherwise be inherited can be deleted
// from the derived slot. This means that a derived slot isn't necessarily a
// true superset of the base slot.

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

operand_spec
  : opcode_operands
  | opcode_operands_list
  ;

opcode_operands_list
  : '(' opcode_operands ')' (',' '(' opcode_operands ')' )*
  ;

opcode_operands
  : pred=IDENT? (':' source=source_list? ( ':' dest_list? )? )?
  ;

source_list
  : source_operand (',' source_operand)*
  ;

source_operand
  : operand
  | '[' array_source=IDENT ']'
  ;

operand
  : op_attribute=OP_ATTRIBUTE '(' op_name=IDENT ')'
  | op_name=IDENT
  ;

// Destination operands may include a latency.

dest_list
  : dest_operand (',' dest_operand)*
  ;

dest_operand
  : (operand | '[' array_dest=IDENT ']')
    ( '(' (expression | wildcard='*' ) ')' )?
  ;

// Special rules for generator instruction descriptions.
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

// An opcode attribute list is a comma separated list with at least one member.

opcode_attribute_list
  :  opcode_attribute (',' opcode_attribute)*
  ;

// An opcode attribute is either a disassembly specifier or a semfunc specifier.

opcode_attribute
  : disasm_spec | semfunc_spec | resource_spec | instruction_attribute_spec
  ;

// The disassembly specifier lists a sequence of format strings. Each formatted
// string is printed within a field of the width and justification specified in
// the global "disasm widths" declaration. If no widths are specified, or fewer
// widths are specified than there are format strings, the "extra" formatted
// strings are concatenated with no explicit width or justification applied.
disasm_spec
  : DISASM ':' STRING_LITERAL ( ',' STRING_LITERAL )*
  ;

// The semantic function specifier lists a sequence of strings that in C++ can
// be assigned to a C++ callables with signature void(Instruction *). These
// will be used when dispatching the instruction. There will be one string
// for the instruction itself, plus one for each child instruction.
// E.g.,
// Given the following function definitions:
//
// void MyCFunction(const Instruction *);
// void MyOtherFcn(int num_regs, Instruction *);
// void MyThirdFcn(Instruction *, int width);
//
// The strings should be:
//
// "&MyCFunction"
// "absl::bind_front(&MyOtherFcn, /*num_regs*/ 8)"
// "std::bind(&MyThirdFcn, std::_1, /*width*/ 32)"
//
semfunc_spec
  : SEMFUNC ':' STRING_LITERAL ( ',' STRING_LITERAL )*
  ;

// The resource specifier lists the resource uses of the instruction.
resource_spec
  : RESOURCES ':' resource_details
  ;

instruction_attribute_spec
  : ATTRIBUTES ':' instruction_attribute_list
  ;

resource_details
  : '{' use_list=resource_item_list?
     (':' acquire_list=resource_item_list?
     (':' hold_list=resource_item_list? )? )? '}'
  | ident
  ;

resource_item_list
  : resource_item (',' resource_item)*
  ;

// The resource will be acquired from begin_cycle to end_cycle. If omitted,
// end_cycle is the result latency of the instruction. If omitted, begin_cycle
// is cycle 0 (when the instruction issues).
// Examples:
// x[1..3]: x is acquired starting the cycle after issue through cycle 3.
// x[..3]:  x is acquired starting at issue through cycle 3.
// x[] or x: x is acquired starting at issue through the instruction latency.
// x[2]: x is acquired starting at cycle 2 through the instruction latency.

resource_item
  : ( name=IDENT | '[' array_name=IDENT ']' )
        ('[' (begin_cycle=expression)? ('..' end_cycle=expression? )? ']')?
  ;

// Instruction attributes are a list of attribute names that are assigned
// a value such as: { priv=0, branch=1.. } etc. Values can be omitted, in
// which case the value is 1. Attributes that are not named are implicitly
// defined to have value 0. All attribute names in an isa are listed in an
// enum class and are used as the index into the instruction attribute
// array.

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

// Other tokens.
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
