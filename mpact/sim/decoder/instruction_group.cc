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

#include "mpact/sim/decoder/instruction_group.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <tuple>
#include <utility>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "antlr4-runtime/Token.h"
#include "mpact/sim/decoder/bin_encoding_info.h"
#include "mpact/sim/decoder/decoder_error_listener.h"
#include "mpact/sim/decoder/encoding_group.h"
#include "mpact/sim/decoder/extract.h"
#include "mpact/sim/decoder/instruction_encoding.h"

namespace mpact {
namespace sim {
namespace decoder {
namespace bin_format {

InstructionGroup::InstructionGroup(std::string name, int width,
                                   std::string format_name,
                                   std::string opcode_enum,
                                   BinEncodingInfo *encoding_info)
    : name_(name),
      width_(width),
      format_name_(format_name),
      opcode_enum_(opcode_enum),
      encoding_info_(encoding_info) {
  format_ = encoding_info->GetFormat(format_name);
}

InstructionGroup::~InstructionGroup() {
  for (auto *enc : encoding_vec_) {
    delete enc;
  }
  encoding_map_.clear();
  encoding_vec_.clear();
  for (auto *group : encoding_group_vec_) {
    delete group;
  }
  encoding_group_vec_.clear();
}

// Add an instruction encoding into the current group. Check that the format
// has the correct width, and that the format the encoding is defined in, or
// derives from the format associated with the instruction group.
InstructionEncoding *InstructionGroup::AddInstructionEncoding(
    antlr4::Token *token, std::string name, Format *format) {
  if ((format != nullptr) &&
      ((format_ == nullptr) || (!format->IsDerivedFrom(format_)))) {
    encoding_info_->error_listener()->semanticError(
        token, absl::StrCat("Format '", format->name(),
                            "' used by instruction encoding '", name,
                            "' is not derived from '", format_name_, "'"));
    return nullptr;
  }

  // No need to double check width, since the format at this point derives
  // from the the instruction group format.

  // Warn if the instruction name has been seen before. It might be fully valid
  // that an instruction name has multiple encodings, but warn about it, in
  // case it is an error.
  if (encoding_name_map_.contains(name)) {
    encoding_info_->error_listener()->semanticWarning(
        token, absl::StrCat("Duplicate instruction opcode name '", name,
                            "' in group '", this->name(), "'."));
  }
  auto *encoding = new InstructionEncoding(name, format);
  encoding_vec_.push_back(encoding);
  encoding_name_map_.insert(std::make_pair(name, encoding));
  return encoding;
}

void InstructionGroup::AddInstructionEncoding(InstructionEncoding *encoding) {
  if (encoding_name_map_.contains(encoding->name())) {
    encoding_info_->error_listener()->semanticWarning(
        nullptr, absl::StrCat("Duplicate instruction opcode name '",
                              encoding->name(), "' in group '", name(), "'."));
  }
  encoding_name_map_.insert(std::make_pair(encoding->name(), encoding));
  encoding_vec_.push_back(encoding);
}

void InstructionGroup::ProcessEncodings() {
  if (encoding_vec_.empty()) {
    encoding_info_->error_listener()->semanticWarning(
        nullptr,
        absl::StrCat("No encodings in instruction group: '", name(), "'"));
    return;
  }
  // Insert the encodings into a map based on the mask value - grouping
  // instructions with the same mask.
  for (auto *enc : encoding_vec_) {
    encoding_map_.insert(std::make_pair(enc->GetMask(), enc));
  }
  encoding_group_vec_.push_back(new EncodingGroup(this, 0));
  for (auto &[unused, enc_ptr] : encoding_map_) {
    bool is_added = false;
    for (auto *group : encoding_group_vec_) {
      if (group->CanAddEncoding(enc_ptr)) {
        group->AddEncoding(enc_ptr);
        is_added = true;
        break;
      }
    }
    if (!is_added) {
      encoding_group_vec_.push_back(new EncodingGroup(this, 0));
      encoding_group_vec_.back()->AddEncoding(enc_ptr);
      is_added = true;
    }
  }
  for (auto *grp : encoding_group_vec_) {
    grp->AddSubGroups();
  }
}

// Check for encoding errors.
void InstructionGroup::CheckEncodings() {
  for (auto *enc_grp : encoding_group_vec_) {
    enc_grp->CheckEncodings();
  }
}

absl::Status InstructionGroup::AddSpecialization(
    const std::string &name, const std::string &parent_name,
    InstructionEncoding *encoding) {
  if (encoding_name_map_.contains(name)) {
    encoding_info_->error_listener()->semanticError(
        nullptr,
        absl::StrCat("Duplicate instruction specialization opcode name '", name,
                     "' in group '", this->name(), "'."));
    return absl::AlreadyExistsError(
        absl::StrCat("Duplicate instruction specialization opcode name '", name,
                     "' in group '", this->name(), "'."));
  }
  encoding_name_map_.insert(std::make_pair(name, encoding));
  auto *parent_encoding = encoding_name_map_.at(parent_name);
  return parent_encoding->AddSpecialization(name, encoding);
}

// Helper function used to sort the instruction group elements in a vector.
static bool InstructionGroupLess(EncodingGroup *lhs, EncodingGroup *rhs) {
  uint64_t lhs_value = 0;
  uint64_t rhs_value = 0;
  if (lhs->parent() == nullptr) {
    auto grp_recipe = GetExtractionRecipe(lhs->constant());
    lhs_value = ExtractValue(lhs->encoding_vec()[0]->GetValue(), grp_recipe);
    rhs_value = ExtractValue(rhs->encoding_vec()[0]->GetValue(), grp_recipe);
  } else {
    auto grp_recipe = GetExtractionRecipe(lhs->parent()->discriminator());
    lhs_value = ExtractValue(lhs->encoding_vec()[0]->GetValue(), grp_recipe);
    rhs_value = ExtractValue(rhs->encoding_vec()[0]->GetValue(), grp_recipe);
  }
  return lhs_value < rhs_value;
}

// Emit the code in the form of two strings that are returned in a tuple.
std::tuple<std::string, std::string> InstructionGroup::EmitCode() {
  std::string h_string;
  std::string cc_string;

  // First sort the encoding group vector according to the value of the
  // discriminator bits.
  std::sort(encoding_group_vec_.begin(), encoding_group_vec_.end(),
            &InstructionGroupLess);

  if (!encoding_group_vec_.empty()) {
    std::string initializers;
    // The signature for the top level decode function for this instruction
    // group.
    std::string signature =
        absl::StrCat(opcode_enum_, " Decode", this->name(), "(",
                     format_->uint_type_name(), " inst_word)");
    std::string w_format_signature = absl::StrCat(
        "std::pair<", opcode_enum_, ", FormatEnum> Decode", this->name(),
        "WithFormat(", format_->uint_type_name(), " inst_word)");
    // First part of the definition of the top level decoder function.
    std::string top_level_decoder = absl::StrCat(signature, " {\n");
    std::string w_format_top_level_decoder =
        absl::StrCat(w_format_signature, " {\n");
    std::string declarations =
        absl::StrCat("std::pair<", opcode_enum_, ", FormatEnum> Decode",
                     this->name(), "None(", format_->uint_type_name(), ");\n");
    std::string definitions = absl::StrCat(
        "std::pair<", opcode_enum_, ", FormatEnum> Decode", this->name(),
        "None(", format_->uint_type_name(), ") {\n  return std::make_pair(",
        opcode_enum_, "::kNone, FormatEnum::kNone);\n}\n\n");
    for (size_t i = 0; i < encoding_group_vec_.size(); i++) {
      auto *grp = encoding_group_vec_[i];
      std::string name = absl::StrCat(this->name(), "_", absl::Hex(i));
      grp->EmitInitializers(name, &initializers, opcode_enum_);
      grp->EmitDecoders(name, &declarations, &definitions, opcode_enum_);
      absl::StrAppend(&top_level_decoder, "  auto opcode = Decode", name,
                      "(inst_word).first;\n");
      absl::StrAppend(&w_format_top_level_decoder,
                      "  auto opcode_format = Decode", name, "(inst_word);\n");
      if (encoding_group_vec_.size() > 1) {
        absl::StrAppend(&top_level_decoder, "  if (opcode != ", opcode_enum_,
                        "::kNone) return opcode;\n");
        absl::StrAppend(&w_format_top_level_decoder,
                        "  if (opcode_format.first != ", opcode_enum_,
                        "::kNone) return opcode_format;\n");
      }
    }
    // Last part of the definition of the top level decoder function.
    absl::StrAppend(&top_level_decoder,
                    "  return opcode;\n"
                    "}\n");
    absl::StrAppend(&w_format_top_level_decoder,
                    "  return opcode_format;\n"
                    "}\n");
    // String the different strings together in order and return.
    absl::StrAppend(&cc_string, declarations, initializers, definitions,
                    top_level_decoder, w_format_top_level_decoder);
    absl::StrAppend(&h_string, signature, ";\n", w_format_signature, ";\n");
  }
  return std::make_tuple(h_string, cc_string);
}

// Write out instruction group information.
std::string InstructionGroup::WriteGroup() {
  std::string output;
  absl::StrAppend(&output, "\n\n// Instruction group: ", name_, "\n");
  absl::PadSpec pad;
  switch (width_ / 8) {
    case 1:
      pad = absl::PadSpec::kZeroPad2;
      break;
    case 2:
      pad = absl::PadSpec::kZeroPad4;
      break;
    case 4:
      pad = absl::PadSpec::kZeroPad8;
      break;
    case 8:
      pad = absl::PadSpec::kZeroPad16;
      break;
    default:
      pad = absl::PadSpec::kNoPad;
      break;
  }
  uint64_t common_mask = 0xffff'ffff'ffff'ffff;
  for (auto &[key, unused] : encoding_map_) {
    common_mask &= key;
  }
  absl::StrAppend(&output, "//   common bits: ", absl::Hex(common_mask, pad),
                  "\n");
  for (auto *grp : encoding_group_vec_) {
    absl::StrAppend(&output, grp->DumpGroup("", "  "));
  }
  return output;
}

}  // namespace bin_format
}  // namespace decoder
}  // namespace sim
}  // namespace mpact
