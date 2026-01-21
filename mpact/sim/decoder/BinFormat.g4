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

// This grammar is used to specify the structure of the binary encoding of an
// instruction set architecture. The generator tool that reads in this grammar
// will generate code to aid in the decoding of instructions by determining
// which opcode a particular instruction word value matches. Additionally, the
// tool will generate extractor functions to extract the bitfields as described
// in the input description.

grammar BinFormat;

top_level
  : once? declaration_list EOF
  ;

once
  : '#' ONCE
  ;

declaration_list
  : (decoder_def | declaration)*
  ;

declaration
  : format_def | instruction_group_def | include_file
  ;

// Defines an instruction format. It may inherit from another format, though
// "inheritance" in this context only refers to the width, and helps group
// formats in a hierarchy.
format_def
  : FORMAT name=IDENT width=index? inherits_from? 
    '{' layout_spec? format_field_defs'}' ';'?
  ;

inherits_from
  : ':' IDENT
  ;

layout_spec
  : LAYOUT ':' layout_type ';'
  ;

layout_type
  : DEFAULT
  | PACKED_STRUCT
  ;

// Each format consists of a number of entries whose widths sum up to the
// format widths. Each entry is either a field, or a sub-format (creating
// a hierarchy), with the width specified with array notation. Fields and/or
// sub-formats are ordered left to right within the bits of the format according
// to their order of declaration. Multiple consecutive instances of a sub-format
// may be expressed using array notation. The sub-format instances are ordered
// left to right in the format in order of the instance number. Within each
// field (or sub-format), bits are numbered from high to low going left to
// right, that is, the msb is the highest numbered bit. That means the lsb for
// any format/fields is bit number 0.
format_field_defs
  : FIELDS ':' field_def* (OVERLAYS ':' overlay_def*)?
  ;

// A field is either a named field with a width, or a reference to another
// format. A field may be signed or unsigned. If signed, the extractor function
// will include code to sign-extend the value.
field_def
  : sign_spec field_name=IDENT width=index field_assign? ';'
  | FORMAT format_name=IDENT field_name=IDENT index? ';'
  ;

field_assign
  : '=' number
  ;

// An overlay is defined by a name and a list of bit fields in the format. An
// overlay may be either signed or unsigned. If signed, the extractor function
// will include code to sign-extend the value.
overlay_def
  : sign_spec IDENT width=index '=' bit_field_list ';'
  ;

index
  : '[' number ']'
  ;

sign_spec
  : SIGNED | UNSIGNED
  ;

bit_field_list
  : bit_field_spec (',' bit_field_spec)*
  ;

// A bit field is either:
//   - A full named bit field.
//   - A set of bit ranges from a bit field.
//   - A set of bit ranges from a format.
//   - A binary number litera.
bit_field_spec
  : IDENT bit_range_list?
  | bit_range_list
  | bin_number
  ;

bin_number
  : BIN_NUMBER
  ;

bit_range_list
  : '[' bit_index_range (',' bit_index_range ) * ']'
  ;

bit_index_range
  : number ( DOTDOT number)?
  ;

// An instruction group lists the encodings of a set of instructions that all
// derive from the same format.
instruction_group_def
  : INSTRUCTION GROUP name=IDENT '[' number ']' ':' inst_format=IDENT
    '{' instruction_def_list '}' ';'?
  | INSTRUCTION GROUP name=IDENT '=' '{' group_name_list '}' ';'?
  ;

instruction_def_list
  : instruction_def*
  ;

// An instruction encoding contains a name, the format it refers to, and a list
// of binary field constraints.
instruction_def
  : name=IDENT ':' format_name=IDENT ':' field_constraint_list ';'
  | generate=GENERATE '(' range_assignment (',' range_assignment)* ')'
    '{' generator_instruction_def_list '}' ';'
  | name=IDENT ':' SPECIALIZES parent=IDENT ':' field_constraint_list ';'
  ;


range_assignment
  : IDENT '=' '[' gen_value (',' gen_value)* ']'
  | '[' IDENT (',' IDENT)* ']' '=' '[' value_list (',' value_list)* ']'
  ;

value_list
  : '{' gen_value (',' gen_value)* '}'
  ;

gen_value
  : (IDENT | number)
  | string=STRING_LITERAL
  ;

