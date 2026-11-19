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

// This ANTLR4 file specifies the binary encoding of an instruction set
// architecture (.bin_fmt files) for MPACT-Sim's binary format code generator
// (bin_format_gen).
//
// A .bin_fmt description consists of four main kinds of declarations:
//
// 1. Preprocessor-style directives:
//    - An optional '#once' guard at the top of a file to prevent multiple
//      inclusions when modularizing descriptions across files.
//    - '#include "..."' directives at top level to import other .bin_fmt files.
//
// 2. Instruction formats ('format'):
//    - Define fixed-width bit layouts (from small 8/16/32/64-bit instruction
//      words up to 128-bit and arbitrary multi-word VLIW bundles).
//    - List 'fields' from most-significant bit (MSB) to least-significant bit
//      (LSB, bit 0), where each entry is either a signed/unsigned bitfield or a
//      nested (and optionally replicated) sub-format.
//    - Optionally define 'overlays' (virtual bitfields up to 64 bits wide)
//      assembled by concatenating fields, field/format bit slices, and binary
//      literals.
//    - Optionally inherit from a base format (': BaseFormat') to inherit width,
//      share non-conflicting extractors, and group formats into a hierarchy.
//    - Optionally specify 'layout: packed_struct;' to emit C++ packed bitfield
//      structs and unions alongside functional extractors/inserters.
//
// 3. Instruction groups ('instruction group'):
//    - Group instruction encodings that share a bit width (up to 64 bits,
//      either declared explicitly or inferred from the group's base format)
//      and whose formats match or derive from a common base format, or merge
//      existing compatible instruction groups ('= { Group1, Group2 }').
//    - Each instruction encoding constrains fields or overlays using relational
//      operators ('==', '!=', '>', '>=', '<', '<=') against numeric literals or
//      other fields/overlays. Encodings also support duplicate-opcode markers
//      ('!'), Cartesian-product template expansion ('GENERATE'), and encoding
//      specializations ('specializes').
//
// 4. Decoder specifications ('decoder'):
//    - Define code-generation targets naming the C++ 'namespace', the target
//      'opcode_enum' type (generated from the corresponding .isa file), C++
//      header 'includes', and the instruction groups (or merged groups) to
//      generate decode functions for.
//
// From a .bin_fmt file, bin_format_gen generates:
//   - <prefix>_bin_decoder.{h,cc}: Decode<Group>() and
//     Decode<Group>WithFormat() functions mapping instruction words to
//     OpcodeEnum (and FormatEnum), plus inline bitfield/sub-format/overlay
//     extractor functions (both in per-format namespaces and in
//     Extractors::<Format> classes).
//   - <prefix>_bin_encoder.{h,cc}: Bitfield/sub-format/overlay inserter
//     functions (Encoder::<Format>) and fixed-bit opcode encoding tables
//     (kOpcodeEncodings).
//   - <prefix>_bin_types.h: FormatEnum and any requested Packed<Format> /
//     Union<Format> C++ bitfield types.

grammar BinFormat;

// Top-level file rule: an optional '#once' include guard followed by zero or
// more decoder, format, instruction group, or '#include' declarations.
top_level
  : once? declaration_list EOF
  ;

// Prevents the current .bin_fmt file from being included more than once.
once
  : '#' ONCE
  ;

declaration_list
  : (decoder_def | declaration)*
  ;

declaration
  : format_def | instruction_group_def | include_file
  ;

// Defines an instruction format.
// - 'width' ('[bits]') may be omitted only if 'inherits_from' (': BaseFormat')
//   is specified, in which case the width is inherited from BaseFormat (if both
//   are given, they must match).
// - Inheritance groups formats under a common ancestor so they can belong to
//   the same 'instruction group', and promotes non-conflicting field/overlay
//   extractors up to ancestor format namespaces.
// - Formats up to 64 bits pass values as uint8_t/uint16_t/uint32_t/uint64_t,
//   formats of 65..128 bits use absl::uint128, and formats wider than 128 bits
//   operate on byte buffers (const uint8_t* / uint8_t*). Formats up to 128
//   bits whose bit width is not a power of two generate both integer and
//   byte-pointer extractors.
format_def
  : FORMAT name=IDENT width=index? inherits_from? 
    '{' layout_spec? format_field_defs'}' ';'?
  ;

