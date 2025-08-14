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

#include "mpact/sim/decoder/format.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <utility>

#include "absl/numeric/bits.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "antlr4-runtime/Token.h"
#include "mpact/sim/decoder/format_name.h"
#include "mpact/sim/decoder/overlay.h"

namespace mpact {
namespace sim {
namespace decoder {
namespace bin_format {

using ::mpact::sim::machine_description::instruction_set::ToSnakeCase;

FieldOrFormat::~FieldOrFormat() {
  if (is_field_) {
    delete field_;
  }
  field_ = nullptr;
  format_ = nullptr;
}

// The equality operator compares to verify that the field/format definitions
// are equivalent, i.e., refers to the same bits.
bool FieldOrFormat::operator==(const FieldOrFormat& rhs) const {
  if (is_field_ != rhs.is_field_) return false;
  if (is_field_) {
    if (high_ != rhs.high_) return false;
    if (size_ != rhs.size_) return false;
  } else {
    if (format_ != rhs.format_) return false;
  }
  return true;
}

bool FieldOrFormat::operator!=(const FieldOrFormat& rhs) const {
  return !(*this == rhs);
}

using ::mpact::sim::machine_description::instruction_set::ToPascalCase;

Format::Format(std::string name, int width, BinEncodingInfo* encoding_info)
    : Format(name, width, "", encoding_info) {}

Format::Format(std::string name, int width, std::string base_format_name,
               BinEncodingInfo* encoding_info)
    : name_(name),
      base_format_name_(base_format_name),
      declared_width_(width),
      encoding_info_(encoding_info) {}

Format::~Format() {
  // for (auto &[unused, field_ptr] : field_map_) {
  //   delete field_ptr;
  // }
  field_map_.clear();
  for (auto& [unused, overlay_ptr] : overlay_map_) {
    delete overlay_ptr;
  }
  overlay_map_.clear();
  for (auto* field : field_vec_) {
    delete field;
  }
  field_vec_.clear();
}

// Add a field to the current format with the given width and signed/unsigned
// attribute.
absl::Status Format::AddField(std::string name, bool is_signed, int width) {
  // Make sure that the name isn't used already in the format.
  if (field_map_.contains(name)) {
    return absl::InvalidArgumentError(
        absl::StrCat("Field '", name, "' already defined"));
  }
  auto field = new Field(name, is_signed, width, this);
  field_vec_.push_back(new FieldOrFormat(field));
  field_map_.insert(std::make_pair(name, field));
  return absl::OkStatus();
}

// Add a format reference field - name of another format - to the current
// format. This will be resolved once all the formats have been parsed.
void Format::AddFormatReferenceField(std::string format_alias,
                                     std::string format_name, int size,
                                     antlr4::Token* ctx) {
  field_vec_.push_back(new FieldOrFormat(format_alias, format_name, size, ctx));
}

// Add an overlay to the current format. An overlay is a named alias for a
// not necessarily contiguous nor in order collection of bits in the format.
absl::StatusOr<Overlay*> Format::AddFieldOverlay(std::string name,
                                                 bool is_signed, int width) {
  // Make sure that the name isn't used already in the format.
  if (overlay_map_.contains(name)) {
    return absl::InvalidArgumentError(
        absl::StrCat("Overlay '", name, "' already defined as an overlay"));
  }
  if (field_map_.contains(name)) {
    return absl::InvalidArgumentError(
        absl::StrCat("Overlay '", name, "' already defined as a field"));
  }
  auto overlay = new Overlay(name, is_signed, width, this);
  overlay_map_.insert(std::make_pair(name, overlay));
  return overlay;
}

// Return the named field if it exists, nullptr otherwise.
Field* Format::GetField(absl::string_view field_name) const {
  auto iter = field_map_.find(field_name);
  if (iter == field_map_.end()) return nullptr;
  return iter->second;
}

// Return the named field if it exists, nullptr otherwise.
Overlay* Format::GetOverlay(absl::string_view overlay_name) const {
  auto iter = overlay_map_.find(overlay_name);
  if (iter == overlay_map_.end()) return nullptr;
  return iter->second;
}

// Return the string containing the integer type used to contain the current
// format. If it is greater than 128 bits, will use a byte array (int8_t *).
// If it is 65 to 128 bits, will use absl::[u]int128.
std::string Format::GetUIntType(int bitwidth) const {
  if (bitwidth > 128) return "uint8_t *";
  if (bitwidth > 64) return "absl::uint128";
  return absl::StrCat("uint", GetIntTypeBitWidth(bitwidth), "_t");
}

std::string Format::GetIntType(int bitwidth) const {
  if (bitwidth > 128) return "int8_t *";
  if (bitwidth > 64) return "absl::int128";
  return absl::StrCat("int", GetIntTypeBitWidth(bitwidth), "_t");
}

// Return the int type byte width (1, 2, 4, 8, 16) or (-1 if it's bigger), of
// the integer type that would fit this format.
int Format::GetIntTypeBitWidth(int bitwidth) const {
  auto shift = absl::bit_width(static_cast<unsigned>(bitwidth)) - 1;
  if (absl::popcount(static_cast<unsigned>(bitwidth)) > 1) shift++;
  shift = std::max(shift, 3);
  if (shift > 7) return -1;
  return 1 << shift;
}

// Once all the formats have been read in, this method is called to check the
// format and update any widths that depended on other formats being read in.
absl::Status Format::ComputeAndCheckFormatWidth() {
  // If there is a base format name, look up that format, verify that the widths
  // are the same.
  if (!base_format_name_.empty()) {
    auto* base_format = encoding_info_->GetFormat(base_format_name_);
    if (base_format == nullptr) {
      return absl::InternalError(
          absl::StrCat("Format ", name(), " refers to undefined base format ",
                       base_format_name_));
    }
    if (base_format->declared_width() != declared_width_) {
      return absl::InternalError(absl::StrCat(
          "Format ", name_, " (", declared_width_,
          ") differs in width from base format ", base_format->name(), " (",
          base_format->declared_width(), ")"));
    }
    base_format_ = base_format;
    base_format_->derived_formats_.push_back(this);
  }
  if (computed_width_ == 0) {
    // Go through the list of fields/format references. Get the declared widths
    // of the formats and add to the computed width. Signal error if the
    // computed width differs from the declared width.
    for (auto* field_or_format : field_vec_) {
      // Field.
      if (field_or_format->is_field()) {
        auto* field = field_or_format->field();
        field->high = declared_width_ - computed_width_ - 1;
        field->low = field->high - field->width + 1;
        computed_width_ += field->width;
        extractors_.insert(std::make_pair(field->name, field_or_format));
        continue;
      }

      // Format;
      auto* format = field_or_format->format();
      if (format == nullptr) {
        std::string fmt_name = field_or_format->format_name();
        format = encoding_info_->GetFormat(fmt_name);
        if (format == nullptr) {
          return absl::InternalError(absl::StrCat(
              "Format '", name(), "' refers to undefined format ", fmt_name));
        }
        field_or_format->set_format(format);
      }
      field_or_format->set_high(declared_width_ - computed_width_ - 1);
      computed_width_ += format->declared_width() * field_or_format->size();
      extractors_.insert(
          std::make_pair(field_or_format->format_alias(), field_or_format));
    }
    if (computed_width_ != declared_width_) {
      return absl::InternalError(absl::StrCat(
          "Format '", name_, "' declared width (", declared_width_,
          ") differs from computed width (", computed_width_, ")"));
    }
  }
  for (auto& [name, overlay_ptr] : overlay_map_) {
    auto status = overlay_ptr->ComputeHighLow();
    if (!status.ok()) return status;
    overlay_extractors_.insert(std::make_pair(name, overlay_ptr));
  }
  // Set the type names.
  int_type_name_ = GetIntType(declared_width_);
  uint_type_name_ = absl::StrCat("u", int_type_name_);
  return absl::OkStatus();
}

// The extractor functions in the generated code are all generated within a
// namespace specific to the format they're associated with. However, extractors
// that don't conflict in the bits they select may be promoted to be generated
// in the base format namespace. This method is used to propagate such
// potential promotions upward in the inheritance tree.
void Format::PropagateExtractorsUp() {
  for (auto* fmt : derived_formats_) {
    fmt->PropagateExtractorsUp();
  }
  if (base_format_ != nullptr) {
    // Try to propagate extractors up the inheritance tree.
    for (auto const& [name, field_or_format_ptr] : extractors_) {
      // Ignore those that have a nullptr, they have already failed to be
      // promoted.
      if (field_or_format_ptr == nullptr) continue;
      auto iter = base_format_->extractors_.find(name);
      // If it isn't in the parent, add it.
      if (iter == base_format_->extractors_.end()) {
        base_format_->extractors_.insert(
            std::make_pair(name, field_or_format_ptr));
      } else if (iter->second == nullptr) {
        // Can't promote it, a previous attempt failed.
        continue;
      } else if (*field_or_format_ptr != *(iter->second)) {
        // If the base extractor refers to a different object, fail the
        // promotion.
        base_format_->extractors_[name] = nullptr;
      }
    }
    for (auto const& [name, overlay_ptr] : overlay_extractors_) {
      // Ignore those that have a nullptr, they have already failed to be
      // promoted.
      if (overlay_ptr == nullptr) continue;
      auto iter = base_format_->overlay_extractors_.find(name);
      // If it isn't in the parent, add it.
      if (iter == base_format_->overlay_extractors_.end()) {
        base_format_->overlay_extractors_.insert(
            std::make_pair(name, overlay_ptr));
      } else if (iter->second == nullptr) {
        // Previous attempt fail, don't promote.
        continue;
      } else if (*overlay_ptr != *(iter->second)) {
        // If the base format extractor refers to a different overlay type,
        // fail the promotion.
        base_format_->overlay_extractors_[name] = nullptr;
      }
    }
  }
}

// This is the counterpart to the previous method and cleans up extractors that
// were attempted to be promoted, but couldn't be due to conflicts with others,
// e.g., two fields were named the same in different formats but referred to
// different bits.
void Format::PropagateExtractorsDown() {
  // Remove the extractor entries with null ptrs and any extractors that
  // have been promoted.
  auto e_iter = extractors_.begin();
  while (e_iter != extractors_.end()) {
    auto cur = e_iter++;
    // Failed promotion from derived format extractors.
    if (cur->second == nullptr) {
      extractors_.erase(cur);
      continue;
    }
    // If the name exists in overlay extractors, erase both.
    if (overlay_extractors_.find(cur->first) != overlay_extractors_.end()) {
      overlay_extractors_.erase(cur->first);
      extractors_.erase(cur);
      continue;
    }
  }
  // Remove the overlay extractor entries with null ptrs.
  auto o_iter = overlay_extractors_.begin();
  while (o_iter != overlay_extractors_.end()) {
    auto cur = o_iter++;
    // Failed promotion from derived format extractors.
    if (cur->second == nullptr) {
      overlay_extractors_.erase(cur);
      continue;
    }
    // If the name exists in overlay extractors, erase both.
    if (extractors_.find(cur->first) != extractors_.end()) {
      extractors_.erase(cur->first);
      overlay_extractors_.erase(cur);
      continue;
    }
  }
  for (auto* fmt : derived_formats_) {
    fmt->PropagateExtractorsDown();
  }
}

// Returns true if the current format, or a base format, contains an
// extractor for field 'name'.
bool Format::HasExtract(const std::string& name) const {
  auto iter = extractors_.find(name);
  if ((iter != extractors_.end()) && (iter->second != nullptr)) return true;

  if (base_format_ != nullptr) return base_format_->HasExtract(name);

  return false;
}

// Returns true if the current format, or a base format, contains an
// extractor for overlay 'name'.
bool Format::HasOverlayExtract(const std::string& name) const {
  auto iter = overlay_extractors_.find(name);
  if ((iter != overlay_extractors_.end()) && (iter->second != nullptr)) {
    return true;
  }

  if (base_format_ != nullptr) return base_format_->HasOverlayExtract(name);

  return false;
}

std::string Format::GeneratePackedStructFieldExtractor(
    const Field* field) const {
  std::string h_output;
  int width = field->width;
  std::string return_type = GetUIntType(width);
  std::string signature = absl::StrCat("inline ", return_type, " Extract",
                                       ToPascalCase(field->name), "(");
  if (declared_width_ < 64) {
    absl::StrAppend(&signature, GetUIntType(declared_width_), " value) {\n");
  } else {
    absl::StrAppend(&signature, "const uint8_t *value) {\n");
  }
  absl::StrAppend(&h_output, signature);
  // Now start the body.
  std::string union_type = absl::StrCat("const ", ToSnakeCase(name()),
                                        "::Union", ToPascalCase(name()));
  absl::StrAppend(&h_output, "  ", union_type,
                  " *packed_union;\n"
                  "  packed_union = reinterpret_cast<",
                  union_type, "*>(",
                  declared_width_ > 64 ? "value);\n" : "&value);\n",
                  "  return packed_union->", ToSnakeCase(name()), ".",
                  field->name, ";\n}\n\n");
  return h_output;
}

// This method generates the C++ code for the field extractors for the current
// format.
std::string Format::GenerateFieldExtractor(const Field* field) const {
  std::string h_output;
  int return_width = GetIntTypeBitWidth(field->width);
  std::string result_type_name =
      field->is_signed ? GetIntType(return_width) : GetUIntType(return_width);
  std::string argument_type_name = GetUIntType(computed_width_);
  std::string signature = absl::StrCat(
      result_type_name, " Extract", ToPascalCase(field->name), "(",
      computed_width_ > 128 ? "const " : "", argument_type_name, " value)");

  absl::StrAppend(&h_output, "inline ", signature, " {\n");

  // Generate extraction function. For fields it's a simple shift and mask if
  // the source format width <= 64 bits. Slightly more involved with format
  // <= 128 bits. For larger formats use the templated extract helper function.
  std::string expr;
  if (declared_width_ <= 64) {
    uint64_t mask = (1ULL << field->width) - 1;
    if (field->low == 0) {
      expr = absl::StrCat("value & 0x", absl::Hex(mask));
    } else {
      expr = absl::StrCat(" (value >> ", field->low, ") & 0x", absl::Hex(mask));
    }
  } else if (declared_width_ <= 128) {
    absl::StrAppend(&h_output,
                    "  absl::uint128 mask = 1;\n"
                    "  mask = (mask << ",
                    field->width, ") - 1;\n");
    if (field->low == 0) {
      expr = absl::StrCat("value & mask");
    } else {
      expr = absl::StrCat(" (value >> ", field->low, ") & mask");
    }
  } else {
    // For format width > 128 bits, use the templated extract helper function.
    int byte_size = (declared_width_ + 7) / 8;
    expr = absl::StrCat("internal::ExtractBits<", result_type_name, ">(value, ",
                        byte_size, ", ", field->high, ", ", field->width, ")");
  }

  // Add sign-extension if the field is signed.
  std::string sign_extension;
  if (field->is_signed) {
    int shift = return_width - field->width;
    sign_extension =
        absl::StrCat("  ", result_type_name, " result = (", expr, ") << ",
                     shift, ";\n  result = result >> ", shift, ";\n");
    expr = "result";
  }
  if (declared_width_ <= 64) {
    absl::StrAppend(&h_output, sign_extension, "  return ", expr, ";\n}\n\n");
  } else if ((declared_width_ <= 128) && (return_width <= 64)) {
    absl::StrAppend(&h_output, sign_extension, "  return absl::Uint128Low64(",
                    expr, ");\n}\n\n");
  } else {
    absl::StrAppend(&h_output, sign_extension, "  return ", expr, ";\n}\n\n");
  }
  // If the parent format size is not a power of two, also create an extractor
  // that takes a uint8_t * parameter.
  if ((declared_width_ <= 128) &&
      (absl::popcount(static_cast<unsigned>(declared_width_)) > 1)) {
    absl::StrAppend(&h_output, "inline ", result_type_name, " Extract",
                    ToPascalCase(field->name), "(const uint8_t *value) {\n");
    int byte_size = (declared_width_ + 7) / 8;
    absl::StrAppend(&h_output, "  return internal::ExtractBits<",
                    result_type_name, ">(value, ", byte_size, ", ", field->high,
                    ", ", field->width, ");\n}\n\n");
  }
  return h_output;
}

std::string Format::GeneratePackedStructFieldInserter(
    const Field* field) const {
  std::string h_output;
  std::string field_type_name;
  std::string inst_word_type_name;
  if (computed_width_ <= 64) {
    inst_word_type_name = GetUIntType(computed_width_);
  } else {
    inst_word_type_name = "uint8_t *";
  }
  field_type_name = GetUIntType(field->width);
  std::string union_type =
      absl::StrCat(ToSnakeCase(name()), "::Union", ToPascalCase(name()));
  absl::StrAppend(&h_output, "static inline ", inst_word_type_name, " Insert",
                  ToPascalCase(field->name), "(", field_type_name, " value, ",
                  inst_word_type_name,
                  " inst_word) {\n"
                  "  ",
                  union_type,
                  " *packed_union;\n"
                  "  packed_union = reinterpret_cast<",
                  union_type, "*>(",
                  (computed_width_ <= 64 ? "&inst_word" : "inst_word"),
                  ");\n"
                  "  packed_union->",
                  ToSnakeCase(name()), ".", field->name,
                  " = value;\n"
                  "  return inst_word;\n"
                  "}\n\n");
  return h_output;
}

// This method generates the C++ code for field inserters for the current
// format. That is, the generated code will take the value of a field and
// insert it into the right place in the instruction word.
std::string Format::GenerateFieldInserter(const Field* field) const {
  std::string h_output;
  std::string field_type_name;
  std::string inst_word_type_name = GetUIntType(computed_width_);
  if (declared_width_ <= 128) {
    field_type_name = inst_word_type_name;
  } else {
    field_type_name = GetUIntType(field->width);
  }
  absl::StrAppend(&h_output, "static inline ", inst_word_type_name, " Insert",
                  ToPascalCase(field->name), "(", field_type_name, " value, ",
                  inst_word_type_name, " inst_word) {\n");
  if (declared_width_ <= 64) {
    uint64_t mask = ((1ULL << field->width) - 1) << field->low;
    std::string shift;
    if (field->low != 0) {
      shift = absl::StrCat(" << ", field->low);
    }
    absl::StrAppend(&h_output, "  inst_word = (inst_word & ~0x",
                    absl::Hex(mask), "ULL)", " | ((value", shift, ") & 0x",
                    absl::Hex(mask),
                    "ULL);\n"
                    "  return inst_word;\n"
                    "}\n");
  } else if (declared_width_ <= 128) {
    absl::StrAppend(&h_output,
                    "  absl::uint128 mask = 1;\n"
                    "  mask = (mask << ",
                    field->width, ") - 1;\n");
    if (field->low != 0) {
      absl::StrAppend(&h_output, "  mask = mask << ", field->low, ";\n");
    }
    absl::StrAppend(&h_output,
                    "  inst_word = (inst_word & ~mask) | (value & mask);\n"
                    "  return inst_word;\n"
                    "}\n");
  } else if (field->width <= 128) {
    int byte_size = (declared_width_ + 7) / 8;
    absl::StrAppend(&h_output, "  internal::InsertBits(inst_word, ", byte_size,
                    ", ", field->high, ", ", field->width,
                    ", value);\n"
                    "  return inst_word;\n"
                    "}\n");
  } else {
    absl::StrAppend(&h_output,
                    "  LOG(FATAL) << \" Support for fields > 128 bits not "
                    "implemented - "
                    "yet.\";\n"
                    "  return 0;\n}\n");
  }
  return h_output;
}

// This method generates the C++ code for overlay inserters for the current
// format. That is, the generated code will take the value of an overlay and
// insert its components into the right places in the instruction word.
std::string Format::GenerateOverlayInserter(Overlay* overlay) const {
  std::string h_output;
  std::string result_type_name = GetUIntType(computed_width_);
  std::string overlay_type_name;
  if (computed_width_ <= 128) {
    overlay_type_name = result_type_name;
  } else {
    overlay_type_name = GetUIntType(overlay->declared_width());
  }
  absl::StrAppend(&h_output, "static inline ", result_type_name, " Insert",
                  ToPascalCase(overlay->name()), "(", overlay_type_name,
                  " value, ", result_type_name, " inst_word) {\n");
  // Mark error if either the overlay or the format is > 64 bits.
  if (overlay->declared_width() > 128) {
    absl::StrAppend(&h_output,
                    "  LOG(FATAL) << \" Support for overlays > 128 bits "
                    "not implemented - "
                    "yet.\";\n"
                    "  return 0;\n}\n");
    return h_output;
  }
  bool use_mask_variable = false;
  int remaining = overlay->declared_width();
  int byte_size = (declared_width_ + 7) / 8;
  if (declared_width_ <= 128) {
    absl::StrAppend(&h_output, "  ", result_type_name, " tmp;\n");
    // Track the leftmost bit in the overlay.
    if (declared_width_ > 64) {
      absl::StrAppend(&h_output, "  absl::uint128 mask;\n");
      use_mask_variable = true;
    }
  } else {
    absl::StrAppend(&h_output, "  ", overlay_type_name, " tmp;\n");
    if (overlay->declared_width() > 64) {
      absl::StrAppend(&h_output, "  absl::uint128 mask;\n");
      use_mask_variable = true;
    }
  }
  for (auto& bits_or_field : overlay->component_vec()) {
    int width = bits_or_field->width();
    // Ignore the bit fields in the overlay.
    if (bits_or_field->high() < 0) {
      remaining -= width;
      continue;
    }
    std::string shift;
    if (remaining - width > 0) {
      shift = absl::StrCat(" >> ", remaining - width);
    }
    if (use_mask_variable) {
      absl::StrAppend(&h_output,
                      "  mask = 1;\n"
                      "  mask = (mask << ",
                      width, ") - 1;\n");
      absl::StrAppend(&h_output, "  tmp = (value ", shift, ") & mask;\n");
    } else {
      uint64_t mask = ((1ULL << width) - 1);
      // Extract the bits from the overlay value for the current component.
      absl::StrAppend(&h_output, "  tmp = (value ", shift, ") & 0x",
                      absl::Hex(mask), "ULL;\n");
    }
    shift.clear();
    if (bits_or_field->low() != 0) {
      shift = absl::StrCat(" << ", bits_or_field->low());
    }
    if (declared_width_ <= 128) {
      absl::StrAppend(&h_output, "  inst_word |= (tmp ", shift, ");\n");
    } else {
      absl::StrAppend(&h_output, "  internal::InsertBits(inst_word, ",
                      byte_size, ", ", bits_or_field->high(), ", ", width,
                      ",  tmp);\n");
    }

    remaining -= width;
  }
  absl::StrAppend(&h_output, "  return inst_word;\n}\n");
  return h_output;
}

std::string Format::GeneratePackedStructFormatInserter(
    std::string_view format_alias, const Format* format, int high,
    int size) const {
  std::string h_output;
  std::string inst_word_type_name;
  if (computed_width_ <= 64) {
    inst_word_type_name = GetUIntType(computed_width_);
  } else {
    inst_word_type_name = "uint8_t *";
  }
  std::string format_type_name = GetUIntType(format->declared_width());
  std::string union_type =
      absl::StrCat(ToSnakeCase(name()), "::Union", ToPascalCase(name()));
  absl::StrAppend(&h_output, "static inline ", inst_word_type_name, "Insert",
                  ToPascalCase(format_alias), "(", format_type_name, " value, ",
                  inst_word_type_name,
                  " inst_word) {\n"
                  "  ",
                  union_type,
                  " *packed_union;\n"
                  "  packed_union = reinterpret_cast<",
                  union_type, "*>(",
                  (computed_width_ <= 64 ? "&inst_word" : "inst_word"),
                  ");\n"
                  "  packed_union->",
                  ToSnakeCase(name()), ".", format_alias,
                  " = value;\n"
                  "  return inst_word;\n"
                  "}\n\n");
  return h_output;
}

// This method generates the C++ code for format inserters for the current
// format. That is, the generated code will take the value of a format and
// insert it into the right place in the instruction word.
std::string Format::GenerateFormatInserter(std::string_view format_alias,
                                           const Format* format, int high,
                                           int size) const {
  if (size > 1) {
    return GenerateReplicatedFormatInserter(format_alias, format, high, size);
  }
  return GenerateSingleFormatInserter(format_alias, format, high);
}

std::string Format::GenerateReplicatedFormatInserter(
    std::string_view format_alias, const Format* format, int high,
    int size) const {
  std::string h_output;
  std::string target_type_name = GetUIntType(declared_width_);
  std::string format_type_name;

  if (declared_width_ <= 128) {
    format_type_name = target_type_name;
  } else {
    format_type_name = GetUIntType(format->declared_width());
  }
  absl::StrAppend(&h_output, "static inline ", target_type_name, " Insert",
                  ToPascalCase(format_alias), "(", "int index, ",
                  format_type_name, " value, ", target_type_name,
                  " inst_word) {\n");
  if (format->declared_width() > 128) {
    absl::StrAppend(&h_output,
                    "  LOG(FATAL) << \" Support for formats > 128 bits not "
                    "implemented - "
                    "yet.\";\n"
                    "  return 0;\n}\n");
    return h_output;
  }
  int width = format->declared_width();
  int low = high - width + 1;
  if (declared_width_ <= 64) {
    uint64_t mask = (1ULL << width) - 1;
    absl::StrAppend(&h_output, "  int low = ", low, " - (index * ", width,
                    ");\n"
                    "  return (inst_word & (~0x",
                    absl::Hex(mask), "ULL << low))", " | ((value << low) & (0x",
                    absl::Hex(mask), "ULL << low));\n}\n");
  } else if (declared_width_ <= 128) {
    absl::StrAppend(
        &h_output, "  int low = ", low, " - (index * ", width,
        ");\n"
        "  absl::uint128 mask = 1;\n"
        "  mask = (mask << ",
        width,
        ") - 1;\n"
        "  mask <<= low;\n"
        "  return (inst_word & ~mask) | (value << low) & mask;\n}\n");
  } else {
    int byte_size = (declared_width_ + 7) / 8;
    absl::StrAppend(&h_output, "  internal::InsertBits(inst_word, ", byte_size,
                    ", ", high, " - (index * ", width, "), ", width,
                    ", value);\n"
                    "  return inst_word;\n}\n");
  }
  return h_output;
}

std::string Format::GenerateSingleFormatInserter(std::string_view format_alias,
                                                 const Format* format,
                                                 int high) const {
  std::string h_output;
  std::string target_type_name = GetUIntType(declared_width_);
  std::string format_type_name;
  if (declared_width_ <= 128) {
    format_type_name = target_type_name;
  } else {
    format_type_name = GetUIntType(format->declared_width());
  }

  absl::StrAppend(&h_output, "static inline ", target_type_name, " Insert",
                  ToPascalCase(format_alias), "(", format_type_name, " value, ",
                  target_type_name, " inst_word) {\n");
  if (format->declared_width() > 128) {
    absl::StrAppend(&h_output,
                    "  LOG(FATAL) << \" Support for formats > 128 bits not "
                    "implemented - "
                    "yet.\";\n"
                    "  return 0;\n}\n");
    return h_output;
  }
  int width = format->declared_width();
  int low = high - width + 1;
  std::string shift;
  if (low != 0) {
    shift = absl::StrCat(" << ", low);
  }
  if (declared_width_ <= 64) {
    uint64_t mask = ((1ULL << width) - 1) << low;
    absl::StrAppend(&h_output, "  return (inst_word & (~0x", absl::Hex(mask),
                    "ULL))", " | ((value ", shift, ") & 0x", absl::Hex(mask),
                    "ULL);\n}\n");
  } else if (declared_width_ <= 128) {
    absl::StrAppend(&h_output,
                    "  absl::uint128 mask = 1;\n"
                    "  mask = (mask << ",
                    width, ") - 1;\n");
    if (low > 0) {
      absl::StrAppend(&h_output, "  mask = mask << ", low, ";\n");
    }
    absl::StrAppend(&h_output, "  return (inst_word & ~mask) | (value ", shift,
                    ") & mask;\n}\n");
  } else {
    int byte_size = (declared_width_ + 7) / 8;
    absl::StrAppend(&h_output, "  internal::InsertBits(inst_word, ", byte_size,
                    ", ", high, ", ", width,
                    ", value);\n"
                    "  return inst_word;\n}\n");
  }
  return h_output;
}

std::string Format::GeneratePackedStructFormatExtractor(
    absl::string_view format_alias, const Format* format, int high,
    int size) const {
  std::string h_output;
  int width = format->declared_width();
  std::string return_type = GetUIntType(width);
  std::string signature = absl::StrCat("inline ", return_type, " Extract",
                                       ToPascalCase(format_alias), "(");
  if (declared_width_ < 64) {
    absl::StrAppend(&signature, GetUIntType(declared_width_), " value) {\n");
  } else {
    absl::StrAppend(&signature, "const uint8_t *value) {\n");
  }
  absl::StrAppend(&h_output, signature);
  // Now start the body.
  std::string union_type = absl::StrCat("const ", ToSnakeCase(name()),
                                        "::Union", ToPascalCase(name()));
  absl::StrAppend(&h_output, "  ", union_type,
                  " *packed_union;\n"
                  "  packed_union = reinterpret_cast<",
                  union_type, " *>(",
                  declared_width_ > 64 ? "value);\n" : "&value);\n",
                  "  return packed_union->", ToSnakeCase(name()), ".",
                  format_alias, ";\n}\n\n");
  return h_output;
}

// This method generates the format extractors for the current format (for
// when a format contains other formats).
std::string Format::GenerateFormatExtractor(absl::string_view format_alias,
                                            const Format* format, int high,
                                            int size) const {
  std::string h_output;  // For each format generate an extractor.
  int width = format->declared_width();
  // An extraction can only be for 128 bits or less.
  if (width > 128) {
    encoding_info_->error_listener()->semanticError(
        nullptr,
        absl::StrCat("Cannot generate a format extractor for format '",
                     format->name(), "': format is wider than 128 bits"));
    return "";
  }
  std::string return_type = GetUIntType(width);
  std::string signature = absl::StrCat("inline ", return_type, " Extract",
                                       ToPascalCase(format_alias), "(");
  if (declared_width_ <= 128) {
    // If the source format is <= 128 bits, then use an int type.
    std::string arg_type = GetUIntType(declared_width_);
    absl::StrAppend(&signature, arg_type, " value");
  } else {
    // Otherwise use a pointer to uint8_t type.
    absl::StrAppend(&signature, "const uint8_t *value");
  }
  // If the format has multiple instances add an index parameter.
  if (size > 1) {
    absl::StrAppend(&signature, ", int index");
  }
  absl::StrAppend(&signature, ")");
  // Now start the body.
  absl::StrAppend(&h_output, signature, " {\n");
  std::string expr;
  if (declared_width_ <= 128) {
    // If the source format can be stored in a uint128 or smaller.
    int low = high - width + 1;
    std::string shift;
    if (size > 1) {
      shift = absl::StrCat("(", low, " + (index - 1) * ", width, ")");
    } else {
      shift = absl::StrCat(low);
    }
    if (declared_width_ <= 64) {
      uint64_t mask = (1ULL << width) - 1;
      expr =
          absl::StrCat("(value >> ", shift, ") & 0x", absl::Hex(mask), ";\n");
      absl::StrAppend(&h_output, "  return ", expr, ";\n}\n\n");
    } else {
      absl::StrAppend(&h_output,
                      "  absl::uint128 mask = 1;\n"
                      "  mask = (mask << ",
                      width, ") - 1;\n");
      expr = absl::StrCat("(value >> ", shift, ") & mask");
      if (width <= 64) {
        absl::StrAppend(&h_output, "  return absl::Uint128Low64(", expr,
                        ");\n}\n\n");
      } else {
        absl::StrAppend(&h_output, "  return ", expr, ";\n}\n\n");
      }
    }
  } else {
    // If the source format is stored in uint8_t[].
    int byte_size = (declared_width_ + 7) / 8;
    expr = absl::StrCat("internal::ExtractBits<", return_type, ">(value, ",
                        byte_size, ", ", high);
    if (size > 1) {
      absl::StrAppend(&expr, " - (index * ", width, ")");
    }
    absl::StrAppend(&expr, ", ", width, ")");
    absl::StrAppend(&h_output, "  return ", expr, ";\n}\n\n");
  }
  // If the parent format size is not a power of two, also create an
  // extractor that takes a uint8_t * parameter.
  if ((declared_width_ <= 128) &&
      (absl::popcount(static_cast<unsigned>(declared_width_)) > 1)) {
    absl::StrAppend(&h_output, "inline ", return_type, " Extract",
                    ToPascalCase(format_alias), "(const uint8_t *value) {\n");
    int byte_size = (declared_width_ + 7) / 8;
    expr = absl::StrCat("internal::ExtractBits<", return_type, ">(value, ",
                        byte_size, ", ", high);
    if (size > 1) {
      absl::StrAppend(&expr, " - (index * ", width, ")");
    }
    absl::StrAppend(&expr, ", ", width, ")");
    absl::StrAppend(&h_output, "  return ", expr, ";\n}\n\n");
  }
  return h_output;
}

std::string Format::GeneratePackedStructOverlayExtractor(
    Overlay* overlay) const {
  std::string h_output;
  std::string arg_type;
  if (declared_width_ > 128) {
    arg_type = "const uint8_t *";
  } else {
    arg_type = GetUIntType(declared_width_);
  }
  std::string return_type = overlay->is_signed()
                                ? GetIntType(overlay->declared_width())
                                : GetUIntType(overlay->declared_width());
  std::string signature =
      absl::StrCat("inline ", return_type, " Extract",
                   ToPascalCase(overlay->name()), "(", arg_type, " value)");
  absl::StrAppend(&h_output, signature, " {\n  ", return_type, " result;\n",
                  overlay->WritePackedStructValueExtractor("value", "result"));
  if (overlay->is_signed()) {
    int shift = GetIntTypeBitWidth(overlay->declared_width()) -
                overlay->declared_width();
    absl::StrAppend(&h_output, "  result = result << ", shift,
                    ";\n"
                    "  result = result >> ",
                    shift, ";\n");
  }
  absl::StrAppend(&h_output,
                  "  return result;\n"
                  "}\n\n");
  return h_output;
}

// Generates the C++ code for the overlay extractors in the current format.
std::string Format::GenerateOverlayExtractor(Overlay* overlay) const {
  std::string h_output;

  std::string return_type = overlay->is_signed()
                                ? GetIntType(overlay->declared_width())
                                : GetUIntType(overlay->declared_width());

  std::string arg_type = GetUIntType(declared_width_);
  std::string signature =
      absl::StrCat("inline ", return_type, " Extract",
                   ToPascalCase(overlay->name()), "(", arg_type, " value)");

  // Generate definition.
  absl::StrAppend(&h_output, signature,
                  " {\n"
                  "  ",
                  return_type, " result;\n");
  if (declared_width_ <= 64) {
    absl::StrAppend(&h_output,
                    overlay->WriteSimpleValueExtractor("value", "result"));
  } else {
    absl::StrAppend(&h_output, overlay->WriteComplexValueExtractor(
                                   "value", "result", return_type));
  }
  if (overlay->is_signed()) {
    int shift = GetIntTypeBitWidth(overlay->declared_width()) -
                overlay->declared_width();
    absl::StrAppend(&h_output, "  result = result << ", shift,
                    ";\n"
                    "  result = result >> ",
                    shift, ";\n");
  }
  if ((declared_width_ > 64) && (overlay->declared_width() <= 64)) {
    absl::StrAppend(&h_output,
                    "  return UInt128Low64(result();\n"
                    "}\n\n");
  } else {
    absl::StrAppend(&h_output,
                    "  return result;\n"
                    "}\n\n");
  }
  return h_output;
}

// Top level function called to generate all the inserters for this format.
std::string Format::GenerateInserters() const {
  std::string class_output;
  std::string h_output;
  if (extractors_.empty() && overlay_extractors_.empty()) {
    return h_output;
  }
  absl::StrAppend(&h_output, "struct ", ToPascalCase(name()), " {\n\n");
  // First fields and formats.
  std::string inserter;
  for (auto& [unused, field_or_format_ptr] : extractors_) {
    if (field_or_format_ptr->is_field()) {
      if (layout() == Layout::kPackedStruct) {
        inserter =
            GeneratePackedStructFieldInserter(field_or_format_ptr->field());
      } else {
        inserter = GenerateFieldInserter(field_or_format_ptr->field());
      }
      absl::StrAppend(&h_output, inserter);
    } else {
      if (layout() == Layout::kPackedStruct) {
        inserter = GeneratePackedStructFormatInserter(
            field_or_format_ptr->format_alias(), field_or_format_ptr->format(),
            field_or_format_ptr->high(), field_or_format_ptr->size());
      } else {
        inserter = GenerateFormatInserter(
            field_or_format_ptr->format_alias(), field_or_format_ptr->format(),
            field_or_format_ptr->high(), field_or_format_ptr->size());
      }
      absl::StrAppend(&h_output, inserter);
    }
  }
  // Next the overlays.
  for (auto& [unused, overlay_ptr] : overlay_extractors_) {
    auto inserter = GenerateOverlayInserter(overlay_ptr);
    absl::StrAppend(&h_output, inserter);
  }
  absl::StrAppend(&h_output, "};  // struct ", ToPascalCase(name()), "\n\n");
  return h_output;
}

std::string Format::GeneratePackedStructTypes() const {
  std::string h_output;
  // First the struct.
  absl::StrAppend(&h_output, "struct Packed", ToPascalCase(name()), " {\n");
  for (auto it = field_vec_.rbegin(); it != field_vec_.rend(); ++it) {
    auto* component = *it;
    if (component->is_field()) {
      int width = component->field()->width;
      std::string field_type = component->field()->is_signed
                                   ? GetIntType(width)
                                   : GetUIntType(width);
      absl::StrAppend(&h_output, "  ", field_type, " ",
                      component->field()->name, " : ",
                      component->field()->width, ";\n");
    } else {
      absl::StrAppend(&h_output, "  ",
                      GetUIntType(component->format()->declared_width()), " ",
                      component->format_alias(), " : ",
                      component->format()->declared_width(), ";\n");
    }
  }
  absl::StrAppend(&h_output, "} ABSL_ATTRIBUTE_PACKED;\n\n");
  // Next the union.
  int num_bytes = (declared_width_ + 7) / 8;
  absl::StrAppend(&h_output, "union Union", ToPascalCase(name()),
                  " {\n"
                  "  Packed",
                  ToPascalCase(name()), " ", ToSnakeCase(name()),
                  ";\n"
                  "  uint8_t bytes[",
                  num_bytes, "];\n");
  // If it is 64 bits or less, add an unsigned integer value type.
  if (declared_width_ <= 64) {
    absl::StrAppend(&h_output, "  ", GetUIntType(declared_width_), " value;\n");
  }
  absl::StrAppend(&h_output, "};\n\n");
  return h_output;
}

// Top level function called to generate all the extractors for this format.
Extractors Format::GenerateExtractors() const {
  Extractors extractors;
  if (extractors_.empty() && overlay_extractors_.empty()) {
    return extractors;
  }

  extractors.class_output =
      absl::StrCat("class ", ToPascalCase(name()), " {\n public:\n", "  ",
                   ToPascalCase(name()), "() = default;\n\n");

  // Use a separate namespace for each format.
  extractors.h_output =
      absl::StrCat("namespace ", ToSnakeCase(name()), " {\n\n");
  extractors.types_output =
      absl::StrCat("namespace ", ToSnakeCase(name()), " {\n\n");

  std::string get_size = absl::StrCat("constexpr int k", ToPascalCase(name()),
                                      "Size = ", declared_width(), ";\n\n");
  absl::StrAppend(&extractors.h_output, get_size);
  absl::StrAppend(&extractors.class_output, "static ", get_size);

  // If this format has a packed struct layout, generate the types required.
  if (layout() == Layout::kPackedStruct) {
    absl::StrAppend(&extractors.types_output, GeneratePackedStructTypes());
  }

  // First fields and formats.
  for (auto& [unused, field_or_format_ptr] : extractors_) {
    if (field_or_format_ptr->is_field()) {
      std::string extractor;
      if (layout() == Layout::kPackedStruct) {
        extractor =
            GeneratePackedStructFieldExtractor(field_or_format_ptr->field());
      } else {
        extractor = GenerateFieldExtractor(field_or_format_ptr->field());
      }
      absl::StrAppend(&extractors.h_output, extractor);
      absl::StrAppend(&extractors.class_output, "static ", extractor);
    } else {
      std::string extractor;
      if (layout() == Layout::kPackedStruct) {
        extractor = GeneratePackedStructFormatExtractor(
            field_or_format_ptr->format_alias(), field_or_format_ptr->format(),
            field_or_format_ptr->high(), field_or_format_ptr->size());
      } else {
        extractor = GenerateFormatExtractor(
            field_or_format_ptr->format_alias(), field_or_format_ptr->format(),
            field_or_format_ptr->high(), field_or_format_ptr->size());
      }
      absl::StrAppend(&extractors.h_output, extractor);
      absl::StrAppend(&extractors.class_output, "static ", extractor);
    }
  }

  // Then the overlays.
  for (auto& [unused, overlay_ptr] : overlay_extractors_) {
    std::string extractor;
    if (layout() == Layout::kPackedStruct) {
      extractor = GeneratePackedStructOverlayExtractor(overlay_ptr);
    } else {
      extractor = GenerateOverlayExtractor(overlay_ptr);
    }
    absl::StrAppend(&extractors.h_output, extractor);
    absl::StrAppend(&extractors.class_output, "static ", extractor);
  }

  absl::StrAppend(&extractors.h_output, "}  // namespace ", ToSnakeCase(name()),
                  "\n\n");
  absl::StrAppend(&extractors.types_output, "}  // namespace ",
                  ToSnakeCase(name()), "\n\n");
  absl::StrAppend(&extractors.class_output, "};\n\n");
  return extractors;
}

bool Format::IsDerivedFrom(const Format* format) {
  if (format == this) return true;
  if (base_format_ == nullptr) return false;
  if (base_format_ == format) return true;
  return base_format_->IsDerivedFrom(format);
}

}  // namespace bin_format
}  // namespace decoder
}  // namespace sim
}  // namespace mpact
