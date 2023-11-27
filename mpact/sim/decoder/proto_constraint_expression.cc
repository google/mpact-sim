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

#include "mpact/sim/decoder/proto_constraint_expression.h"

#include <cstdint>
#include <string>

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "mpact/sim/decoder/proto_instruction_encoding.h"
#include "mpact/sim/generic/type_helpers.h"
#include "src/google/protobuf/descriptor.h"

namespace mpact {
namespace sim {
namespace decoder {
namespace proto_fmt {

// Map a proto cpp_type to a C++ type string.
std::string GetCppTypeName(proto2::FieldDescriptor::CppType cpp_type) {
  switch (cpp_type) {
    case proto2::FieldDescriptor::CPPTYPE_INT32:
      return "int32_t";
    case proto2::FieldDescriptor::CPPTYPE_INT64:
      return "int64_t";
    case proto2::FieldDescriptor::CPPTYPE_UINT32:
      return "uint32_t";
    case proto2::FieldDescriptor::CPPTYPE_UINT64:
      return "uint64_t";
    case proto2::FieldDescriptor::CPPTYPE_BOOL:
      return "bool";
    case proto2::FieldDescriptor::CPPTYPE_FLOAT:
      return "float";
    case proto2::FieldDescriptor::CPPTYPE_DOUBLE:
      return "double";
    case proto2::FieldDescriptor::CPPTYPE_STRING:
      return "std::string";
    default:
      return "void";
  }
}

// Operator to implement negation.
absl::StatusOr<ProtoValue> operator-(const ProtoValue &value) {
  switch (value.index()) {
    case *ProtoValueIndex::kInt32:
      return -std::get<int32_t>(value);
    case *ProtoValueIndex::kInt64:
      return -std::get<int64_t>(value);
    case *ProtoValueIndex::kUint32:
      return -std::get<uint32_t>(value);
    case *ProtoValueIndex::kUint64:
      return -std::get<uint64_t>(value);
    case *ProtoValueIndex::kBool:
      return !std::get<bool>(value);
    case *ProtoValueIndex::kFloat:
      return -std::get<float>(value);
    case *ProtoValueIndex::kDouble:
      return -std::get<double>(value);
    default:
      return absl::InvalidArgumentError(
          absl::StrCat("Unsupported type in negation (", value.index(), ")"));
  }
}

ProtoConstraintNegateExpression::~ProtoConstraintNegateExpression() {
  delete expr_;
  expr_ = nullptr;
}

absl::StatusOr<ProtoValue> ProtoConstraintNegateExpression::GetValue() const {
  auto res = expr_->GetValue();
  if (!res.ok()) return res;
  return -res.value();
}

absl::StatusOr<ProtoValue> ProtoConstraintEnumExpression::GetValue() const {
  if (enum_value_ == nullptr) {
    return absl::InvalidArgumentError("Enum value is null");
  }
  return enum_value_->number();
}

}  // namespace proto_fmt
}  // namespace decoder
}  // namespace sim
}  // namespace mpact
