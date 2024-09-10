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

#include "mpact/sim/decoder/bin_format_visitor.h"

#include <cstdint>
#include <deque>
#include <fstream>
#include <iostream>
#include <istream>
#include <list>
#include <memory>
#include <new>
#include <string>
#include <utility>
#include <vector>

#include "absl/container/btree_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "antlr4-runtime/ParserRuleContext.h"
#include "mpact/sim/decoder/bin_decoder.h"
#include "mpact/sim/decoder/bin_encoding_info.h"
#include "mpact/sim/decoder/bin_format_contexts.h"
#include "mpact/sim/decoder/decoder_error_listener.h"
#include "mpact/sim/decoder/format.h"
#include "mpact/sim/decoder/format_name.h"
#include "mpact/sim/decoder/instruction_encoding.h"
#include "mpact/sim/decoder/instruction_group.h"
#include "mpact/sim/decoder/overlay.h"
#include "re2/re2.h"

namespace mpact {
namespace sim {
namespace decoder {
namespace bin_format {

using mpact::sim::machine_description::instruction_set::ToPascalCase;

constexpr char kTemplatedExtractBits[] = R"foo(
namespace internal {

// This function extracts a bitfield width bits wide from the byte vector,
// starting at bit_index bits from the end of data. The lsb has index 0. The
// byte vector is data_size bytes long. There is no error checking that T
// can accommodate width bits.
template <typename T>
static inline T ExtractBits(const uint8_t *data, int data_size, int msb,
                            int width) {
  if (width == 0) return 0;

  int byte_low = data_size - ((msb - width) >> 3) - 1;
  int byte_high = data_size - (msb >> 3) - 1;
  int high_bit = msb & 0x7;

  // If it is only from one byte, extract and return.
  if (byte_low == byte_high) {
    uint8_t mask = (1 << (high_bit + 1)) - 1;
    return (mask & data[byte_high]) >> (high_bit - width + 1);
  }

  // Extract from the high order byte.
  T val = 0;
  uint8_t mask = 0xff >> (7 - high_bit);
  val = (mask & data[byte_high++]);
  int remainder = width - (1 + high_bit);

  while (remainder >= 8) {
    val = (val << 8) | data[byte_high++];
    remainder -= 8;
  }

  // Extract any remaining bits from the high end of the last byte.
  if (remainder > 0) {
    val <<= remainder;
    int shift = 8 - remainder;
    uint8_t mask = 0xff << shift;
    val |= (data[byte_high] & mask) >> shift;
  }
  return val;
}

}  // namespace internal

)foo";

BinFormatVisitor::~BinFormatVisitor() {
  for (auto *wrapper : antlr_parser_wrappers_) {
    delete wrapper;
  }
  antlr_parser_wrappers_.clear();
}

using ::mpact::sim::machine_description::instruction_set::ToHeaderGuard;

absl::Status BinFormatVisitor::Process(
    const std::vector<std::string> &file_names, const std::string &decoder_name,
    absl::string_view prefix, const std::vector<std::string> &include_roots,
    absl::string_view directory) {
  decoder_name_ = decoder_name;

  include_dir_vec_.push_back(".");
  for (const auto &root : include_roots) {
    include_dir_vec_.push_back(root);
  }

  std::istream *source_stream = &std::cin;

  if (!file_names.empty()) {
    source_stream = new std::fstream(file_names[0], std::fstream::in);
  }
  // Create an antlr4 stream from the input stream.
  BinFmtAntlrParserWrapper parser_wrapper(source_stream);

  // Create and add the error listener.
  set_error_listener(std::make_unique<decoder::DecoderErrorListener>());
  error_listener_->set_file_name(file_names[0]);
  file_names_.push_back(file_names[0]);
  parser_wrapper.parser()->removeErrorListeners();
  parser_wrapper.parser()->addErrorListener(error_listener());

  // Parse the file and then create the data structures.
  TopLevelCtx *top_level = parser_wrapper.parser()->top_level();
  (void)top_level;
  if (!file_names.empty()) {
    delete source_stream;
    source_stream = nullptr;
  }

  if (error_listener_->HasError() > 0) {
    return absl::InternalError("Errors encountered - terminating.");
  }
  // Visit the parse tree starting at the namespaces declaration.
  PreProcessDeclarations(top_level->declaration_list());
  // Process additional source files.
  for (int i = 1; i < file_names.size(); ++i) {
    ParseIncludeFile(top_level, file_names[i], {});
  }
  // Process the parse tree.
  auto encoding_info = ProcessTopLevel(decoder_name);
  // Include files may generate additional syntax errors.
  if (encoding_info == nullptr) {
    LOG(ERROR) << "No encoding specified";
    return absl::InternalError("No encoding specified");
  }
  PerformEncodingChecks(encoding_info.get());
  // Fail if there are errors.
  if (error_listener_->HasError() > 0) {
    return absl::InternalError("Errors encountered - terminating.");
  }

  // Create output streams for .h and .cc files.
  std::string dot_h_name = absl::StrCat(prefix, "_bin_decoder.h");
  std::string dot_cc_name = absl::StrCat(prefix, "_bin_decoder.cc");
  std::ofstream dot_h_file(absl::StrCat(directory, "/", dot_h_name));
  std::ofstream dot_cc_file(absl::StrCat(directory, "/", dot_cc_name));

  auto [h_output, cc_output] = EmitFilePrefix(dot_h_name, encoding_info.get());
  dot_h_file << h_output;
  dot_cc_file << cc_output;
  // Output file prefix is the input file name.
  auto [h_output2, cc_output2] = EmitCode(encoding_info.get());
  dot_h_file << h_output2;
  dot_cc_file << cc_output2;
  auto [h_output3, cc_output3] =
      EmitFileSuffix(dot_h_name, encoding_info.get());
  dot_h_file << h_output3;
  dot_cc_file << cc_output3;
  dot_h_file.close();
  dot_cc_file.close();
  return absl::OkStatus();
}

void BinFormatVisitor::PerformEncodingChecks(BinEncodingInfo *encoding) {
  encoding->decoder()->CheckEncodings();
}

BinFormatVisitor::StringPair BinFormatVisitor::EmitFilePrefix(
    const std::string &dot_h_name, BinEncodingInfo *encoding_info) {
  std::string h_string;
  std::string cc_string;

  std::string guard_name = ToHeaderGuard(dot_h_name);
  absl::StrAppend(&h_string, "#ifndef ", guard_name,
                  "\n"
                  "#define ",
                  guard_name,
                  "\n"
                  "\n"
                  "#include <iostream>\n"
                  "#include <cstdint>\n"
                  "\n"
                  "#include \"absl/functional/any_invocable.h\"\n"
                  "\n\n");
  for (auto const &include_file : encoding_info->include_files()) {
    absl::StrAppend(&h_string, "#include ", include_file, "\n");
  }
  absl::StrAppend(&h_string, "\n");
  absl::StrAppend(&cc_string, "#include \"", dot_h_name, "\"\n\n");
  for (auto &name_space : encoding_info->decoder()->namespaces()) {
    auto name_space_str = absl::StrCat("namespace ", name_space, " {\n");
    absl::StrAppend(&h_string, name_space_str);
    absl::StrAppend(&cc_string, name_space_str);
  }
  absl::StrAppend(&h_string, "\n");
  absl::StrAppend(&cc_string, "\n");
  // Write out the templated extractor function used by the other methods.
  absl::StrAppend(&h_string, kTemplatedExtractBits);
  // Write out the instruction format enum.
  absl::StrAppend(&h_string,
                  "\n"
                  "enum class FormatEnum {\n"
                  "  kNone = 0,\n");
  int i = 1;
  for (auto &[name, unused] : encoding_info->format_map()) {
    absl::StrAppend(&h_string, "  k", ToPascalCase(name), " = ", i++, ",\n");
  }
  absl::StrAppend(&h_string, "};\n\n");
  return {h_string, cc_string};
}

BinFormatVisitor::StringPair BinFormatVisitor::EmitFileSuffix(
    const std::string &dot_h_name, BinEncodingInfo *encoding_info) {
  std::string h_string;
  std::string cc_string;

  absl::StrAppend(&h_string, "\n");
  absl::StrAppend(&cc_string, "\n");
  auto &namespaces = encoding_info->decoder()->namespaces();
  for (auto rptr = namespaces.rbegin(); rptr != namespaces.rend(); rptr++) {
    std::string name_space = absl::StrCat("}  // namespace ", *rptr, "\n");
    absl::StrAppend(&h_string, name_space);
    absl::StrAppend(&cc_string, name_space);
  }
  std::string guard_name = ToHeaderGuard(dot_h_name);
  absl::StrAppend(&h_string, "\n#endif // ", guard_name);
  return {h_string, cc_string};
}

BinFormatVisitor::StringPair BinFormatVisitor::EmitCode(
    BinEncodingInfo *encoding) {
  std::string h_string;
  std::string cc_string;
  std::string group_string;
  std::string extractor_class =
      absl::StrCat("class Extractors {\n", "public: \n");
  // Write out the inline functions for bitfield and overlay extractions.
  for (auto &[unused, format_ptr] : encoding->format_map()) {
    auto [functions, classes] = format_ptr->GenerateExtractors();
    absl::StrAppend(&h_string, functions);
    absl::StrAppend(&extractor_class, classes);
  }
  absl::StrAppend(&h_string, extractor_class, "};\n\n");
  absl::flat_hash_set<std::string> groups;
  auto *decoder = encoding->decoder();
  // Generate the code for decoders.
  for (auto *group : decoder->instruction_group_vec()) {
    auto [h_decoder, cc_decoder] = group->EmitCode();
    absl::StrAppend(&h_string, h_decoder);
    absl::StrAppend(&cc_string, cc_decoder);
    // Write out some summary information about the instruction encodings.
    // Useful for now to ensure that the info is correct.
    absl::StrAppend(&group_string, group->WriteGroup());
  }
  absl::StrAppend(&h_string, group_string);
  return {h_string, cc_string};
}

// Parse the range and convert to a BitRange.
BitRange BinFormatVisitor::GetBitIndexRange(BitIndexRangeCtx *ctx) {
  int start = ConvertToInt(ctx->number(0));
  int stop = start;
  if (ctx->number().size() == 2) {
    stop = ConvertToInt(ctx->number(1));
  }
  return BitRange{start, stop};
}

// Parse a binary number string such as 0b1010'0111 and return a BinaryNum
// to encode the value and width.
BinaryNum BinFormatVisitor::ParseBinaryNum(TerminalNode *node) {
  std::string bin_str = node->getText();
  if (bin_str.substr(0, 2) != "0b") {
    error_listener_->semanticError(node->getSymbol(),
                                   "Illegal binary number string");
    return BinaryNum{/*value=*/0, /*width=*/-1};
  }
  auto digits = bin_str.substr(2);
  int64_t value = 0;
  int width = 0;
  for (auto d : digits) {
    if (d == '\'') continue;
    if ((d != '1') && (d != '0')) {
      error_listener_->semanticError(node->getSymbol(),
                                     "Illegal binary number string");
      return BinaryNum{/*value=*/0, /*width=*/-1};
    }
    value <<= 1;
    value |= d - '0';
    width++;
  }
  return BinaryNum{value, width};
}

// Parse a number string and return the value.
int BinFormatVisitor::ConvertToInt(NumberCtx *ctx) {
  // Binary has to be handled separately.
  auto bin_number = ctx->BIN_NUMBER();
  if (bin_number != nullptr) {
    return static_cast<int>(ParseBinaryNum(bin_number).value);
  }
  // Hex, octal and decimal.
  int ret_val = std::stoi(ctx->getText(), nullptr, 0);
  return ret_val;
}

std::unique_ptr<BinEncodingInfo> BinFormatVisitor::ProcessTopLevel(
    const std::string &decoder_name) {
  // At this point we have the contexts for all slots, bundles and isas.
  // First make sure the named isa (decoder) has been defined.
  auto decoder_iter = decoder_decl_map_.find(decoder_name);
  if (decoder_iter == decoder_decl_map_.end()) {
    error_listener()->semanticError(
        nullptr, absl::StrCat("No decoder '", decoder_name, "' declared"));
    return nullptr;
  }
  // Visit the decoder.
  auto bin_encoding_info = VisitDecoderDef(decoder_iter->second);
  // Now visit any formats that references any formats that have been visited.
  // Formats are visited lazily, so only those "reachable" from the decoder
  // will have been visited. Build a multi-map from referenced format to parent
  // format.
  absl::btree_multimap<std::string, std::string> reference_map;
  for (auto &[format_name, ctx_ptr] : format_decl_map_) {
    // Skip those that have already been visited.
    if (bin_encoding_info->GetFormat(format_name) != nullptr) continue;
    for (auto *field_ctx : ctx_ptr->format_field_defs()->field_def()) {
      if (field_ctx->format_name != nullptr) {
        reference_map.emplace(field_ctx->format_name->getText(), format_name);
      }
    }
  }
  // Now, starting at each visited format, traverse links in the reference_map
  // to transitively visit any "parent" formats.
  std::list<Format *> format_list;
  for (auto &[unused, fmt_ptr] : bin_encoding_info->format_map()) {
    format_list.push_back(fmt_ptr);
  }
  while (!format_list.empty()) {
    auto *format = format_list.front();
    format_list.pop_front();
    for (auto iter = reference_map.lower_bound(format->name());
         iter != reference_map.upper_bound(format->name()); iter++) {
      std::string parent_format_name = iter->second;
      VisitFormatDef(format_decl_map_.at(parent_format_name),
                     bin_encoding_info.get());
      format_list.push_back(bin_encoding_info->GetFormat(parent_format_name));
    }
  }
  bin_encoding_info->PropagateExtractors();
  return bin_encoding_info;
}

void BinFormatVisitor::PreProcessDeclarations(DeclarationListCtx *ctx) {
  std::vector<IncludeFileCtx *> include_files;

  for (auto *declaration : ctx->declaration()) {
    context_file_map_.insert({declaration, current_file_index_});
    // Create map from format name to format ctx.
    if (declaration->format_def() != nullptr) {
      auto format_def = declaration->format_def();
      context_file_map_.insert({format_def, current_file_index_});
      auto name = format_def->name->getText();
      auto iter = format_decl_map_.find(name);
      if (iter != format_decl_map_.end()) {
        error_listener()->semanticError(
            format_def->start, absl::StrCat("Multiple definitions of format '",
                                            name, "' first defined at line: ",
                                            iter->second->start->getLine()));
        continue;
      }
      format_decl_map_.emplace(name, format_def);
      continue;
    }
    // Create map from instruction group name to instruction group ctx.
    if (declaration->instruction_group_def() != nullptr) {
      auto group_def = declaration->instruction_group_def();
      context_file_map_.insert({group_def, current_file_index_});
      auto name = group_def->name->getText();
      auto iter = group_decl_map_.find(name);
      if (iter != group_decl_map_.end()) {
        error_listener()->semanticError(
            group_def->start,
            absl::StrCat(
                "Multiple definitions of instruction group '", name,
                "' first defined at line: ", iter->second->start->getLine()));
        continue;
      }
      group_decl_map_.emplace(name, group_def);
      continue;
    }
    // Process bin_fmt include files.
    include_files.push_back(declaration->include_file());
  }
  // Create map from decoder name to decoder ctx.
  for (auto *decoder_def : ctx->decoder_def()) {
    context_file_map_.insert({decoder_def, current_file_index_});
    auto name = decoder_def->name->getText();
    auto iter = decoder_decl_map_.find(name);
    if (iter != decoder_decl_map_.end()) {
      error_listener()->semanticError(
          decoder_def->start, absl::StrCat("Multiple definitions of decoder '",
                                           name, "' first defined at line: ",
                                           iter->second->start->getLine()));
      continue;
    }
    decoder_decl_map_.emplace(name, decoder_def);
  }
  for (auto *include_file_ctx : include_files) {
    VisitIncludeFile(include_file_ctx);
  }
}

void BinFormatVisitor::VisitIncludeFile(IncludeFileCtx *ctx) {
  // The literal includes the double quotes.
  std::string literal = ctx->STRING_LITERAL()->getText();
  // Remove the double quotes from the literal and construct the full file name.
  std::string file_name = literal.substr(1, literal.length() - 2);
  // Check for recursive include.
  for (auto const &name : include_file_stack_) {
    if (name == file_name) {
      error_listener()->semanticError(
          ctx->start, absl::StrCat("Recursive include of '", file_name, "'"));
      return;
    }
  }
  ParseIncludeFile(ctx, file_name, include_dir_vec_);
}

void BinFormatVisitor::ParseIncludeFile(antlr4::ParserRuleContext *ctx,
                                        const std::string &file_name,
                                        std::vector<std::string> const &dirs) {
  std::fstream include_file;
  // Open include file.
  include_file.open(file_name, std::fstream::in);
  if (!include_file.is_open()) {
    // Try each of the include file directories.
    for (auto const &dir : dirs) {
      std::string include_name = absl::StrCat(dir, "/", file_name);
      include_file.open(include_name, std::fstream::in);
      if (include_file.is_open()) break;
    }
    if (!include_file.is_open()) {
      // Try a local file.
      include_file.open(file_name, std::fstream::in);
      if (!include_file.is_open()) {
        error_listener()->semanticError(
            ctx->start, absl::StrCat("Failed to open '", file_name, "'"));
        return;
      }
    }
  }
  std::string previous_file_name = error_listener()->file_name();
  int previous_file_index_ = current_file_index_;
  error_listener()->set_file_name(file_name);
  file_names_.push_back(file_name);
  current_file_index_ = file_names_.size() - 1;
  auto *include_parser = new BinFmtAntlrParserWrapper(&include_file);
  // We need to save the parser state so it's available for analysis after
  // we are done with building the parse trees.
  antlr_parser_wrappers_.push_back(include_parser);
  // Add the error listener.
  include_parser->parser()->removeErrorListeners();
  include_parser->parser()->addErrorListener(error_listener());
  // Start parsing at the declaratition_list_w_eof rule.
  DeclarationListCtx *declaration_list =
      include_parser->parser()->top_level()->declaration_list();
  include_file.close();
  if (error_listener()->syntax_error_count() > 0) {
    error_listener()->set_file_name(previous_file_name);
    current_file_index_ = previous_file_index_;
    return;
  }
  include_file_stack_.push_back(file_name);
  PreProcessDeclarations(declaration_list);
  include_file_stack_.pop_back();
  error_listener()->set_file_name(previous_file_name);
  current_file_index_ = previous_file_index_;
}

void BinFormatVisitor::VisitFormatDef(FormatDefCtx *ctx,
                                      BinEncodingInfo *encoding_info) {
  if (ctx == nullptr) return;
  // Get the format name and width.
  std::string format_name = ctx->IDENT()->getText();
  // If we have already visited the format, just return. There is no error.
  if (encoding_info->GetFormat(format_name) != nullptr) return;
  int format_width = -1;
  if (ctx->width != nullptr) {
    format_width = ConvertToInt(ctx->width->number());
  } else if (ctx->inherits_from() == nullptr) {
    // Must specify either a width or it must inherit from a format that has
    // a width.
    error_listener_->semanticError(
        file_names_[context_file_map_.at(ctx)], ctx->start,
        absl::StrCat("Format '", format_name,
                     "': must specify a width or inherited format"));
    return;
  }
  absl::StatusOr<Format *> format_res;
  if (ctx->inherits_from() != nullptr) {
    std::string parent_name = ctx->inherits_from()->IDENT()->getText();
    auto *parent_format = encoding_info->GetFormat(parent_name);
    if (parent_format == nullptr) {
      auto iter = format_decl_map_.find(parent_name);
      if (iter != format_decl_map_.end()) {
        VisitFormatDef(iter->second, encoding_info);
      }
      parent_format = encoding_info->GetFormat(parent_name);
    }
    if (parent_format == nullptr) {
      error_listener_->semanticError(
          file_names_[context_file_map_.at(ctx)], ctx->inherits_from()->start,
          absl::StrCat("Parent format '", parent_name, "' not defined"));
      return;
    } else {
      int parent_width = parent_format->declared_width();
      if ((format_width != -1) && (format_width != parent_width)) {
        error_listener_->semanticError(
            file_names_[context_file_map_.at(format_decl_map_.at(parent_name))],
            ctx->inherits_from()->start,
            absl::StrCat("Format '", format_name, "' declared width (",
                         format_width, ") differs from width inherited from '",
                         parent_name, "' (", parent_width, ")"));
        return;
      }
      if (format_width == -1) format_width = parent_width;
    }
    format_res =
        encoding_info->AddFormat(format_name, format_width, parent_name);
  } else {
    format_res = encoding_info->AddFormat(format_name, format_width);
  }
  if (!format_res.ok()) {
    error_listener_->semanticError(file_names_[context_file_map_.at(ctx)],
                                   ctx->start, format_res.status().message());
    return;
  }
  // Parse the fields in the format.
  auto format = format_res.value();
  auto file_index = context_file_map_.at(ctx);
  for (auto field : ctx->format_field_defs()->field_def()) {
    context_file_map_.insert({field, file_index});
    VisitFieldDef(field, format, encoding_info);
  }
  // Parse the overlays in the format.
  for (auto overlay : ctx->format_field_defs()->overlay_def()) {
    context_file_map_.insert({overlay, file_index});
    VisitOverlayDef(overlay, format);
  }
  auto status = format->ComputeAndCheckFormatWidth();
  if (!status.ok()) {
    error_listener_->semanticError(file_names_[context_file_map_.at(ctx)],
                                   ctx->start, status.message());
  }
}

void BinFormatVisitor::VisitFieldDef(FieldDefCtx *ctx, Format *format,
                                     BinEncodingInfo *encoding_info) {
  if (ctx == nullptr) return;

  std::string field_name(ctx->field_name->getText());
  if (ctx->FORMAT() == nullptr) {
    // If it's a field definition, add the field.
    bool is_signed = ctx->sign_spec()->SIGNED() != nullptr;
    int width = ConvertToInt(ctx->index()->number());
    auto status = format->AddField(field_name, is_signed, width);
    if (!status.ok()) {
      error_listener_->semanticError(file_names_[context_file_map_.at(ctx)],
                                     ctx->field_name, status.message());
    }
    return;
  }

  // Otherwise it is a reference to a format (which may be defined later
  // in the file). Add it and adjust the width later.
  int size = 1;
  if (ctx->index() != nullptr) {
    size = ConvertToInt(ctx->index()->number());
  }
  std::string format_ref_name = ctx->format_name->getText();
  // Make sure that the referred to format is fully parsed.
  auto *format_ref = encoding_info->GetFormat(format_ref_name);
  if (format_ref == nullptr) {
    auto iter = format_decl_map_.find(format_ref_name);
    if (iter != format_decl_map_.end()) {
      VisitFormatDef(iter->second, encoding_info);
    }
    format_ref = encoding_info->GetFormat(format_ref_name);
  }
  format->AddFormatReferenceField(field_name, format_ref_name, size,
                                  ctx->start);
}

void BinFormatVisitor::VisitOverlayDef(OverlayDefCtx *ctx, Format *format) {
  if (ctx == nullptr) return;

  std::string name(ctx->IDENT()->getText());
  bool is_signed = ctx->sign_spec()->SIGNED() != nullptr;
  int width = ConvertToInt(ctx->width->number());
  // For now, only support overlays <= 64 bit wide.
  if (width > 64) {
    error_listener_->semanticError(
        file_names_[context_file_map_.at(ctx)], ctx->width->number()->start,
        "Only overlays <= 64 bits are supported for now");
    return;
  }
  // Visit the bitfield spec items.
  auto overlay_res = format->AddFieldOverlay(name, is_signed, width);
  if (!overlay_res.ok()) {
    error_listener_->semanticError(file_names_[context_file_map_.at(ctx)],
                                   ctx->start, overlay_res.status().message());
    return;
  }
  auto *overlay = overlay_res.value();
  int file_index = context_file_map_.at(ctx);
  for (auto *bit_field : ctx->bit_field_list()->bit_field_spec()) {
    context_file_map_.insert({bit_field, file_index});
    VisitOverlayBitField(bit_field, overlay);
  }
  if (overlay->computed_width() != overlay->declared_width()) {
    error_listener_->semanticError(
        file_names_[context_file_map_.at(ctx)], ctx->start,
        absl::StrCat("Declared width (", overlay->declared_width(),
                     ") differs from computed width (",
                     overlay->computed_width(), ")"));
  }
}

void BinFormatVisitor::VisitOverlayBitField(BitFieldCtx *ctx,
                                            Overlay *overlay) {
  if (ctx == nullptr) return;

  if (ctx->IDENT() != nullptr) {
    // This is a reference to a bit field in the format.
    if (ctx->bit_range_list() != nullptr) {
      std::vector<BitRange> bit_ranges_;
      for (auto *range : ctx->bit_range_list()->bit_index_range()) {
        bit_ranges_.push_back(GetBitIndexRange(range));
      }
      auto status = overlay->AddFieldReference(ctx->IDENT()->getText(),
                                               std::move(bit_ranges_));
      if (!status.ok()) {
        error_listener_->semanticError(file_names_[context_file_map_.at(ctx)],
                                       ctx->start, status.message());
      }
      return;
    }
    auto status = overlay->AddFieldReference(ctx->IDENT()->getText());
    if (!status.ok()) {
      error_listener_->semanticError(file_names_[context_file_map_.at(ctx)],
                                     ctx->start, status.message());
    }
    return;
  }
  // Is this a reference to the format itself?
  if (ctx->bit_range_list() != nullptr) {
    std::vector<BitRange> bit_ranges_;
    for (auto *range : ctx->bit_range_list()->bit_index_range()) {
      bit_ranges_.push_back(GetBitIndexRange(range));
    }
    auto status = overlay->AddFormatReference(std::move(bit_ranges_));
    if (!status.ok()) {
      error_listener_->semanticError(file_names_[context_file_map_.at(ctx)],
                                     ctx->start, status.message());
    }
    return;
  }

  // This must be a binary number string.
  auto bin_num = ParseBinaryNum(ctx->bin_number()->BIN_NUMBER());
  overlay->AddBitConstant(bin_num);
}

InstructionGroup *BinFormatVisitor::VisitInstructionGroupDef(
    InstructionGroupDefCtx *ctx, BinEncodingInfo *encoding_info) {
  if (ctx == nullptr) return nullptr;

  // Create the named instruction group.
  std::string group_name = ctx->name->getText();
  auto format_iter = format_decl_map_.find(group_name);
  if (format_iter != format_decl_map_.end()) {
    error_listener_->semanticError(
        file_names_[context_file_map_.at(ctx)], ctx->start,
        absl::StrCat(group_name, ": illegal use of format name"));
  }
  int width = ConvertToInt(ctx->number());
  std::string format_name = ctx->format->getText();
  auto iter = format_decl_map_.find(format_name);
  if (iter == format_decl_map_.end()) {
    error_listener_->semanticError(
        file_names_[context_file_map_.at(ctx)], ctx->start,
        absl::StrCat("Undefined format '", format_name,
                     "' used by instruction group '", group_name, "'"));
    return nullptr;
  } else {
    VisitFormatDef(iter->second, encoding_info);
    auto *format = encoding_info->GetFormat(format_name);
    // Verify that the format width = declared width and also <= 64 bits wide.
    if (format->declared_width() != width) {
      error_listener_->semanticError(
          file_names_[context_file_map_.at(format_decl_map_.at(format_name))],
          ctx->start,
          absl::StrCat("Width of format '", format_name, "' (",
                       format->declared_width(),
                       ") differs from the declared width of instruction "
                       "group '",
                       group_name, "' (", width, ")"));
    }
    if (format->declared_width() > 64) {
      error_listener_->semanticError(
          file_names_[context_file_map_.at(format_decl_map_.at(format_name))],
          ctx->start,
          absl::StrCat("Instruction group '", group_name,
                       "': width must be <= 64 bits"));
    }
  }
  auto inst_group_res =
      encoding_info->AddInstructionGroup(group_name, width, format_name);
  if (!inst_group_res.ok()) {
    error_listener_->semanticError(file_names_[context_file_map_.at(ctx)],
                                   ctx->start,
                                   inst_group_res.status().message());
    return nullptr;
  }
  // Parse the instruction encoding definitions in the instruction group.
  auto *inst_group = inst_group_res.value();
  int file_index = context_file_map_.at(ctx);
  for (auto *inst_def : ctx->instruction_def_list()->instruction_def()) {
    context_file_map_.insert({inst_def, file_index});
    VisitInstructionDef(inst_def, inst_group);
  }
  return inst_group_res.value();
}

void BinFormatVisitor::VisitInstructionDef(InstructionDefCtx *ctx,
                                           InstructionGroup *inst_group) {
  if (ctx == nullptr) return;

  if (ctx->generate != nullptr) {
    ProcessInstructionDefGenerator(ctx, inst_group);
    return;
  }
  // Get the instruction name and the format it refers to.
  std::string name = ctx->name->getText();
  std::string format_name = ctx->format_name->getText();
  auto format = inst_group->encoding_info()->GetFormat(format_name);
  if (format == nullptr) {
    auto iter = format_decl_map_.find(format_name);
    if (iter == format_decl_map_.end()) {
      error_listener_->semanticError(
          file_names_[context_file_map_.at(ctx)], ctx->start,
          absl::StrCat("Format '", format_name, "' referenced by instruction '",
                       name, "' not defined"));
    } else {
      VisitFormatDef(iter->second, inst_group->encoding_info());
      format = inst_group->encoding_info()->GetFormat(format_name);
    }
  }
  if ((format != nullptr) &&
      (format->declared_width() != inst_group->width())) {
    error_listener_->semanticError(
        file_names_[context_file_map_.at(ctx)], ctx->start,
        absl::StrCat(
            "Length of format '", format_name, "' (", format->declared_width(),
            ") differs from the declared width of the instruction group (",
            inst_group->width(), ")"));
  }
  auto *inst_encoding =
      inst_group->AddInstructionEncoding(ctx->format_name, name, format);
  if (format == nullptr) return;
  // Add constraints to the instruction encoding.
  int file_index = context_file_map_.at(ctx);
  for (auto *constraint : ctx->field_constraint_list()->field_constraint()) {
    context_file_map_.insert({constraint, file_index});
    VisitConstraint(format, constraint, inst_encoding);
  }
}

void BinFormatVisitor::ProcessInstructionDefGenerator(
    InstructionDefCtx *ctx, InstructionGroup *inst_group) {
  if (ctx == nullptr) return;
  absl::flat_hash_set<std::string> range_variable_names;
  std::vector<RangeAssignmentInfo *> range_info_vec;
  // Process range assignment lists. The range assignment is either a single
  // value or a structured binding assignment. If it's a binding assignment we
  // need to make sure each tuple has the same number of values as there are
  // idents to assign them to.
  int file_index = context_file_map_.at(ctx);
  for (auto *assign_ctx : ctx->range_assignment()) {
    auto *range_info = new RangeAssignmentInfo();
    range_info_vec.push_back(range_info);
    for (auto *ident_ctx : assign_ctx->IDENT()) {
      std::string name = ident_ctx->getText();
      if (range_variable_names.contains(name)) {
        error_listener()->semanticError(
            file_names_[file_index], assign_ctx->start,
            absl::StrCat("Duplicate binding variable name '", name, "'"));
        continue;
      }
      range_variable_names.insert(name);
      range_info->range_names.push_back(ident_ctx->getText());
      range_info->range_values.push_back({});
      range_info->range_regexes.emplace_back(
          absl::StrCat("\\$\\(", name, "\\)"));
      // Verify that the range variable is used in the string.
      if (!RE2::PartialMatch(ctx->generator_instruction_def_list()->getText(),
                             range_info->range_regexes.back())) {
        error_listener()->semanticWarning(
            file_names_[file_index], assign_ctx->start,
            absl::StrCat("Unreferenced binding variable '", name, "'."));
      }
    }
    // See if it's a list of simple values.
    if (!assign_ctx->gen_value().empty()) {
      for (auto *gen_value_ctx : assign_ctx->gen_value()) {
        if (gen_value_ctx->IDENT() != nullptr) {
          range_info->range_values[0].push_back(
              gen_value_ctx->IDENT()->getText());
        } else if (gen_value_ctx->number() != nullptr) {
          range_info->range_values[0].push_back(
              gen_value_ctx->number()->getText());
        } else {
          // Strip off double quotes.
          std::string value = gen_value_ctx->string->getText();
          range_info->range_values[0].push_back(
              value.substr(1, value.size() - 2));
        }
      }
      continue;
    }
    // It's a list of tuples with a structured binding assignment.
    for (auto *tuple_ctx : assign_ctx->tuple()) {
      if (tuple_ctx->gen_value().size() != range_info->range_names.size()) {
        // Clean up.
        for (auto *info : range_info_vec) delete info;
        error_listener_->semanticError(
            file_names_[file_index], assign_ctx->start,
            "Number of values differs from number of identifiers");
        return;
      }
      for (int i = 0; i < tuple_ctx->gen_value().size(); ++i) {
        if (tuple_ctx->gen_value(i)->IDENT() != nullptr) {
          range_info->range_values[i].push_back(
              tuple_ctx->gen_value(i)->IDENT()->getText());
        } else if (tuple_ctx->gen_value(i)->number() != nullptr) {
          range_info->range_values[i].push_back(
              tuple_ctx->gen_value(i)->number()->getText());
        } else {
          // Strip off double quotes.
          std::string value = tuple_ctx->gen_value(i)->string->getText();
          range_info->range_values[i].push_back(
              value.substr(1, value.size() - 2));
        }
      }
    }
  }
  // Check that all binding variable references are valid.
  std::string input_text = ctx->generator_instruction_def_list()->getText();
  std::string::size_type start_pos = 0;
  auto pos = input_text.find_first_of('$', start_pos);
  while (pos != std::string::npos) {
    // Skip past the '$('
    start_pos = pos + 2;
    auto end_pos = input_text.find_first_of(')', pos);
    // Extract the ident.
    auto ident = input_text.substr(start_pos, end_pos - start_pos);
    if (!range_variable_names.contains(ident)) {
      error_listener()->semanticError(
          file_names_[file_index], ctx->generator_instruction_def_list()->start,
          absl::StrCat("Undefined binding variable '", ident, "'"));
    }
    start_pos = end_pos;
    pos = input_text.find_first_of('$', start_pos);
  }
  if (error_listener()->HasError()) {
    // Clean up.
    for (auto *info : range_info_vec) delete info;
    return;
  }

  // Now we need to iterate over the range_info instances and substitution
  // ranges. This will produce new text that will be parsed and processed.
  std::string generated_text =
      GenerateInstructionDefList(range_info_vec, 0, input_text);
  // Parse and process the generated text.
  auto *parser = new BinFmtAntlrParserWrapper(generated_text);
  antlr_parser_wrappers_.push_back(parser);
  // Parse the text starting at the opcode_spec_list rule.
  auto instruction_defs =
      parser->parser()->instruction_def_list()->instruction_def();
  // Process the opcode spec.
  for (auto *inst_def : instruction_defs) {
    context_file_map_.insert({inst_def, file_index});
    VisitInstructionDef(inst_def, inst_group);
  }
  // Clean up.
  for (auto *info : range_info_vec) delete info;
}

std::string BinFormatVisitor::GenerateInstructionDefList(
    const std::vector<RangeAssignmentInfo *> &range_info_vec, int index,
    const std::string &template_str_in) const {
  std::string generated;
  // Iterate for the number of values.
  for (int i = 0; i < range_info_vec[index]->range_values[0].size(); ++i) {
    // Copy the template string.
    std::string template_str = template_str_in;
    // For each ident, perform substitutions in the template copy with the
    // current set of values.
    int var_index = 0;
    int replace_count = 0;
    for (auto &re : range_info_vec[index]->range_regexes) {
      replace_count += RE2::GlobalReplace(
          &template_str, re,
          range_info_vec[index]->range_values[var_index++][i]);
    }
    // If there are multiple range specifications, then recursively call to
    // generate the cartesian product with the values of the next value range
    // substitutions.
    if (range_info_vec.size() > index + 1) {
      absl::StrAppend(&generated, GenerateInstructionDefList(
                                      range_info_vec, index + 1, template_str));
    } else {
      absl::StrAppend(&generated, template_str);
    }
    // If there were no replacements, then the range variables weren't used,
    // and the template string won't change for any other values in the range.
    // This can happen if there range variables aren't referenced in the string.
    // Thus, break out of the loop.
    if (replace_count == 0) break;
  }
  return generated;
}

void BinFormatVisitor::VisitConstraint(Format *format, FieldConstraintCtx *ctx,
                                       InstructionEncoding *inst_encoding) {
  if (ctx == nullptr) return;
  if (inst_encoding == nullptr) return;

  // Constraints are based on field names ==/!=/>/>=/</<= to a value.
  std::string field_name = ctx->field_name->getText();
  std::string op = ctx->constraint_op()->getText();
  // If the number is binary, let's get its length too and check against the
  // field width.
  if (ctx->number()->BIN_NUMBER() != nullptr) {
    int length = ParseBinaryNum(ctx->number()->BIN_NUMBER()).width;
    auto *field = format->GetField(field_name);
    auto *overlay = format->GetOverlay(field_name);
    if (field != nullptr) {
      if (field->width != length) {
        error_listener_->semanticWarning(
            file_names_[context_file_map_.at(ctx)], ctx->start,
            absl::StrCat("Field '", field_name, "' has width ", field->width,
                         " but constraint value is ", length, " bits"));
      }
    } else if (overlay != nullptr) {
      if (overlay->computed_width() != length) {
        error_listener_->semanticWarning(
            file_names_[context_file_map_.at(ctx)], ctx->start,
            absl::StrCat("Overlay '", field_name, "' has width ",
                         overlay->computed_width(), " but constraint value is ",
                         length, " bits"));
      }
    }
  }
  int value = ConvertToInt(ctx->number());
  absl::Status status;
  if (op == "==") {
    status = inst_encoding->AddEqualConstraint(field_name, value);
  } else if (op == "!=") {
    status = inst_encoding->AddOtherConstraint(ConstraintType::kNe, field_name,
                                               value);
  } else if (op == ">") {
    status = inst_encoding->AddOtherConstraint(ConstraintType::kGt, field_name,
                                               value);
  } else if (op == ">=") {
    status = inst_encoding->AddOtherConstraint(ConstraintType::kGe, field_name,
                                               value);
  } else if (op == "<") {
    status = inst_encoding->AddOtherConstraint(ConstraintType::kLt, field_name,
                                               value);
  } else if (op == "<=") {
    status = inst_encoding->AddOtherConstraint(ConstraintType::kLe, field_name,
                                               value);
  }
  if (!status.ok()) {
    error_listener_->semanticError(file_names_[context_file_map_.at(ctx)],
                                   ctx->start, status.message());
  }
}

std::unique_ptr<BinEncodingInfo> BinFormatVisitor::VisitDecoderDef(
    DecoderDefCtx *ctx) {
  if (ctx == nullptr) return nullptr;
  std::string name = ctx->name->getText();

  // First get the opcode enum.
  int opcode_count = 0;
  std::string opcode_enum;
  for (auto *attr_ctx : ctx->decoder_attribute()) {
    if (attr_ctx->opcode_enum_decl() != nullptr) {
      auto opcode_enum_literal =
          attr_ctx->opcode_enum_decl()->STRING_LITERAL()->getText();
      opcode_enum =
          opcode_enum_literal.substr(1, opcode_enum_literal.size() - 2);
      if (opcode_enum.empty()) {
        error_listener_->semanticError(file_names_[context_file_map_.at(ctx)],
                                       attr_ctx->start,
                                       "Empty opcode enum string");
      }
      if (opcode_count > 0) {
        error_listener_->semanticError(file_names_[context_file_map_.at(ctx)],
                                       attr_ctx->start,
                                       "More than one opcode enum declaration");
      }
      opcode_count++;
    }
  }
  auto encoding_info =
      std::make_unique<BinEncodingInfo>(opcode_enum, error_listener_.get());
  auto *decoder = encoding_info->AddBinDecoder(name);
  if (decoder == nullptr) return nullptr;
  absl::flat_hash_set<std::string> group_name_set;
  int namespace_count = 0;
  for (auto *attr_ctx : ctx->decoder_attribute()) {
    // Include files.
    if (attr_ctx->include_files() != nullptr) {
      for (auto *include_file : attr_ctx->include_files()->include_file()) {
        auto include_text = include_file->STRING_LITERAL()->getText();
        encoding_info->AddIncludeFile(include_text);
      }
      continue;
    }
    // Namespace declaration.
    if (attr_ctx->namespace_decl() != nullptr) {
      auto decl = attr_ctx->namespace_decl();
      for (auto *namespace_name : decl->namespace_ident()) {
        decoder->namespaces().push_back(namespace_name->getText());
      }
      if (namespace_count > 0) {
        error_listener_->semanticError(file_names_[context_file_map_.at(ctx)],
                                       attr_ctx->start,
                                       "More than one namespace declaration");
      }
      namespace_count++;
      continue;
    }
    // Instruction groups are listed as either a single instruction group,
    // or a parent group that contains several individual groups.

    // If it is a single group process it here.
    if ((attr_ctx->group_name() != nullptr) &&
        (attr_ctx->group_name()->group_name_list() == nullptr)) {
      std::string group_name = attr_ctx->group_name()->IDENT()->getText();

      auto map_iter = encoding_info->instruction_group_map().find(group_name);
      InstructionGroup *inst_group = nullptr;
      if (map_iter != encoding_info->instruction_group_map().end()) {
        inst_group = map_iter->second;
      } else {
        auto iter = group_decl_map_.find(group_name);
        if (iter != group_decl_map_.end()) {
          context_file_map_.insert({iter->second, current_file_index_});
          inst_group =
              VisitInstructionGroupDef(iter->second, encoding_info.get());
        }
        if (inst_group == nullptr) {
          error_listener_->semanticError(
              file_names_[context_file_map_.at(ctx)], attr_ctx->start,
              absl::StrCat("No such instruction group: '", group_name, "'"));
          continue;
        }
      }
      if (group_name_set.contains(group_name)) {
        error_listener_->semanticError(
            file_names_[context_file_map_.at(ctx)], attr_ctx->start,
            absl::StrCat("Instruction group '", group_name, "' listed twice"));
        continue;
      }
      group_name_set.insert(group_name);
      decoder->AddInstructionGroup(inst_group);
      continue;
    }
    // If it is a parent group, process it here.
    if ((attr_ctx->group_name() != nullptr) &&
        (attr_ctx->group_name()->group_name_list() != nullptr)) {
      // Check each of the child groups. Visit any that haven't been visited
      // yet, and make sure they all specify the same format.
      std::string group_name = attr_ctx->group_name()->IDENT()->getText();
      if (group_name_set.contains(group_name)) {
        error_listener_->semanticError(
            file_names_[context_file_map_.at(ctx)], attr_ctx->start,
            absl::StrCat("Instruction group '", group_name,
                         "' listed twice - ignored"));
        continue;
      }
      std::vector<InstructionGroup *> child_groups;
      std::string group_format_name;
      // Iterate through the list of named "child" groups to combine.
      for (auto *ident : attr_ctx->group_name()->group_name_list()->IDENT()) {
        auto child_name = ident->getText();
        if (group_name_set.contains(child_name)) {
          error_listener_->semanticError(
              file_names_[context_file_map_.at(ctx)], attr_ctx->start,
              absl::StrCat("Instruction group listed twice: '", child_name,
                           "' - ignored"));
          continue;
        }
        InstructionGroup *child_group = nullptr;
        auto map_iter = encoding_info->instruction_group_map().find(child_name);
        if (map_iter != encoding_info->instruction_group_map().end()) {
          child_group = map_iter->second;
          bool exists = false;
          for (auto const *group : child_groups) {
            if (child_name == group->name()) {
              exists = true;
              break;
            }
          }
          if (exists) {
            error_listener_->semanticError(
                file_names_[context_file_map_.at(ctx)], attr_ctx->start,
                absl::StrCat("Instruction group '", child_name,
                             "' listed twice"));
            continue;
          }
        } else {
          // The instruction group hasn't been visited yet, so look up the
          // declaration and visit it now.
          auto iter = group_decl_map_.find(child_name);
          if (iter != group_decl_map_.end()) {
            child_group =
                VisitInstructionGroupDef(iter->second, encoding_info.get());
          }
          if (child_group == nullptr) {
            error_listener_->semanticError(
                file_names_[context_file_map_.at(ctx)], attr_ctx->start,
                absl::StrCat("Instruction group '", child_name, "' not found"));
            continue;
          }
        }
        if (child_groups.empty()) {
          group_format_name = child_group->format_name();
        }
        // Check that the child groups all use the same instruction format.
        if (group_format_name != child_group->format_name()) {
          error_listener_->semanticError(
              file_names_[context_file_map_.at(ctx)], attr_ctx->start,
              absl::StrCat("Instruction group '", child_name,
                           "' must use format '", group_format_name,
                           ", to be merged into group '", group_name, "'"));
          continue;
        }
        child_groups.push_back(child_group);
      }

      if (child_groups.empty()) {
        error_listener_->semanticError(attr_ctx->start, "No child groups");
        continue;
      }
      // Create the "parent" group and add all of the instructions from the
      // child groups to it.
      auto width = child_groups.front()->width();
      auto res = encoding_info->AddInstructionGroup(group_name, width,
                                                    group_format_name);
      if (!res.ok()) {
        error_listener_->semanticError(attr_ctx->start, res.status().message());
        continue;
      }
      auto parent_group = res.value();
      for (auto *child_group : child_groups) {
        for (auto *encoding : child_group->encoding_vec()) {
          parent_group->AddInstructionEncoding(
              new InstructionEncoding(*encoding));
        }
      }
      group_name_set.insert(parent_group->name());
      decoder->AddInstructionGroup(parent_group);
    }
  }
  if (group_name_set.empty()) {
    error_listener_->semanticError(ctx->start, "No instruction groups found");
  }
  return encoding_info;
}

}  // namespace bin_format
}  // namespace decoder
}  // namespace sim
}  // namespace mpact
