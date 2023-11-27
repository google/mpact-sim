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

#ifndef MPACT_SIM_DECODER_PROTO_INSTRUCTION_ENCODING_H_
#define MPACT_SIM_DECODER_PROTO_INSTRUCTION_ENCODING_H_

#include <cstdint>
#include <string>
#include <vector>

#include "absl/container/btree_map.h"
#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "mpact/sim/decoder/proto_constraint_expression.h"
#include "mpact/sim/decoder/proto_format_contexts.h"
#include "src/google/protobuf/descriptor.h"

namespace proto2 {
class FieldDescriptor;
}

namespace mpact {
namespace sim {
namespace decoder {
namespace proto_fmt {

enum class ConstraintType : int { kEq = 0, kNe, kLt, kLe, kGt, kGe, kHas };

inline std::string GetOpText(ConstraintType op) {
  switch (op) {
    case ConstraintType::kEq:
      return "==";
    case ConstraintType::kNe:
      return "!=";
    case ConstraintType::kLt:
      return "<";
    case ConstraintType::kLe:
      return "<=";
    case ConstraintType::kGt:
      return ">";
    case ConstraintType::kGe:
      return ">=";
    case ConstraintType::kHas:
      return "";
  }
}

// Struct to store information about an encoding constraint for an instruction.
struct ProtoConstraint {
  // Parsing context.
  FieldConstraintCtx *ctx;
  // The proto field descriptor for which the constraint applies.
  const proto2::FieldDescriptor *field_descriptor;
  // The constraint type.
  ConstraintType op;
  // If non-null, the expression that applies to the constraint.
  const ProtoConstraintExpression *expr;
  // If the value is compatible with int64_t, the value of the expression. This
  // is filled in later when the expression is evaluated for decoding purposes.
  int64_t value;
  // If non-null, points to a constraint that has to be true before one can
  // evaluate this constraint.
  ProtoConstraint *depends_on;
  // Constructors.
  ProtoConstraint(FieldConstraintCtx *ctx,
                  const proto2::FieldDescriptor *field_descriptor,
                  ConstraintType op, const ProtoConstraintExpression *expr,
                  int64_t value, ProtoConstraint *depends_on)
      : ctx(ctx),
        field_descriptor(field_descriptor),
        op(op),
        expr(expr),
        value(value),
        depends_on(depends_on) {}
  ProtoConstraint(FieldConstraintCtx *ctx,
                  const proto2::FieldDescriptor *field_descriptor,
                  ConstraintType op)
      : ProtoConstraint(ctx, field_descriptor, op, nullptr, 0, nullptr) {}
  // Copy constructor.
  ProtoConstraint(const ProtoConstraint &rhs) {
    this->ctx = rhs.ctx;
    this->field_descriptor = rhs.field_descriptor;
    this->op = rhs.op;
    this->expr = rhs.expr != nullptr ? rhs.expr->Clone() : nullptr;
    this->value = rhs.value;
    this->depends_on = rhs.depends_on;
  }
};

// Struct to store information about a setter for an instruction encoding.
struct ProtoSetter {
  // Proto setter context.
  SetterDefCtx *ctx;
  // The name of the object that is set.
  std::string name;
  // The field that will provide the type and value of the object.
  const proto2::FieldDescriptor *field_descriptor;
  // Default value of the object if the field descriptor is not valid.
  IfNotCtx *if_not;
  // If non-null, points to constraint that has to be true in order to access
  // the value of the field described by field_descriptor.
  ProtoConstraint *depends_on;
};

class ProtoInstructionGroup;

// This class captures all the encoding constraints and the setters for one
// instruction in an instruction group.
class ProtoInstructionEncoding {
 public:
  ProtoInstructionEncoding(std::string name, ProtoInstructionGroup *parent);
  ProtoInstructionEncoding(const ProtoInstructionEncoding &rhs);
  ProtoInstructionEncoding() = delete;
  ProtoInstructionEncoding &operator=(
      const ProtoInstructionEncoding &encoding) = delete;
  ~ProtoInstructionEncoding();

  // Adds a value setter that is executed when the instruction is successfully
  // decoded. This is used to make values, such as register numbers, immediate
  // values, etc., that could be stored in a nested one_of submessage, available
  // at known names.
  absl::Status AddSetter(
      SetterDefCtx *ctx, const std::string &name,
      const proto2::FieldDescriptor *field_descriptor,
      const std::vector<const proto2::FieldDescriptor *> &one_of_fields,
      IfNotCtx *if_not);
  // Adds an encoding constraint for the current instruction. Encoding
  // constraints provide constraints on values of proto message fields that
  // have to be satisfied in order for the instruction to match.
  absl::Status AddConstraint(
      FieldConstraintCtx *ctx, ConstraintType op,
      const proto2::FieldDescriptor *field_descriptor,
      const std::vector<const proto2::FieldDescriptor *> &one_of_fields,
      const ProtoConstraintExpression *expr);

  // Call when the setters and constraints have been added in order to generate
  // the setter code into the setter_code_ variable.
  void GenerateSetterCode();
  // Get setter code, substituting 'message_name' for '$' in the text.
  std::string GetSetterCode(absl::string_view message_name, int indent) const;
  // Getters.
  const std::string &name() const { return name_; }
  ProtoInstructionGroup *instruction_group() const {
    return instruction_group_;
  }
  std::vector<ProtoConstraint *> &equal_constraints() {
    return equal_constraints_;
  }
  std::vector<ProtoConstraint *> &other_constraints() {
    return other_constraints_;
  }
  absl::flat_hash_map<std::string, ProtoConstraint *> &has_constraints() {
    return has_constraints_;
  }

  std::string setter_code() const { return setter_code_; }

 private:
  // Returns a pointer to the constraint for field_descriptor if it exists. If
  // it does not exist, it creates it and adds it to the has_constraints and
  // returns a pointer to the new constraint. If depends_on is not nullptr,
  // then it is required that the depends_on constraint exists in the
  // has_constraints_ map. This is checked by searching for the full_name of
  // the field_descriptor in the depends_on constraint.
  ProtoConstraint *AddHasConstraint(
      const proto2::FieldDescriptor *field_descriptor,
      ProtoConstraint *depends_on);

  // Instruction name.
  std::string name_;
  // Parent instruction group.
  ProtoInstructionGroup *instruction_group_ = nullptr;
  // Setter code for this encoding.
  std::string setter_code_;
  // Map from setter names to the setter structs.
  absl::btree_map<std::string, ProtoSetter *> setter_map_;
  // Map from one_of descriptor to field.
  absl::flat_hash_map<const proto2::OneofDescriptor *,
                      const proto2::FieldDescriptor *>
      oneof_field_map_;
  // "equal-to" field constraints.
  std::vector<ProtoConstraint *> equal_constraints_;
  // All other constraints.
  std::vector<ProtoConstraint *> other_constraints_;
  // Has Constraints, these are required one_of members that other constraints
  // may depend on.
  absl::flat_hash_map<std::string, ProtoConstraint *> has_constraints_;
};

}  // namespace proto_fmt
}  // namespace decoder
}  // namespace sim
}  // namespace mpact

#endif  // MPACT_SIM_DECODER_PROTO_INSTRUCTION_ENCODING_H_
