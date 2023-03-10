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

#include "mpact/sim/decoder/instruction_encoding.h"

#include <string>

#include "absl/status/status.h"

namespace mpact {
namespace sim {
namespace decoder {
namespace bin_format {

InstructionEncoding::InstructionEncoding(std::string name, Format *format)
    : name_(name), format_(format) {}

// Custom copy constructor.
InstructionEncoding::InstructionEncoding(const InstructionEncoding &encoding)
    : name_(encoding.name_),
      format_name_(encoding.format_name_),
      format_(encoding.format_),
      mask_set_(encoding.mask_set_),
      mask_(encoding.mask_),
      other_mask_(encoding.other_mask_),
      extracted_mask_(encoding.extracted_mask_),
      value_(encoding.value_) {
  // Copy construct each of the constraints.
  for (auto *constraint : encoding.equal_constraints_) {
    equal_constraints_.push_back(new Constraint(*constraint));
  }
  for (auto *constraint : encoding.equal_extracted_constraints_) {
    equal_extracted_constraints_.push_back(new Constraint(*constraint));
  }
  for (auto *constraint : encoding.other_constraints_) {
    other_constraints_.push_back(new Constraint(*constraint));
  }
}

absl::StatusOr<Constraint *> InstructionEncoding::CreateConstraint(
    ConstraintType type, std::string field_name, int64_t value) {
  // Check if the field name is indeed a field.
  auto *field = format_->GetField(field_name);
  if (field != nullptr) {
    if (value >= (1 << field->width)) {
      return absl::InternalError(
          absl::StrCat("Constraint value (", value,
                       ") exceeds field width for field '", field_name, "'"));
    }
    auto *constraint = new Constraint();
    constraint->type = type;
    constraint->field = field;
    constraint->value = value;
    return constraint;
  }
  // If not a field, is it an overlay?
  auto *overlay = format_->GetOverlay(field_name);
  if (overlay == nullptr) {
    // If neither, it's an error.
    return absl::InternalError(absl::StrCat(
        "Format '", format_->name(),
        "' does not contain a field or overlay named ", field_name));
  }
  if (value >= (1 << overlay->computed_width())) {
    return absl::InternalError(absl::StrCat(
        "Constraint value exceeds field width for field '", field_name, "'"));
  }
  auto *constraint = new Constraint();
  constraint->type = type;
  constraint->overlay = overlay;
  constraint->value = value;

  return constraint;
}

InstructionEncoding::~InstructionEncoding() {
  for (auto *constraint : equal_constraints_) {
    delete constraint;
  }
  equal_constraints_.clear();
  for (auto *constraint : equal_extracted_constraints_) {
    delete constraint;
  }
  equal_extracted_constraints_.clear();
  for (auto *constraint : other_constraints_) {
    delete constraint;
  }
  other_constraints_.clear();
}

absl::Status InstructionEncoding::AddEqualConstraint(std::string field_name,
                                                     int64_t value) {
  // Invalidate the computed masks and values when a new constraint is added.
  mask_set_ = false;
  auto res = CreateConstraint(ConstraintType::kEq, field_name, value);
  if (!res.ok()) return res.status();
  auto *constraint = res.value();
  if ((constraint->overlay != nullptr) &&
      constraint->overlay->must_be_extracted()) {
    // If the value is not 100% based on extracted bits (i.e., it is an
    // overlay that has constant bits concatenated, then the value cannot be
    // compared directly against a masked value of the instruction, but have
    // to use an extractor for the overlay first.
    equal_extracted_constraints_.push_back(constraint);
  } else {
    equal_constraints_.push_back(constraint);
  }
  return absl::OkStatus();
}

absl::Status InstructionEncoding::AddOtherConstraint(ConstraintType type,
                                                     std::string field_name,
                                                     int64_t value) {
  auto res = CreateConstraint(type, field_name, value);
  if (!res.ok()) return res.status();
  auto *constraint = res.value();
  other_constraints_.push_back(constraint);
  return absl::OkStatus();
}

absl::Status InstructionEncoding::ComputeMaskAndValue() {
  // First consider equal constraints.
  mask_ = 0;
  for (auto *constraint : equal_constraints_) {
    uint64_t mask = 0;
    uint64_t value = 0;
    if (constraint->field != nullptr) {
      int width = constraint->field->width;
      mask = (1LLU << width) - 1;
      int shift = constraint->field->low;
      mask <<= shift;
      value = constraint->value << shift;
    } else {
      auto res = constraint->overlay->GetBitField(constraint->value);
      if (!res.ok()) return res.status();
      value = res.value();
      mask = constraint->overlay->mask();
    }
    value_ |= mask & value;
    mask_ |= mask;
  }
  // The overlays with bit constant concatenations.
  extracted_mask_ = 0;
  for (auto *constraint : equal_extracted_constraints_) {
    uint64_t mask = 0;
    if (constraint->field != nullptr) {
      int width = constraint->field->width;
      mask = (1LLU << width) - 1;
      int shift = constraint->field->low;
      mask <<= shift;
    } else {
      mask = constraint->overlay->mask();
    }
    extracted_mask_ |= mask;
  }
  // Other constraints.
  other_mask_ = 0;
  for (auto *constraint : other_constraints_) {
    uint64_t mask = 0;
    if (constraint->field != nullptr) {
      int width = constraint->field->width;
      mask = (1LLU << width) - 1;
      int shift = constraint->field->low;
      mask <<= shift;
    } else {
      mask = constraint->overlay->mask();
    }
    other_mask_ |= mask;
  }
  mask_set_ = true;
  return absl::OkStatus();
}

uint64_t InstructionEncoding::GetMask() {
  if (!mask_set_) {
    auto result = ComputeMaskAndValue();
    if (!result.ok()) {
      format_->encoding_info()->error_listener()->semanticError(
          nullptr, "Internal Error in GetMask()");
    }
  }
  return mask_;
}

uint64_t InstructionEncoding::GetCombinedMask() {
  if (!mask_set_) {
    auto result = ComputeMaskAndValue();
    if (!result.ok()) {
      format_->encoding_info()->error_listener()->semanticError(
          nullptr, "Internal Error in GetCombinedMask()");
    }
  }
  return mask_ | extracted_mask_ | other_mask_;
}

uint64_t InstructionEncoding::GetValue() {
  if (!mask_set_) {
    auto result = ComputeMaskAndValue();
    if (!result.ok()) {
      format_->encoding_info()->error_listener()->semanticError(
          nullptr, "Internal Error in GetValue()");
    }
  }
  return value_;
}

}  // namespace bin_format
}  // namespace decoder
}  // namespace sim
}  // namespace mpact
