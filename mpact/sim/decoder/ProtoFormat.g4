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

grammar ProtoFormat;

top_level
  : declaration* EOF
  ;

declaration
  : decoder_def
  | instruction_group_def
  | include_file
  | using_alias
  ;

// Declare a using alias for the right hand side qualified identifier. The
// identifier must either be a message, a oneof, a field, or an enum value.

using_alias
  : USING (IDENT '=')? qualified_ident ';'
  ;

instruction_group_def
  : INSTRUCTION GROUP name=IDENT ':' message_name=qualified_ident
   '{' (setter_group_def | instruction_def)* '}' ';'?
  ;

setter_group_def
  : SETTER name=IDENT ':' setter_def*
  ;

instruction_def
  : name=IDENT ':' field_constraint_list ';' (setter_def | setter_ref)*
  | generate=GENERATE '(' range_assignment (',' range_assignment)* ')'
    '{' generator_instruction_def_list '}' ';' (setter_def | setter_ref)*
  ;

range_assignment
  : IDENT '=' '[' gen_value (',' gen_value)* ']'
  | '[' IDENT (',' IDENT)* ']' '=' '[' value_list (',' value_list)* ']'
  ;

value_list
  : '{' gen_value (',' gen_value)* '}'
  ;

gen_value
  : value
  | IDENT
  ;

value
  : number
  | bool_value
  | string=STRING_LITERAL
  ;

generator_instruction_def_list
  : generator_instruction_def*
  ;

generator_instruction_def
  : name=qualified_var_ident ':' sub_message=qualified_var_ident? ':'
    generator_field_constraint (',' generator_field_constraint)* ';'
  ;

generator_field_constraint
  : qualified_var_ident constraint_op (STRING_LITERAL | number | VAR_IDENT)
  ;

field_constraint_list
  : field_constraint (',' field_constraint)*
  ;

field_constraint
  : field=qualified_ident constraint_op constraint_expr
  | HAS '(' qualified_ident ')'
  ;

constraint_op
  : '=='
  | '!='
  | '>'
  | '>='
  | '<'
  | '<='
  ;

// Currently the constraint expr is very simple. Either a literal or an
// enumeration value. In the future, this may be changed to a more general
// expression.
constraint_expr
  : value
  | qualified_ident
  ;

setter_def
  : name=IDENT '=' qualified_ident if_not? ';'
  ;

setter_ref
  : name=IDENT ';'
  ;

if_not
  :  '||' (value | qualified_ident)
  ;

decoder_def
  : DECODER name=IDENT '{' decoder_attribute* '}' ';'?
  ;

decoder_attribute
  : include_files
  | opcode_enum_decl
  | namespace_decl
  | group_name
  ;

namespace_decl
  : NAMESPACE namespace_ident ( '::' namespace_ident) * ';'
  ;

opcode_enum_decl
  : OPCODE_ENUM '=' STRING_LITERAL ';'
  ;

include_files
  : INCLUDE_FILES '{' include_file* '}'
  ;

include_file
  : INCLUDE STRING_LITERAL
  ;

group_name
  : IDENT ';'
  | IDENT '=' '{' ident_list '}' ';'
  ;

ident_list
  : IDENT (',' IDENT)* 
  ;

number
  : HEX_NUMBER | DEC_NUMBER
  ;

namespace_ident
  : IDENT | DECODER | GROUP | INSTRUCTION
  ;

qualified_ident
  : field_ident ('.' field_ident) *
  ;

qualified_var_ident
  : field_var_ident ('.' field_var_ident) *
  ;

field_var_ident
  : field_ident | VAR_IDENT
  ;

bool_value
  : TRUE
  | FALSE
  ;

field_ident
  : IDENT | DECODER | GROUP | INSTRUCTION
  ;

//
// Lexer specification
//

// Reserved words.

NAMESPACE : 'namespace';
HAS : 'HAS';
INCLUDE : '#include';
INCLUDE_FILES : 'includes';
OPCODE_ENUM : 'opcode_enum';
GENERATE: 'GENERATE';
NAMESPACES : 'namespaces';
INSTRUCTION : 'instruction';
GROUP : 'group';
USING : 'using';
DECODER : 'decoder';
SETTER : 'setter';

// Other tokens.
TRUE : 'true';
FALSE : 'false';
STRING_LITERAL : UNTERMINATED_STRING_LITERAL '"';
fragment UNTERMINATED_STRING_LITERAL : '"' (~["\\\r\n] | '\\' (. | EOF))*;
IDENT : [_a-zA-Z][_a-zA-Z0-9]*;
VAR_IDENT : ([_a-zA-Z] | VAR_REF) ([_a-zA-Z0-9] | VAR_REF)*;
fragment VAR_REF: '$(' IDENT ')';
fragment SUFFIX: ([uU]?('l'| 'L' | 'll' | 'LL')?) |
                 (('l' | 'L' | 'll' | 'LL')?[uU]?);
HEX_NUMBER: '0x' [0-9a-fA-F][0-9a-fA-F']*SUFFIX?;
DEC_NUMBER: '0' | [1-9][0-9']*SUFFIX?;
DOTDOT : '..' ;

BLOCK_COMMENT : '/*' .*? '*/' -> channel(HIDDEN);
LINE_COMMENT : '//' ~[\n\r]* -> channel(HIDDEN);
WS : [ \t\r\n] -> channel(HIDDEN) ;

