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

#ifndef MPACT_SIM_DECODER_PROTO_CONSTRAINT_EXPRESSION_H_
#define MPACT_SIM_DECODER_PROTO_CONSTRAINT_EXPRESSION_H_

#include <cstddef>
#include <cstdint>
#include <string>
#include <type_traits>
#include <variant>

#include "absl/log/log.h"
#include "absl/status/statusor.h"
#include "mpact/sim/generic/type_helpers.h"
#include "src/google/protobuf/descriptor.h"

namespace mpact {
namespace sim {
namespace decoder {
namespace proto_fmt {

// This file defines the expression types used in the constraints specified
// for the instruction encodings.
// TODO(torerik): add a richer set of expressions.

// Variant type used to represent a proto expression value.
using ProtoValue = std::variant<int32_t, int64_t, uint32_t, uint64_t, double,
                                float, bool, std::string>;
using ::mpact::sim::generic::operator*;

// Indices of the types in the ProtoValue variant.
enum class ProtoValueIndex : std::size_t {
  kInt32 = 0,
  kInt64 = 1,
  kUint32 = 2,
  kUint64 = 3,
  kDouble = 4,
  kFloat = 5,
  kBool = 6,
  kString = 7
};

// Return true if the type index is that of an integer type ([u]int(32|64)_t).
inline bool IsIntType(size_t type_index) {
  return type_index <= *ProtoValueIndex::kUint64;
}

// Maps from proto cpp type to variant type index.
constexpr int kCppToVariantTypeMap[] = {-1,
                                        *ProtoValueIndex::kInt32,
                                        *ProtoValueIndex::kInt64,
                                        *ProtoValueIndex::kUint32,
                                        *ProtoValueIndex::kUint64,
                                        *ProtoValueIndex::kDouble,
                                        *ProtoValueIndex::kFloat,
                                        *ProtoValueIndex::kBool,
                                        *ProtoValueIndex::kInt32,
                                        *ProtoValueIndex::kString,
                                        -1};

// Mapping from protobuf variant type index to proto cpp field types.
constexpr google::protobuf::FieldDescriptor::CppType kVariantToCppTypeMap[] = {
    google::protobuf::FieldDescriptor::CPPTYPE_INT32,
    google::protobuf::FieldDescriptor::CPPTYPE_INT64,
    google::protobuf::FieldDescriptor::CPPTYPE_UINT32,
    google::protobuf::FieldDescriptor::CPPTYPE_UINT64,
    google::protobuf::FieldDescriptor::CPPTYPE_DOUBLE,
    google::protobuf::FieldDescriptor::CPPTYPE_FLOAT,
    google::protobuf::FieldDescriptor::CPPTYPE_BOOL,
    google::protobuf::FieldDescriptor::CPPTYPE_STRING,
};

std::string GetCppTypeName(google::protobuf::FieldDescriptor::CppType cpp_type);

// Helper templates for comparing CPP type to actual type.
template <typename T>
struct CppType;

template <>
struct CppType<int32_t> {
  static constexpr int value = google::protobuf::FieldDescriptor::CPPTYPE_INT32;
};
template <>
struct CppType<int64_t> {
  static constexpr int value = google::protobuf::FieldDescriptor::CPPTYPE_INT64;
};
template <>
struct CppType<uint32_t> {
  static constexpr int value =
      google::protobuf::FieldDescriptor::CPPTYPE_UINT32;
};
template <>
struct CppType<uint64_t> {
  static constexpr int value =
      google::protobuf::FieldDescriptor::CPPTYPE_UINT64;
};
template <>
struct CppType<double> {
  static constexpr int value =
      google::protobuf::FieldDescriptor::CPPTYPE_DOUBLE;
};
template <>
struct CppType<float> {
  static constexpr int value = google::protobuf::FieldDescriptor::CPPTYPE_FLOAT;
};
template <>
struct CppType<bool> {
  static constexpr int value = google::protobuf::FieldDescriptor::CPPTYPE_BOOL;
};
template <>
struct CppType<std::string> {
  static constexpr int value =
      google::protobuf::FieldDescriptor::CPPTYPE_STRING;
};

// Expression base class.
class ProtoConstraintExpression {
 public:
  virtual ~ProtoConstraintExpression() = default;
  virtual absl::StatusOr<ProtoValue> GetValue() const = 0;
  template <typename T>
  T GetValueAs() const {
    auto res = this->GetValue();
    // If there is a error, or the value is of a different type, just return
    // the default constructed object of the desired type.
    if (!res.ok() || !std::holds_alternative<T>(res.value())) return T();
    return std::get<T>(res.value());
  }
  virtual ProtoConstraintExpression *Clone() const = 0;
  virtual google::protobuf::FieldDescriptor::CppType cpp_type() const = 0;
  virtual int variant_type() const = 0;
};

// Unary negate expression.
class ProtoConstraintNegateExpression : public ProtoConstraintExpression {
 public:
  explicit ProtoConstraintNegateExpression(ProtoConstraintExpression *expr)
      : expr_(expr) {}
  ~ProtoConstraintNegateExpression() override;
  absl::StatusOr<ProtoValue> GetValue() const override;
  ProtoConstraintExpression *Clone() const override {
    return new ProtoConstraintNegateExpression(expr_->Clone());
  }
  google::protobuf::FieldDescriptor::CppType cpp_type() const override {
    return expr_->cpp_type();
  }
  int variant_type() const override { return expr_->variant_type(); }

