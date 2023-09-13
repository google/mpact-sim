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

#ifndef MPACT_SIM_DECODER_BIN_FORMAT_VISITOR_H_
#define MPACT_SIM_DECODER_BIN_FORMAT_VISITOR_H_

#include <cstdint>
#include <deque>
#include <list>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "antlr4-runtime/ParserRuleContext.h"
#include "mpact/sim/decoder/BinFormatLexer.h"
#include "mpact/sim/decoder/antlr_parser_wrapper.h"
#include "mpact/sim/decoder/bin_encoding_info.h"
#include "mpact/sim/decoder/bin_format_contexts.h"
#include "mpact/sim/decoder/decoder_error_listener.h"
#include "re2/re2.h"

namespace mpact {
namespace sim {
namespace decoder {
namespace bin_format {

// This struct holds information about a range assignment in an instruction
// generator.
struct RangeAssignmentInfo {
  std::vector<std::string> range_names;
  std::list<RE2> range_regexes;
  std::vector<std::vector<std::string>> range_values;
};

struct BinaryNum {
  int64_t value;
  int width;
};

struct BitRange {
  int first;
  int last;
};

class Format;
class Overlay;
class InstructionEncoding;
class InstructionGroup;

using ::mpact::sim::decoder::bin_format::generated::BinFormatLexer;

using BinFmtAntlrParserWrapper =
    decoder::AntlrParserWrapper<BinFormatParser, BinFormatLexer>;

class BinFormatVisitor {
 public:
  struct StringPair {
    std::string h_output;
    std::string cc_output;
  };

  BinFormatVisitor() = default;
  ~BinFormatVisitor();

  // Entry point for processing a source_stream input, generating any output
  // files in the given directory. Returns OK if no errors were encountered.
  absl::Status Process(const std::vector<std::string> &file_names,
                       const std::string &decoder_name,
                       absl::string_view prefix,
                       const std::vector<std::string> &include_roots,
                       absl::string_view directory);

 private:
  // Check the encodings to make sure there aren't any obvious errors.
  void PerformEncodingChecks(BinEncodingInfo *encoding);
  // Called to generate and emit code for the decoder according to the parsed
  // input file.
  StringPair EmitCode(BinEncodingInfo *encoding);
  StringPair EmitFilePrefix(const std::string &dot_h_name,
                            BinEncodingInfo *encoding_info);
  StringPair EmitFileSuffix(const std::string &dot_h_name,
                            BinEncodingInfo *encoding_info);
  // Utility methods to parse certain nodes.
  BinaryNum ParseBinaryNum(TerminalNode *node);
  BitRange GetBitIndexRange(BitIndexRangeCtx *ctx);
  int ConvertToInt(NumberCtx *ctx);
  // Methods that visit the nodes of the parse tree.
  void VisitTopLevel(TopLevelCtx *ctx);
  std::unique_ptr<BinEncodingInfo> ProcessTopLevel(
      const std::string &decoder_name);
  void PreProcessDeclarations(DeclarationListCtx *ctx);
  void VisitDeclarations(DeclarationListCtx *ctx,
                         BinEncodingInfo *encoding_info);
  void VisitFormatDef(FormatDefCtx *ctx, BinEncodingInfo *encoding_info);
  void VisitFieldDef(FieldDefCtx *ctx, Format *format,
                     BinEncodingInfo *encoding_info);
  void VisitIncludeFile(IncludeFileCtx *ctx);
  void ParseIncludeFile(antlr4::ParserRuleContext *ctx,
                        const std::string &file_name,
                        const std::vector<std::string> &dirs);
  void VisitOverlayDef(OverlayDefCtx *ctx, Format *format);
  void VisitOverlayBitField(BitFieldCtx *ctx, Overlay *overlay);
  InstructionGroup *VisitInstructionGroupDef(InstructionGroupDefCtx *ctx,
                                             BinEncodingInfo *encoding_info);
  std::unique_ptr<BinEncodingInfo> VisitDecoderDef(DecoderDefCtx *ctx);
  void VisitInstructionDef(InstructionDefCtx *ctx,
                           InstructionGroup *inst_group);
  void ProcessInstructionDefGenerator(InstructionDefCtx *ctx,
                                      InstructionGroup *inst_group);
  std::string GenerateInstructionDefList(
      const std::vector<RangeAssignmentInfo *> &range_info_vec, int index,
      const std::string &template_str_in) const;
  void VisitConstraint(FieldConstraintCtx *ctx,
                       InstructionEncoding *inst_encoding);

  // Accessors.
  decoder::DecoderErrorListener *error_listener() const {
    return error_listener_.get();
  }
  void set_error_listener(
      std::unique_ptr<decoder::DecoderErrorListener> listener) {
    error_listener_ = std::move(listener);
  }

  // This stores a vector of include file root directories.
  std::vector<std::string> include_dir_vec_;
  // Keep track of files that are included in case there is recursive includes.
  std::deque<std::string> include_file_stack_;
  // Error listening object passed to the parser.
  std::unique_ptr<decoder::DecoderErrorListener> error_listener_ = nullptr;
  std::string decoder_name_;
  // Maps from identifiers to declaration contexts.
  absl::flat_hash_map<std::string, FormatDefCtx *> format_decl_map_;
  absl::flat_hash_map<std::string, InstructionGroupDefCtx *> group_decl_map_;
  absl::flat_hash_map<std::string, DecoderDefCtx *> decoder_decl_map_;
  // AntlrParserWrapper vector.
  std::vector<BinFmtAntlrParserWrapper *> antlr_parser_wrappers_;
};

}  // namespace bin_format
}  // namespace decoder
}  // namespace sim
}  // namespace mpact

#endif  // MPACT_SIM_DECODER_BIN_FORMAT_VISITOR_H_
