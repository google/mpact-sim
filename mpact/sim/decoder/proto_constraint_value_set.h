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

#ifndef MPACT_SIM_DECODER_PROTO_CONSTRAINT_VALUE_SET_H_
#define MPACT_SIM_DECODER_PROTO_CONSTRAINT_VALUE_SET_H_

#include <vector>

#include "absl/status/status.h"
#include "mpact/sim/decoder/proto_constraint_expression.h"
#include "src/google/protobuf/descriptor.h"

// This file implements the ProtoConstraintValueSet class which represents the
// set of values a ProtoConstraint may take.
namespace mpact {
namespace sim {
namespace decoder {
namespace proto_fmt {

class ProtoConstraintExpression;
struct ProtoConstraint;

// This class implements the value set of a constraint as a vector of
// sub-ranges. The sub-ranges may overlap, there is no guarantee that they are
// the minimum set of sub-ranges that describe the set of values. In part this
// is because we are more interested in weather two value sets intersect or not,
// and thus there is no need to for that guarantee.
class ProtoConstraintValueSet {
 public:
  struct SubRange {
    const ProtoConstraintExpression* min = nullptr;
    bool min_included;
    const ProtoConstraintExpression* max = nullptr;
    bool max_included;
    SubRange() = default;
    SubRange(const ProtoConstraintExpression* min, bool min_included,
             const ProtoConstraintExpression* max, bool max_included)
        : min(min),
          min_included(min_included),
          max(max),
          max_included(max_included) {}
  };

  ProtoConstraintValueSet() = default;
  ProtoConstraintValueSet(const ProtoConstraintExpression* min,
                          bool min_included,
                          const ProtoConstraintExpression* max,
                          bool max_included);
  explicit ProtoConstraintValueSet(const ProtoConstraint* constraint);
  explicit ProtoConstraintValueSet(const ProtoConstraint& constraint)
      : ProtoConstraintValueSet(&constraint) {}
  ProtoConstraintValueSet(const ProtoConstraintValueSet& other);
  ~ProtoConstraintValueSet();

  // Intersect rhs with this, modifying this value set.
  absl::Status IntersectWith(const ProtoConstraintValueSet& rhs);
  // Simply add the sub ranges associated with rhs to this.
  absl::Status UnionWith(const ProtoConstraintValueSet& rhs);

  bool IsEmpty() const;

  // Accessors.
  const std::vector<SubRange>& subranges() const { return subranges_; }
  const google::protobuf::FieldDescriptor* field_descriptor() const {
    return field_descriptor_;
  }

 private:
  // Private templated helper methods for intersection. The definitions are in
  // the .cc file.
  template <typename T>
  SubRange IntersectSubrange(const SubRange& lhs_subrange,
                             const SubRange& rhs_subrange) const;
  template <typename T>
  void IntersectSubranges(const std::vector<SubRange>& lhs_subranges,
                          const std::vector<SubRange>& rhs_subranges,
                          std::vector<SubRange>& new_subranges) const;
  // The range consists of a union of a number of subranges.
  std::vector<SubRange> subranges_;
  const google::protobuf::FieldDescriptor* field_descriptor_ = nullptr;
};

}  // namespace proto_fmt
}  // namespace decoder
}  // namespace sim
}  // namespace mpact

#endif  // MPACT_SIM_DECODER_PROTO_CONSTRAINT_VALUE_SET_H_
