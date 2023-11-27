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

#ifndef MPACT_SIM_DECODER_PROTO_FORMAT_CONTEXTS_H_
#define MPACT_SIM_DECODER_PROTO_FORMAT_CONTEXTS_H_

// This file defines more convenient type aliases for parser context types.

#include "mpact/sim/decoder/ProtoFormatLexer.h"
#include "mpact/sim/decoder/ProtoFormatParser.h"

namespace mpact {
namespace sim {
namespace decoder {
namespace proto_fmt {

using ConstraintExprCtx = generated::ProtoFormatParser::Constraint_exprContext;
using DeclarationCtx = generated::ProtoFormatParser::DeclarationContext;
using DecoderAttributeCtx =
    generated::ProtoFormatParser::Decoder_attributeContext;
using DecoderDefCtx = generated::ProtoFormatParser::Decoder_defContext;
using FieldConstraintCtx =
    generated::ProtoFormatParser::Field_constraintContext;
using IfNotCtx = generated::ProtoFormatParser::If_notContext;
using IncludeFileCtx = generated::ProtoFormatParser::Include_fileContext;
using InstructionDefCtx = generated::ProtoFormatParser::Instruction_defContext;
using InstructionGroupDefCtx =
    generated::ProtoFormatParser::Instruction_group_defContext;
using NumberCtx = generated::ProtoFormatParser::NumberContext;
using QualifiedIdentCtx = generated::ProtoFormatParser::Qualified_identContext;
using SetterDefCtx = generated::ProtoFormatParser::Setter_defContext;
using SetterGroupDefCtx = generated::ProtoFormatParser::Setter_group_defContext;
using SetterRefCtx = generated::ProtoFormatParser::Setter_refContext;
using TopLevelCtx = generated::ProtoFormatParser::Top_levelContext;
using ValueCtx = generated::ProtoFormatParser::ValueContext;

}  // namespace proto_fmt
}  // namespace decoder
}  // namespace sim
}  // namespace mpact

#endif  // MPACT_SIM_DECODER_PROTO_FORMAT_CONTEXTS_H_