inherits_from
  : ':' IDENT
  ;

// Optional C++ representation layout for the format:
//   - 'default': Generates functional extractors and inserters only.
//   - 'packed_struct': Also emits Packed<Format> (ABSL_ATTRIBUTE_PACKED C++
//     bitfield struct) and Union<Format> in <prefix>_bin_types.h. Supported
//     when all fields/sub-formats are <= 64 bits and no sub-format uses an
//     array count.
layout_spec
  : LAYOUT ':' layout_type ';'
  ;

layout_type
  : DEFAULT
  | PACKED_STRUCT
  ;

// Each format consists of a sequence of field/sub-format entries whose widths
// sum up to the format width, followed by an optional 'overlays:' section.
// Entries are ordered from MSB (left) to LSB (right) according to declaration
// order. Multiple consecutive instances of a sub-format may be expressed using
// array notation ('format SubFmt slot[N];'), ordered left-to-right from
// instance 0 to N-1. Within every format, field, or sub-format, bit 0 is the
// LSB and bit (width - 1) is the MSB.
format_field_defs
  : FIELDS ':' field_def* (OVERLAYS ':' overlay_def*)?
  ;

// A field is either:
//   1. A named 'signed' or 'unsigned' bitfield of 'width' bits (up to 64 bits
//      if extracted directly), optionally with a '= number' default value.
//      Signed fields are sign-extended by their generated extractor.
//   2. A reference to another format ('format FormatName field_name[count]?;'),
//      creating a format hierarchy (e.g., slots inside a VLIW bundle). Directly
//      extracted sub-formats must be <= 128 bits wide.
field_def
  : sign_spec field_name=IDENT width=index field_assign? ';'
  | FORMAT format_name=IDENT field_name=IDENT index? ';'
  ;

field_assign
  : '=' number
  ;

// An overlay defines a virtual 'signed' or 'unsigned' bitfield (up to 64 bits
// wide) formed by concatenating 'bit_field_list' items from MSB (left) to LSB
// (right). The sum of the component widths must equal 'width'. Signed overlays
// are sign-extended by their generated extractor. Binary literal components
// contribute constant bits during extraction (and are skipped during bit
// insertion).
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

// Each component of an overlay is one of:
//   - A full named field declared in 'fields:' (e.g., 'imm5').
//   - One or more bit ranges/indices of a named field (e.g., 'imm7[6, 4..0]').
//   - One or more bit ranges/indices directly from the enclosing format
//     (e.g., '[31, 7, 30..25]'; also used to slice into nested sub-formats).
//   - A binary number literal (e.g., '0b0' or '0b0100\'1010'), whose bit width
//     equals the number of binary digits following '0b' (excluding '\'').
bit_field_spec
  : IDENT bit_range_list?
  | bit_range_list
  | bin_number
  ;

bin_number
  : BIN_NUMBER
  ;

// Bit ranges specify descending ('hi..lo', with hi >= lo) or single-bit
// ('bit') slices within the target field or enclosing format.
bit_range_list
  : '[' bit_index_range (',' bit_index_range ) * ']'
  ;

bit_index_range
  : number ( DOTDOT number)?
  ;

// Defines either:
//   1. A single instruction group of instructions (up to 64 bits wide) whose
//      encoding formats are 'inst_format' or derive from 'inst_format'. The
//      explicit bit width ('[number]') is optional; when omitted, the group's
//      width is inferred from 'inst_format' (if both are given, they must
//      match).
//   2. A composite instruction group formed by merging two or more previously
//      defined instruction groups that share the same width and base format.
instruction_group_def
  : INSTRUCTION GROUP name=IDENT width=index? ':' inst_format=IDENT
    '{' instruction_def_list '}' ';'?
  | INSTRUCTION GROUP name=IDENT '=' '{' group_name_list '}' ';'?
  ;