generator_instruction_def_list
  : generator_instruction_def*
  ;

generator_instruction_def
  : name=(VAR_IDENT | IDENT) ':' format_name=(VAR_IDENT | IDENT) ':'
    generator_field_constraint (',' generator_field_constraint) * ';'
  ;

generator_field_constraint
  : (VAR_IDENT | IDENT) constraint_op (number | VAR_IDENT)
  ;

field_constraint_list
  : field_constraint (',' field_constraint)*
  ;

// Each field constraint specifies the name or either a field or overlay defined
// in the format the encoding refers to, and whether that field/overlay has to
// be equal, not equal, greater/less, etc. to a number given by a numeric
// literal.
field_constraint
  : field_name=IDENT constraint_op (value=number | rhs_field_name=IDENT)
  ;

constraint_op
  : '=='
  | '!='
  | '>'
  | '>='
  | '<'
  | '<='
  ;

// A decoder definition is a named grouping of instruction groups for which a
// binary decoder can be generated. You may speficy multiple decoders. If more
// than one is specified, the name of the decoder to generate code for must be
// passed to the tool.
decoder_def
  : DECODER name=IDENT '{'  decoder_attribute* '}' ';'?
  ;

decoder_attribute
  : include_files
  | opcode_enum_decl
  | namespace_decl
  | group_name
  ;


// Namespaces can be specified to wrap the body of the instruction set. Inside
// the name spaces is the declaration of the instruction format layouts, as
// well as the full instruction encodings.
namespace_decl
  : NAMESPACE namespace_ident ( '::' namespace_ident) * ';'
  ;

opcode_enum_decl
  : OPCODE_ENUM '=' STRING_LITERAL ';'
  ;

include_files
  : INCLUDE_FILES '{' include_file* '}' ';'?
  ;

include_file
  : INCLUDE STRING_LITERAL
  ;

// A group name is either the name of an instruction group or a ident for
// a new instruction group that is the result of merging the list of groups
// specified in braces.
group_name
  : IDENT ';'
  | IDENT '=' '{' group_name_list '}' ';'
  ;

// Allow an extra comma at the end of the list.
group_name_list
  : IDENT (',' IDENT)* ','?
  ;

number
  : HEX_NUMBER | OCT_NUMBER | DEC_NUMBER | BIN_NUMBER
  ;

namespace_ident
  : IDENT | DECODER | GROUP | BINARY | FORMAT | OVERLAYS | INSTRUCTION
  ;
//
// Lexer specification
//

// Reserved words.

NAMESPACE : 'namespace';
SIGNED : 'signed';
UNSIGNED : 'unsigned';
INCLUDE : '#include';
INCLUDE_FILES : 'includes';
OPCODE_ENUM : 'opcode_enum';
BINARY : 'binary';
FORMAT : 'format';
FIELDS : 'fields';
GENERATE: 'GENERATE';
NAMESPACES : 'namespaces';
OVERLAYS : 'overlays';
INSTRUCTION : 'instruction';
GROUP : 'group';
DECODER : 'decoder';
SPECIALIZES : 'specializes';
LAYOUT : 'layout';
DEFAULT : 'default';
PACKED_STRUCT : 'packed_struct';
ONCE : 'once';

// Other tokens.
STRING_LITERAL : UNTERMINATED_STRING_LITERAL '"';
fragment UNTERMINATED_STRING_LITERAL : '"' (~["\\\r\n] | '\\' (. | EOF))*;
IDENT : [_a-zA-Z][_a-zA-Z0-9]*;
VAR_IDENT : ([_a-zA-Z] | VAR_REF) ([_a-zA-Z0-9] | VAR_REF)*;
fragment VAR_REF: '$(' IDENT ')';
HEX_NUMBER: '0x' [0-9a-fA-F][0-9a-fA-F']*;
BIN_NUMBER: '0b'[01]([01'])*;
OCT_NUMBER: '0'[0-7']*;
DEC_NUMBER: '0' | [1-9][0-9']*;
DOTDOT : '..' ;

BLOCK_COMMENT : '/*' .*? '*/' -> channel(HIDDEN);
LINE_COMMENT : '//' ~[\n\r]* -> channel(HIDDEN);
WS : [ \t\r\n] -> channel(HIDDEN) ;
