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

#ifndef MPACT_SIM_DECODER_BIN_FORMAT_CONTEXTS_H_
#define MPACT_SIM_DECODER_BIN_FORMAT_CONTEXTS_H_

#include "mpact/sim/decoder/BinFormatParser.h"

namespace mpact {
namespace sim {
namespace decoder {
namespace bin_format {

// This file provides short(er) hand notations for the parser contexts.

using BinFormatParser =
    ::mpact::sim::decoder::bin_format::generated::BinFormatParser;

using BitFieldCtx = BinFormatParser::Bit_field_specContext;
using BitIndexRangeCtx = BinFormatParser::Bit_index_rangeContext;
using DecoderDefCtx = BinFormatParser::Decoder_defContext;
using DeclarationListCtx = BinFormatParser::Declaration_listContext;
using FieldConstraintCtx = BinFormatParser::Field_constraintContext;
using FieldDefCtx = BinFormatParser::Field_defContext;
using FormatDefCtx = BinFormatParser::Format_defContext;
using GroupNameListCtx = BinFormatParser::Group_name_listContext;
using IncludeFileCtx = BinFormatParser::Include_fileContext;
using InstructionGroupDefCtx = BinFormatParser::Instruction_group_defContext;
using InstructionDefCtx = BinFormatParser::Instruction_defContext;
using NamespaceCtx = BinFormatParser::Namespace_declContext;
using NumberCtx = BinFormatParser::NumberContext;
using OnceCtx = BinFormatParser::OnceContext;
using OverlayDefCtx = BinFormatParser::Overlay_defContext;
using TopLevelCtx = BinFormatParser::Top_levelContext;

using TerminalNode = antlr4::tree::TerminalNode;

}  // namespace bin_format
}  // namespace decoder
}  // namespace sim
}  // namespace mpact

#endif  // MPACT_SIM_DECODER_BIN_FORMAT_CONTEXTS_H_
