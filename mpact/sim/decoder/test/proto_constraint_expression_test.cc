#include "mpact/sim/decoder/proto_constraint_expression.h"

#include <cstdint>
#include <string>
#include <variant>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "googletest/include/gtest/gtest.h"
#include "mpact/sim/generic/type_helpers.h"
#include "src/google/protobuf/compiler/importer.h"
#include "src/google/protobuf/descriptor.h"

// This file implements unit tests on the proto constraint expressions.

namespace {

using ::mpact::sim::decoder::proto_fmt::CppType;
using ::mpact::sim::decoder::proto_fmt::ProtoConstraintEnumExpression;
using ::mpact::sim::decoder::proto_fmt::ProtoConstraintNegateExpression;
using ::mpact::sim::decoder::proto_fmt::ProtoConstraintValueExpression;
using ::mpact::sim::decoder::proto_fmt::ProtoValue;
using ::mpact::sim::decoder::proto_fmt::ProtoValueIndex;

constexpr char kIsaProto[] = "mpact/sim/decoder/test/testfiles/riscv32i.proto";

// Need to implement RecordError.
class MultiFileErrorCollector
    : public google::protobuf::compiler::MultiFileErrorCollector {
 public:
  MultiFileErrorCollector() {}
  MultiFileErrorCollector(const MultiFileErrorCollector &) = delete;
  MultiFileErrorCollector &operator=(const MultiFileErrorCollector &) = delete;

  void RecordError(absl::string_view filename, int line, int column,
                   absl::string_view message) override {
    LOG(ERROR) << absl::StrCat("Line ", line, " Column ", column, ": ", message,
                               "\n");
    absl::StrAppend(&error_, "Line ", line, " Column ", column, ": ", message,
                    "\n");
  }
  const std::string &GetError() const { return error_; }

 private:
  std::string error_;
};

class ProtoConstraintExpressionTest : public ::testing::Test {
 protected:
  ProtoConstraintExpressionTest() {}
};

// Verify the value indices of the types.
TEST_F(ProtoConstraintExpressionTest, ValueIndex) {
  ProtoValue val = static_cast<int32_t>(-1);
  EXPECT_EQ(val.index(), *ProtoValueIndex::kInt32);
  val = static_cast<int64_t>(-1);
  EXPECT_EQ(val.index(), *ProtoValueIndex::kInt64);
  val = static_cast<uint32_t>(1);
  EXPECT_EQ(val.index(), *ProtoValueIndex::kUint32);
  val = static_cast<uint64_t>(1);
  EXPECT_EQ(val.index(), *ProtoValueIndex::kUint64);
  val = static_cast<double>(1.0);
  EXPECT_EQ(val.index(), *ProtoValueIndex::kDouble);
  val = static_cast<float>(1.0);
  EXPECT_EQ(val.index(), *ProtoValueIndex::kFloat);
  val = static_cast<bool>(true);
  EXPECT_EQ(val.index(), *ProtoValueIndex::kBool);
  val = static_cast<std::string>("hello world");
  EXPECT_EQ(val.index(), *ProtoValueIndex::kString);
}

// Verify the operation of an enumeration expression.
TEST_F(ProtoConstraintExpressionTest, EnumExpression) {
  // Read in the proto file and initialize the importer pool.
  MultiFileErrorCollector error_collector;
  google::protobuf::compiler::DiskSourceTree source_tree;
  source_tree.MapPath("", "");
  google::protobuf::compiler::Importer importer(&source_tree, &error_collector);
  auto const *isa_descriptor = importer.Import(kIsaProto);
  CHECK_NE(isa_descriptor, nullptr);
  auto const *pool = importer.pool();
  CHECK_NE(pool, nullptr);
  // Get the enum value descriptor.
  auto const *enum_value_desc =
      pool->FindEnumValueByName("mpact_sim.decoder.test.OPCODE_ADD");
  CHECK_NE(enum_value_desc, nullptr);
  int enum_value = enum_value_desc->number();
  // Create a constraint enum expression using the enum value descriptor.
  ProtoConstraintEnumExpression enum_expr(enum_value_desc);
  // Verify the type of the enum expr.
  EXPECT_EQ(enum_expr.cpp_type(),
            google::protobuf::FieldDescriptor::CPPTYPE_INT32);
  auto res = enum_expr.GetValue();
  // Get the value (std::variant)
  EXPECT_TRUE(res.status().ok());
  auto expr_value = res.value();
  // Verify that the type is as expected.
  EXPECT_EQ(expr_value.index(), *ProtoValueIndex::kInt32);
  // Get the typed value.
  auto const *value = std::get_if<int32_t>(&expr_value);
  CHECK_NE(value, nullptr);
  // Match the value against that obtained from the enum value descriptor.
  EXPECT_EQ(*value, enum_value);
}

// For each type, check the value expressions.
TEST_F(ProtoConstraintExpressionTest, ValueExpressionInt32) {
  ProtoConstraintValueExpression expr(ProtoValue(static_cast<int32_t>(-1)));
  EXPECT_EQ(CppType<int32_t>::value, expr.cpp_type());
  EXPECT_EQ(std::get<int32_t>(expr.GetValue().value()), -1);
}

TEST_F(ProtoConstraintExpressionTest, ValueExpressionInt64) {
  ProtoConstraintValueExpression expr(ProtoValue(static_cast<int64_t>(-1)));
  EXPECT_EQ(CppType<int64_t>::value, expr.cpp_type());
  EXPECT_EQ(std::get<int64_t>(expr.GetValue().value()), -1);
}

TEST_F(ProtoConstraintExpressionTest, ValueExpressionUint32) {
  ProtoConstraintValueExpression expr(ProtoValue(static_cast<uint32_t>(1)));
  EXPECT_EQ(CppType<uint32_t>::value, expr.cpp_type());
  EXPECT_EQ(std::get<uint32_t>(expr.GetValue().value()), 1);
}

TEST_F(ProtoConstraintExpressionTest, ValueExpressionUint64) {
  ProtoConstraintValueExpression expr(ProtoValue(static_cast<uint64_t>(1)));
  EXPECT_EQ(CppType<uint64_t>::value, expr.cpp_type());
  EXPECT_EQ(std::get<uint64_t>(expr.GetValue().value()), 1);
}

TEST_F(ProtoConstraintExpressionTest, ValueExpressionDouble) {
  ProtoConstraintValueExpression expr(ProtoValue(static_cast<double>(-1.0)));
  EXPECT_EQ(CppType<double>::value, expr.cpp_type());
  EXPECT_EQ(std::get<double>(expr.GetValue().value()), -1.0);
}

TEST_F(ProtoConstraintExpressionTest, ValueExpressionFloat) {
  ProtoConstraintValueExpression expr(ProtoValue(static_cast<float>(-1.0)));
  EXPECT_EQ(CppType<float>::value, expr.cpp_type());
  EXPECT_EQ(std::get<float>(expr.GetValue().value()), -1.0);
}

TEST_F(ProtoConstraintExpressionTest, ValueExpressionBool) {
  ProtoConstraintValueExpression expr(ProtoValue(static_cast<bool>(true)));
  EXPECT_EQ(CppType<bool>::value, expr.cpp_type());
  EXPECT_EQ(std::get<bool>(expr.GetValue().value()), true);
}

TEST_F(ProtoConstraintExpressionTest, ValueExpressionString) {
  ProtoConstraintValueExpression expr(
      ProtoValue(static_cast<std::string>("hello world")));
  EXPECT_EQ(CppType<std::string>::value, expr.cpp_type());
  EXPECT_EQ(std::get<std::string>(expr.GetValue().value()), "hello world");
}

// Test the negate expression.
TEST_F(ProtoConstraintExpressionTest, NegateExpression) {
  // int32_t
  auto *val_expr =
      new ProtoConstraintValueExpression(ProtoValue(static_cast<int32_t>(-1)));
  ProtoConstraintNegateExpression neg_expr(val_expr);
  EXPECT_EQ(CppType<int32_t>::value, neg_expr.cpp_type());
  EXPECT_EQ(std::get<int32_t>(neg_expr.GetValue().value()), 1);
  // uint32_t
  val_expr =
      new ProtoConstraintValueExpression(ProtoValue(static_cast<uint32_t>(1)));
  ProtoConstraintNegateExpression neg_uint32_expr(val_expr);
  EXPECT_EQ(CppType<uint32_t>::value, neg_uint32_expr.cpp_type());
  EXPECT_EQ(std::get<uint32_t>(neg_uint32_expr.GetValue().value()),
            0xffff'ffff);
  // bool
  val_expr =
      new ProtoConstraintValueExpression(ProtoValue(static_cast<bool>(true)));
  ProtoConstraintNegateExpression neg_bool_expr(val_expr);
  EXPECT_EQ(CppType<bool>::value, neg_bool_expr.cpp_type());
  EXPECT_EQ(std::get<bool>(neg_bool_expr.GetValue().value()), false);
  // float
  val_expr =
      new ProtoConstraintValueExpression(ProtoValue(static_cast<float>(-1.0)));
  ProtoConstraintNegateExpression neg_float_expr(val_expr);
  EXPECT_EQ(CppType<float>::value, neg_float_expr.cpp_type());
  EXPECT_EQ(std::get<float>(neg_float_expr.GetValue().value()), 1.0);
  // string
  val_expr = new ProtoConstraintValueExpression(
      ProtoValue(static_cast<std::string>("hello world")));
  ProtoConstraintNegateExpression neg_string_expr(val_expr);
  EXPECT_EQ(CppType<std::string>::value, neg_string_expr.cpp_type());
  auto res = neg_string_expr.GetValue();
  EXPECT_FALSE(res.status().ok());
}

TEST_F(ProtoConstraintExpressionTest, CloneValueExpr) {
  ProtoConstraintValueExpression expr(ProtoValue(static_cast<int32_t>(-1)));
  auto *clone = expr.Clone();
  EXPECT_NE(clone, nullptr);
  EXPECT_EQ(clone->cpp_type(), expr.cpp_type());
  EXPECT_EQ(clone->GetValueAs<int32_t>(), -1);
  delete clone;
}

TEST_F(ProtoConstraintExpressionTest, CloneEnumExpr) {
  // Read in the proto file and initialize the importer pool.
  MultiFileErrorCollector error_collector;
  google::protobuf::compiler::DiskSourceTree source_tree;
  source_tree.MapPath("", "");
  google::protobuf::compiler::Importer importer(&source_tree, &error_collector);
  auto const *isa_descriptor = importer.Import(kIsaProto);
  CHECK_NE(isa_descriptor, nullptr);
  auto const *pool = importer.pool();
  CHECK_NE(pool, nullptr);
  // Get the enum value descriptor.
  auto const *enum_value_desc =
      pool->FindEnumValueByName("mpact_sim.decoder.test.OPCODE_ADD");
  CHECK_NE(enum_value_desc, nullptr);
  int enum_value = enum_value_desc->number();
  // Create a constraint enum expression using the enum value descriptor.
  ProtoConstraintEnumExpression enum_expr(enum_value_desc);
  auto *clone = enum_expr.Clone();
  EXPECT_NE(clone, nullptr);
  EXPECT_EQ(clone->cpp_type(), enum_expr.cpp_type());
  EXPECT_EQ(clone->GetValueAs<int32_t>(), enum_value);
  delete clone;
}

TEST_F(ProtoConstraintExpressionTest, CloneNegateExpr) {
  // int32_t
  auto *val_expr =
      new ProtoConstraintValueExpression(ProtoValue(static_cast<int32_t>(-1)));
  ProtoConstraintNegateExpression neg_expr(val_expr);
  auto *clone = neg_expr.Clone();
  EXPECT_NE(clone, nullptr);
  EXPECT_EQ(clone->cpp_type(), neg_expr.cpp_type());
  EXPECT_EQ(clone->GetValueAs<int32_t>(), neg_expr.GetValueAs<int32_t>());
  delete clone;
}

}  // namespace