 private:
  ProtoConstraintExpression *expr_;
};

// Enumeration value expression.
class ProtoConstraintEnumExpression : public ProtoConstraintExpression {
 public:
  explicit ProtoConstraintEnumExpression(
      const google::protobuf::EnumValueDescriptor *enum_value)
      : enum_value_(enum_value) {}
  ~ProtoConstraintEnumExpression() override = default;
  absl::StatusOr<ProtoValue> GetValue() const override;
  ProtoConstraintExpression *Clone() const override {
    return new ProtoConstraintEnumExpression(enum_value_);
  }
  google::protobuf::FieldDescriptor::CppType cpp_type() const override {
    return google::protobuf::FieldDescriptor::CPPTYPE_INT32;
  }
  int variant_type() const override { return *ProtoValueIndex::kInt32; }

 private:
  const google::protobuf::EnumValueDescriptor *enum_value_ = nullptr;
};

// Constant value expression.
class ProtoConstraintValueExpression : public ProtoConstraintExpression {
 public:
  explicit ProtoConstraintValueExpression(const ProtoValue &value)
      : value_(value) {}
  template <typename T>
  explicit ProtoConstraintValueExpression(const T &value) : value_(value) {}

  ~ProtoConstraintValueExpression() override = default;
  absl::StatusOr<ProtoValue> GetValue() const override { return value_; }
  ProtoConstraintExpression *Clone() const override {
    return new ProtoConstraintValueExpression(value_);
  }
  google::protobuf::FieldDescriptor::CppType cpp_type() const override {
    return kVariantToCppTypeMap[value_.index()];
  }
  int variant_type() const override { return value_.index(); }

 private:
  ProtoValue value_;
};

// These static asserts are used to make sure that the indices in
// ProtoValueIndex map to the correct type in ProtoValue.
static_assert(
    std::is_same_v<int32_t, std::variant_alternative_t<*ProtoValueIndex::kInt32,
                                                       ProtoValue>>);
static_assert(
    std::is_same_v<int64_t, std::variant_alternative_t<*ProtoValueIndex::kInt64,
                                                       ProtoValue>>);
static_assert(
    std::is_same_v<uint32_t, std::variant_alternative_t<
                                 *ProtoValueIndex::kUint32, ProtoValue>>);
static_assert(
    std::is_same_v<uint64_t, std::variant_alternative_t<
                                 *ProtoValueIndex::kUint64, ProtoValue>>);
static_assert(
    std::is_same_v<
        bool, std::variant_alternative_t<*ProtoValueIndex::kBool, ProtoValue>>);
static_assert(
    std::is_same_v<double, std::variant_alternative_t<*ProtoValueIndex::kDouble,
                                                      ProtoValue>>);
static_assert(std::is_same_v<float, std::variant_alternative_t<
                                        *ProtoValueIndex::kFloat, ProtoValue>>);
static_assert(
    std::is_same_v<std::string, std::variant_alternative_t<
                                    *ProtoValueIndex::kString, ProtoValue>>);

}  // namespace proto_fmt
}  // namespace decoder
}  // namespace sim
}  // namespace mpact

#endif  // MPACT_SIM_DECODER_PROTO_CONSTRAINT_EXPRESSION_H_
