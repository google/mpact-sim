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

#include "mpact/sim/decoder/proto_constraint_value_set.h"

#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "mpact/sim/decoder/proto_constraint_expression.h"
#include "mpact/sim/decoder/proto_instruction_encoding.h"

namespace mpact {
namespace sim {
namespace decoder {
namespace proto_fmt {

// Local helper function used to get the minimum value for a given constraint
// expression based on its type.
static ProtoConstraintExpression* MinValueExpr(
    const ProtoConstraintExpression* expr) {
  auto res = expr->GetValue();
  if (!res.ok()) return nullptr;
  auto value = res.value();
  switch (value.index()) {
    case *ProtoValueIndex::kInt32:
      return new ProtoConstraintValueExpression(
          std::numeric_limits<int32_t>::min());
    case *ProtoValueIndex::kInt64:
      return new ProtoConstraintValueExpression(
          std::numeric_limits<int64_t>::min());
    case *ProtoValueIndex::kBool:
      return new ProtoConstraintValueExpression(false);
    case *ProtoValueIndex::kUint32:
      return new ProtoConstraintValueExpression(
          std::numeric_limits<uint32_t>::min());
    case *ProtoValueIndex::kUint64:
      return new ProtoConstraintValueExpression(
          std::numeric_limits<uint64_t>::min());
    case *ProtoValueIndex::kFloat:
      return new ProtoConstraintValueExpression(
          -std::numeric_limits<float>::infinity());
    case *ProtoValueIndex::kDouble:
      return new ProtoConstraintValueExpression(
          -std::numeric_limits<double>::infinity());
    default:
      return nullptr;
  }
}

// Local helper function used to get the minimum value for a given constraint
// expression based on its type.
static ProtoConstraintExpression* MaxValueExpr(
    const ProtoConstraintExpression* expr) {
  auto res = expr->GetValue();
  if (!res.ok()) return nullptr;
  auto value = res.value();
  switch (value.index()) {
    case *ProtoValueIndex::kInt32:
      return new ProtoConstraintValueExpression(
          std::numeric_limits<int32_t>::max());
    case *ProtoValueIndex::kInt64:
      return new ProtoConstraintValueExpression(
          std::numeric_limits<int64_t>::max());
    case *ProtoValueIndex::kBool:
      return new ProtoConstraintValueExpression(true);
    case *ProtoValueIndex::kUint32:
      return new ProtoConstraintValueExpression(
          std::numeric_limits<uint32_t>::max());
    case *ProtoValueIndex::kUint64:
      return new ProtoConstraintValueExpression(
          std::numeric_limits<uint64_t>::max());
    case *ProtoValueIndex::kFloat:
      return new ProtoConstraintValueExpression(
          std::numeric_limits<float>::infinity());
    case *ProtoValueIndex::kDouble:
      return new ProtoConstraintValueExpression(
          std::numeric_limits<double>::infinity());
    default:
      return nullptr;
  }
}

// Basic constructor taking explicit arguments for the data members.
ProtoConstraintValueSet::ProtoConstraintValueSet(
    const ProtoConstraintExpression* min, bool min_included,
    const ProtoConstraintExpression* max, bool max_included) {
  // If either expression is nullptr, the range is malformed and treated as
  // empty.
  if ((min == nullptr || max == nullptr)) return;
  // If the types are different, treat the range as empty.
  if (min->variant_type() != max->variant_type()) return;
  // Create the range.
  subranges_.emplace_back(min->Clone(), min_included, max->Clone(),
                          max_included);
}

// Constructor that initializes the value set based on the expression that is
// part of the constraint.
ProtoConstraintValueSet::ProtoConstraintValueSet(
    const ProtoConstraint* constraint) {
  field_descriptor_ = constraint->field_descriptor;
  auto* expr = constraint->expr;
  switch (constraint->op) {
    case ConstraintType::kEq:
      subranges_.emplace_back(expr->Clone(), true, expr->Clone(), true);
      break;
    case ConstraintType::kNe:
      subranges_.emplace_back(MinValueExpr(expr), true, expr->Clone(), false);
      subranges_.emplace_back(expr->Clone(), false, MaxValueExpr(expr), true);
      break;
    case ConstraintType::kLt:
      subranges_.emplace_back(MinValueExpr(expr), true, expr->Clone(), false);
      break;
    case ConstraintType::kLe:
      subranges_.emplace_back(MinValueExpr(expr), true, expr->Clone(), true);
      break;
    case ConstraintType::kGt:
      subranges_.emplace_back(expr->Clone(), false, MaxValueExpr(expr), true);
      break;
    case ConstraintType::kGe:
      subranges_.emplace_back(expr->Clone(), true, MaxValueExpr(expr), true);
      break;
    case ConstraintType::kHas: {
      int32_t value = -1;
      auto* field = constraint->field_descriptor;
      auto* one_of = constraint->field_descriptor->containing_oneof();
      for (int32_t i = 0; i < one_of->field_count(); ++i) {
        if (one_of->field(i)->name() == field->name()) {
          value = i;
          break;
        }
      }
      ProtoConstraintExpression* expr =
          new ProtoConstraintValueExpression(value);
      subranges_.emplace_back(expr, true, expr->Clone(), true);
      break;
    }
    default:
      break;
  }
}

// Copy constructor.
ProtoConstraintValueSet::ProtoConstraintValueSet(
    const ProtoConstraintValueSet& other) {
  field_descriptor_ = other.field_descriptor_;
  for (auto const& subrange : other.subranges_) {
    subranges_.emplace_back(
        subrange.min == nullptr ? nullptr : subrange.min->Clone(),
        subrange.min_included,
        subrange.max == nullptr ? nullptr : subrange.max->Clone(),
        subrange.max_included);
  }
}

ProtoConstraintValueSet::~ProtoConstraintValueSet() {
  for (auto& subrange : subranges_) {
    delete subrange.min;
    delete subrange.max;
  }
  subranges_.clear();
}

// Templated helper function to perform type specific intersections of value
// sets.
template <typename T>
ProtoConstraintValueSet::SubRange ProtoConstraintValueSet::IntersectSubrange(
    const SubRange& lhs_subrange, const SubRange& rhs_subrange) const {
  // If both min and max are nullptr, then lhs is already a null set.
  if ((lhs_subrange.min == nullptr) && (rhs_subrange.min == nullptr)) {
    return {nullptr, false, nullptr, false};
  }
  // If rhs min and max are nullptr, then lhs becomes a nullset.
  if ((rhs_subrange.min == nullptr) && (rhs_subrange.max == nullptr)) {
    return {nullptr, false, nullptr, false};
  }
  SubRange subrange;
  // Below, a nullptr for min/max means min/max numeric limit.
  // Get the min values.
  T min_value;
  T lhs_min_value = lhs_subrange.min == nullptr
                        ? std::numeric_limits<T>::min()
                        : lhs_subrange.min->GetValueAs<T>();
  T rhs_min_value = rhs_subrange.min == nullptr
                        ? std::numeric_limits<T>::min()
                        : rhs_subrange.min->GetValueAs<T>();
  // Get the max values.
  T max_value;
  T lhs_max_value = lhs_subrange.max == nullptr
                        ? std::numeric_limits<T>::max()
                        : lhs_subrange.max->GetValueAs<T>();
  T rhs_max_value = rhs_subrange.max == nullptr
                        ? std::numeric_limits<T>::max()
                        : rhs_subrange.max->GetValueAs<T>();
  // Check for non-overlap - if so, return the empty set.
  if ((lhs_min_value > rhs_max_value) || (lhs_max_value < rhs_min_value)) {
    return {nullptr, false, nullptr, false};
  }

  // At this point we know that they overlap (or almost overlap). It could
  // still be two (half-) open intervals that have a common point not included
  // in either.

  // Compare the values.
  if (lhs_min_value > rhs_min_value) {
    min_value = lhs_min_value;
    subrange.min = lhs_subrange.min->Clone();
    subrange.min_included = lhs_subrange.min_included;
  } else if (lhs_min_value < rhs_min_value) {
    min_value = rhs_min_value;
    subrange.min = rhs_subrange.min->Clone();
    subrange.min_included = rhs_subrange.min_included;
  } else {
    min_value = lhs_min_value;
    subrange.min = lhs_subrange.min->Clone();
    subrange.min_included =
        lhs_subrange.min_included && rhs_subrange.min_included;
  }
  // Compare the values.
  if (lhs_max_value < rhs_max_value) {
    max_value = lhs_max_value;
    subrange.max = lhs_subrange.max->Clone();
    subrange.max_included = lhs_subrange.max_included;
  } else if (lhs_max_value > rhs_max_value) {
    max_value = rhs_max_value;
    subrange.max = rhs_subrange.max->Clone();
    subrange.max_included = rhs_subrange.max_included;
  } else {
    max_value = lhs_max_value;
    subrange.max = lhs_subrange.max->Clone();
    subrange.max_included =
        lhs_subrange.max_included && rhs_subrange.max_included;
  }
  // Check for a null set if max == min, but either endpoint is not included.
  if ((min_value == max_value) &&
      !(subrange.max_included && lhs_subrange.min_included)) {
    delete subrange.min;
    delete subrange.max;
    return {nullptr, false, nullptr, false};
  }
  return subrange;
}

// Iterate over the subranges to perform subrange by subrange intersection.
template <typename T>
void ProtoConstraintValueSet::IntersectSubranges(
    const std::vector<SubRange>& lhs_subranges,
    const std::vector<SubRange>& rhs_subranges,
    std::vector<SubRange>& new_subranges) const {
  for (auto const& lhs_subrange : lhs_subranges) {
    for (auto const& rhs_subrange : rhs_subranges) {
      auto subrange = IntersectSubrange<T>(lhs_subrange, rhs_subrange);
      // Ignore empty sets.
      if ((subrange.min != nullptr) && (subrange.max != nullptr)) {
        new_subranges.emplace_back(subrange);
      }
    }
  }
}

absl::Status ProtoConstraintValueSet::IntersectWith(
    const ProtoConstraintValueSet& rhs) {
  std::vector<SubRange> new_subranges;
  // If either set is empty, the result is empty.
  if (IsEmpty() || rhs.IsEmpty()) {
    for (auto& subrange : subranges_) {
      delete subrange.min;
      delete subrange.max;
    }
    subranges_.clear();
    return absl::OkStatus();
  }
  // Get expressions to check on type compatibility. Signal error if types
  // don't match.
  auto const* lhs_expr =
      subranges_[0].min != nullptr ? subranges_[0].min : subranges_[0].max;
  auto const* rhs_expr = rhs.subranges_[0].min != nullptr
                             ? rhs.subranges_[0].min
                             : rhs.subranges_[0].max;
  if ((lhs_expr != nullptr) && (rhs_expr != nullptr) &&
      (lhs_expr->variant_type() != rhs_expr->variant_type())) {
    return absl::InvalidArgumentError(
        "ProtoConstraintValueSet::IntersectWith: type error");
  }
  // Perform the intersections.
  switch (subranges_.back().min->variant_type()) {
    case *ProtoValueIndex::kInt32:
      IntersectSubranges<int32_t>(subranges_, rhs.subranges_, new_subranges);
      break;
    case *ProtoValueIndex::kInt64:
      IntersectSubranges<int64_t>(subranges_, rhs.subranges_, new_subranges);
      break;
    case *ProtoValueIndex::kUint32:
      IntersectSubranges<uint32_t>(subranges_, rhs.subranges_, new_subranges);
      break;
    case *ProtoValueIndex::kUint64:
      IntersectSubranges<uint64_t>(subranges_, rhs.subranges_, new_subranges);
      break;
    case *ProtoValueIndex::kBool:
      IntersectSubranges<bool>(subranges_, rhs.subranges_, new_subranges);
      break;
    case *ProtoValueIndex::kFloat:
      IntersectSubranges<float>(subranges_, rhs.subranges_, new_subranges);
      break;
    case *ProtoValueIndex::kDouble:
      IntersectSubranges<double>(subranges_, rhs.subranges_, new_subranges);
      break;
    default:
      return absl::InvalidArgumentError(
          absl::StrCat("Unsupported type in range"));
  }
  // Clean up the old subranges and replace with the new subranges.
  for (auto& subrange : subranges_) {
    delete subrange.min;
    delete subrange.max;
  }
  subranges_.clear();
  subranges_ = std::move(new_subranges);
  return absl::OkStatus();
}

absl::Status ProtoConstraintValueSet::UnionWith(
    const ProtoConstraintValueSet& rhs) {
  if (rhs.IsEmpty()) return absl::OkStatus();
  // Copy the rhs subranges.
  for (auto& subrange : rhs.subranges_) {
    SubRange new_subrange;
    new_subrange.min =
        subrange.min != nullptr ? subrange.min->Clone() : nullptr;
    new_subrange.min_included = subrange.min_included;
    new_subrange.max =
        subrange.max != nullptr ? subrange.max->Clone() : nullptr;
    new_subrange.max_included = subrange.max_included;
    subranges_.emplace_back(new_subrange);
  }
  return absl::OkStatus();
}

bool ProtoConstraintValueSet::IsEmpty() const { return subranges_.empty(); }

}  // namespace proto_fmt
}  // namespace decoder
}  // namespace sim
}  // namespace mpact