instruction_def_list
  : instruction_def*
  ;

// Specifies one or more instruction encodings within an instruction group:
//   1. Standard encoding: optional '!' ('duplicate'), opcode 'name' (matching
//      an opcode in the .isa file, converted to PascalCase 'k<Name>' in
//      OpcodeEnum), format name, and comma-separated field/overlay constraints.
//   2. 'GENERATE(...) { ... };': Expands the body across the Cartesian product
//      of all 'range_assignment' variable/tuple lists, substituting '$(var)'
//      references in instruction names, format names, and constraints.
//   3. Specialization ('name : specializes parent : ...;'): Defines a more
//      specific encoding of 'parent' (in the same group), inheriting 'parent's
//      format and constraints and adding further constraints tested when
//      'parent' matches.
instruction_def
  : duplicate? name=IDENT ':' format_name=IDENT ':' field_constraint_list ';'
  | generate=GENERATE '(' range_assignment (',' range_assignment)* ')'
    '{' generator_instruction_def_list '}' ';'
  | name=IDENT ':' SPECIALIZES parent=IDENT ':' field_constraint_list ';'
  ;

// Prefixing an instruction encoding with '!' suppresses the warning normally
// emitted when multiple encodings in a group share the same opcode name.
duplicate
  : '!'
  ;

// Binds either a single variable ('var = [v1, v2, ...]') or a synchronized
// tuple of variables ('[v1, v2] = [{a1, b1}, {a2, b2}, ...]') for GENERATE.
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

// Constrains a field or overlay ('field_name') in the encoding's format using a
// relational operator against either a numeric literal ('value') or another
// field/overlay ('rhs_field_name') in the same format (e.g., 'rs1 != rs2').
// Signed fields and overlays support '==' and '!=' against constants/fields;
// unsigned fields and overlays support all six relational operators.
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

// A decoder definition specifies a named target for binary decoder/encoder code
// generation, including its C++ namespace, OpcodeEnum type, header includes,
// and the instruction groups (or merged groups) to generate decode functions
// for. Multiple decoders may be declared; if more than one is present, pass
// '--decoder_name=<name>' to bin_format_gen to select which one to generate.
decoder_def
  : DECODER name=IDENT '{'  decoder_attribute* '}' ';'?
  ;

decoder_attribute
  : include_files
  | opcode_enum_decl
  | namespace_decl
  | group_name
  ;


// Specifies the C++ namespace in which the generated decoder, extractors,
// encoders, and format types will be placed (at most one per decoder block).
namespace_decl
  : NAMESPACE namespace_ident ( '::' namespace_ident) * ';'
  ;

// Specifies the C++ OpcodeEnum type name returned by generated Decode<Group>()
// functions (typically the enum generated from the corresponding .isa file).
// Required inside every decoder block.
opcode_enum_decl
  : OPCODE_ENUM '=' STRING_LITERAL ';'
  ;

// C++ '#include' directives to emit into the generated decoder header (for
// example, the '*_enums.h' header defining OpcodeEnum).
include_files
  : INCLUDE_FILES '{' include_file* '}' ';'?
  ;

// At top level, imports another .bin_fmt file. Inside 'includes { ... }' in a
// decoder block, emits a C++ '#include' line into the generated header.
include_file
  : INCLUDE STRING_LITERAL
  ;

// References an existing instruction group to generate a decoder for, or
// defines a new merged instruction group inline from two or more compatible
// groups listed in braces.
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

// Identifiers, '$(var)' generator references, strings, numeric literals (which
// allow single-quote '\'' digit separators), range operator, and comments.
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
