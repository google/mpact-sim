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

#include "mpact/sim/decoder/overlay.h"

#include <string>
#include <vector>

#include "mpact/sim/decoder/format.h"
#include "absl/status/status.h"
#include "absl/strings/string_view.h"

namespace mpact {
namespace sim {
namespace decoder {
namespace bin_format {

BitsOrField::BitsOrField(Field *field, int high, int low, int width)
    : field_(field), high_(high), low_(low), width_(width), position_(-1) {}

BitsOrField::BitsOrField(BinaryNum bin_num, int width)
    : field_(nullptr),
      width_(width),
      bin_num_(bin_num) {
  width_ = bin_num_.width;
}

Overlay::Overlay(std::string name, bool is_signed, int width, Format *format)
    : name_(name),
      is_signed_(is_signed),
      declared_width_(width),
      format_(format) {}

Overlay::~Overlay() {
  for (auto *item : component_vec_) {
    delete item;
  }
  component_vec_.clear();
}

// Simply append another component with a copy of the bin_num.
void Overlay::AddBitConstant(BinaryNum bin_num) {
  must_be_extracted_ = true;
  int position = declared_width_ - computed_width_ - 1;
  component_vec_.push_back(new BitsOrField(bin_num, position));
  computed_width_ += bin_num.width;
}

// Adds a reference to an entire field.
absl::Status Overlay::AddFieldReference(std::string field_name) {
  // Check that it names a field in the format.
  auto field = format_->GetField(field_name);
  if (field == nullptr) {
    return absl::InternalError(
        absl::StrCat("'", field_name, "' does not name a field in format '",
                     format_->name(), "'"));
  }
  // Translate the field relative bit ranges to format relative bit ranges.
  int width = field->width;
  component_vec_.push_back(new BitsOrField(field, width - 1, 0, field->width));
  computed_width_ += width;
  return absl::OkStatus();
}

// Adds a series of bit references to a field.
absl::Status Overlay::AddFieldReference(std::string field_name,
                                        const std::vector<BitRange> &ranges) {
  // Verify that the fields is valid.
  auto field = format_->GetField(field_name);
  if (field == nullptr) {
    return absl::InternalError(
        absl::StrCat("'", field_name, "' does not name a field in format '",
                     format_->name(), "'"));
  }
  // Scan the ranges.
  for (auto &range : ranges) {
    // Verify that the ranges don't refer to bits that don't exist.
    if (range.first >= field->width) {
      return absl::InternalError(absl::StrCat("bit index '", range.first,
                                              "' out of range for field '",
                                              field->name));
    }
    if (range.last >= field->width) {
      return absl::InternalError(absl::StrCat("bit index '", range.last,
                                              "' out of range for field '",
                                              field->name));
    }
    int width = range.first - range.last + 1;
    component_vec_.push_back(
        new BitsOrField(field, range.first, range.last, width));
    computed_width_ += width;
  }
  return absl::OkStatus();
}

absl::Status Overlay::AddFormatReference(const std::vector<BitRange> &ranges) {
  for (auto &range : ranges) {
    // Check that the range is legal for the format.
    if (range.first >= format_->declared_width()) {
      return absl::InternalError(absl::StrCat("bit index '", range.first,
                                              "' out of range for format '",
                                              format_->name(), "'"));
    }
    if (range.last >= format_->declared_width()) {
      return absl::InternalError(absl::StrCat("bit index '", range.last,
                                              "' out of range for format '",
                                              format_->name(), "'"));
    }
    int width = range.first - range.last + 1;
    // Translate into bit positions relative to the bit format that contains
    // the field.
    component_vec_.push_back(
        new BitsOrField(nullptr, range.first, range.last, width));
    computed_width_ += width;
  }
  return absl::OkStatus();
}

absl::Status Overlay::ComputeHighLow() {
  if (high_low_computed_) return absl::OkStatus();

  high_low_computed_ = true;
  int position = declared_width_ - 1;
  for (auto *component : component_vec_) {
    component->set_position(position);
    if (component->high() >= 0) {
      // Field or format reference.
      if (component->field() != nullptr) {  // Field, not format reference.
        component->set_high(component->high() + component->field()->low);
        component->set_low(component->low() + component->field()->low);
      }
      mask_ |= ((1LLU << component->width()) - 1) << component->low();
    }
    position -= component->width();
  }
  return absl::OkStatus();
}

// Extract the bits from the input value according to the component
// specification of the overlay.
absl::StatusOr<uint64_t> Overlay::GetValue(uint64_t input) {
  if (declared_width_ != computed_width_)
    return absl::InternalError(
        "Overlay definition incomplete: declared width != computed width");

  uint64_t value = 0;
  for (auto *component : component_vec_) {
    if (component->high() < 0) {
      BinaryNum bin_num = component->bin_num();
      // If value == 0, nothing to or in - it just takes space.
      if (bin_num.value == 0) continue;
      int shift = component->position() - bin_num.width + 1;
      if (shift < 0)
        return absl::InternalError("Illegal shift amount in Overlay::GetValue");

      value |= bin_num.value << shift;
    } else {
      uint64_t mask = ((1ULL << component->width()) - 1) << component->low();
      int diff = component->high() - component->position();
      auto tmp = (input & mask);
      tmp = (diff < 0) ? tmp << -diff : tmp >> diff;
      value |= tmp;
    }
  }
  return value;
}

absl::StatusOr<uint64_t> Overlay::GetBitField(uint64_t input) {
  uint64_t bitfield = 0;
  for (auto *component : component_vec_) {
    // Constant bits do not map to the instruction word.
    if (component->high() < 0) continue;
    uint64_t mask = ((1ULL << component->width()) - 1);
    int shift = component->position() - component->width() + 1;
    uint64_t bits = ((input >> shift) & mask);
    bitfield |= bits << component->low();
  }
  return bitfield;
}

bool Overlay::operator==(const Overlay &rhs) const {
  if (declared_width_ > 64) {
    return WriteComplexValueExtractor("value", "result") ==
           rhs.WriteComplexValueExtractor("value", "result");
  } else {
    return WriteSimpleValueExtractor("value", "result") ==
           rhs.WriteSimpleValueExtractor("value", "result");
  }
}

bool Overlay::operator!=(const Overlay &rhs) const { return !(*this == rhs); }

// Return a string with the code (not counting function definition, variable
// definition or return statement) for extracting the value of the overlay from
// a variable 'value' and storing it into the variable 'result'. This extractor
// works when the format is <= 64 bits wide.
std::string Overlay::WriteSimpleValueExtractor(absl::string_view value,
                                               absl::string_view result) const {
  std::string output;
  std::string assign = " = ";
  for (auto *component : component_vec_) {
    if (component->high() < 0) {
      // Binary literals are added.
      BinaryNum bin_num = component->bin_num();
      // If the value is 0, no need to 'or' it in.
      if (bin_num.value == 0) continue;

      int shift = component->position() - bin_num.width + 1;
      absl::StrAppend(&output, "  ", result, assign, bin_num.value);
      if (shift > 0) {
        absl::StrAppend(&output, " << ", shift);
      } else if (shift < 0) {
        absl::StrAppend(&output, ";\n#error Illegal shift amount < 0");
      }
      absl::StrAppend(&output, ";\n");
    } else {
      // Field or format references are added.
      uint64_t mask = ((1ULL << component->width()) - 1) << component->low();
      absl::StrAppend(&output, "  ", result, assign, "(", value, " & 0x",
                      absl::Hex(mask), ")");
      int diff = component->high() - component->position();
      if (diff < 0) {
        absl::StrAppend(&output, " << ", -diff);
      } else if (diff > 0) {
        absl::StrAppend(&output, " >> ", diff);
      }
      absl::StrAppend(&output, ";\n");
    }
    assign = " |= ";
  }
  return output;
}

// Return a string with the code (not counting function definition, variable
// definition or return statement) for extracting the value of the overlay from
// a variable 'value' and storing it into the variable 'result'. This extractor
// works when the source format is => 64 bits wide.
std::string Overlay::WriteComplexValueExtractor(
    absl::string_view value, absl::string_view result) const {
  std::string output;
  std::string assign = " = ";
  for (auto *component : component_vec_) {
    if (component->high() < 0) {
      BinaryNum bin_num = component->bin_num();
      // If the value is 0, no need to 'or' it in.
      if (bin_num.value == 0) continue;

      int shift = component->position() - bin_num.width + 1;
      absl::StrAppend(&output, "  ", result, assign, bin_num.value);
      if (shift > 0) {
        absl::StrAppend(&output, " << ", shift);
      } else if (shift < 0) {
        absl::StrAppend(&output, ";\n#error Illegal shift amount < 0");
      }
      absl::StrAppend(&output, ";\n");
    } else {
      absl::StrAppend(&output, "  ", result, assign, "ExtractValue<>(", value,
                      component->high(), component->width(), ")");
      if (component->low() > 0) {
        absl::StrAppend(&output, " << ", component->low());
      }
      absl::StrAppend(&output, ";\n");
    }
    assign = " |= ";
  }
  return output;
}

}  // namespace bin_format
}  // namespace decoder
}  // namespace sim
}  // namespace mpact
