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

#include "mpact/sim/decoder/template_expression.h"

#include <variant>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "googletest/include/gtest/gtest.h"

namespace mpact {
namespace sim {
namespace machine_description {
namespace instruction_set {
namespace {

constexpr int kIntConst5 = 5;
constexpr int kIntConst2 = 2;
constexpr int kIntConst3 = 3;
constexpr int kIntConst0 = 0;

static absl::StatusOr<TemplateValue> Add3TemplateFunc(
    TemplateInstantiationArgs* args) {
  if (args->size() != 1) {
    return absl::InternalError(absl::StrCat(
        "Wrong number of arguments, expected 1, was given ", args->size()));
  }
  auto result = (*args)[0]->GetValue();
  if (!result.ok()) return result.status();

  auto* value_ptr = std::get_if<int>(&result.value());
  if (value_ptr == nullptr) {
    return absl::InternalError("Type mismatch - int expected");
  }
  int return_value = *value_ptr + kIntConst3;
  return TemplateValue(return_value);
}

// Simple tests of each expression type.
TEST(TemplateExpressionTest, Constant) {
  auto five = new TemplateConstant(kIntConst5);
  auto result = five->GetValue();
  ASSERT_TRUE(result.status().ok());
  ASSERT_TRUE(five->IsConstant());
  auto template_value = result.value();
  int* value_ptr = std::get_if<int>(&template_value);
  EXPECT_EQ(*value_ptr, kIntConst5);
  delete five;
}

TEST(TemplateExpressionTest, Negate) {
  auto* five = new TemplateConstant(kIntConst5);
  auto* negate_expr = new TemplateNegate(five);
  auto result = negate_expr->GetValue();
  ASSERT_TRUE(result.status().ok());
  auto template_value = result.value();
  int* value_ptr = std::get_if<int>(&template_value);
  EXPECT_EQ(*value_ptr, -kIntConst5);
  // Call Evaluate and check that the value is the same.
  auto eval_res = negate_expr->Evaluate(nullptr);
  EXPECT_TRUE(eval_res.status().ok());
  auto* eval_expr = eval_res.value();
  result = eval_expr->GetValue();
  ASSERT_TRUE(result.status().ok());
  template_value = result.value();
  value_ptr = std::get_if<int>(&template_value);
  EXPECT_EQ(*value_ptr, -kIntConst5);
  delete negate_expr;
  delete eval_expr;
}

TEST(TemplateExpressionTest, Add) {
  auto two = new TemplateConstant(kIntConst2);
  auto three = new TemplateConstant(kIntConst3);
  auto add_expr = new TemplateAdd(two, three);
  EXPECT_TRUE(add_expr->IsConstant());
  auto result = add_expr->GetValue();
  ASSERT_TRUE(result.status().ok());
  auto template_value = result.value();
  int* value_ptr = std::get_if<int>(&template_value);
  EXPECT_EQ(*value_ptr, kIntConst2 + kIntConst3);
  delete add_expr;
}

TEST(TemplateExpressionTest, Subtract) {
  auto two = new TemplateConstant(kIntConst2);
  auto three = new TemplateConstant(kIntConst3);
  auto subtract_expr = new TemplateSubtract(two, three);
  EXPECT_TRUE(subtract_expr->IsConstant());
  auto result = subtract_expr->GetValue();
  ASSERT_TRUE(result.status().ok());
  auto template_value = result.value();
  int* value_ptr = std::get_if<int>(&template_value);
  EXPECT_EQ(*value_ptr, kIntConst2 - kIntConst3);
  delete subtract_expr;
}

TEST(TemplateExpressionTest, Mult) {
  auto two = new TemplateConstant(kIntConst2);
  auto three = new TemplateConstant(kIntConst3);
  auto mult_expr = new TemplateMultiply(two, three);
  EXPECT_TRUE(mult_expr->IsConstant());
  auto result = mult_expr->GetValue();
  ASSERT_TRUE(result.status().ok());
  auto template_value = result.value();
  int* value_ptr = std::get_if<int>(&template_value);
  EXPECT_EQ(*value_ptr, kIntConst2 * kIntConst3);
  delete mult_expr;
}

TEST(TemplateExpressionTest, Divide) {
  auto five = new TemplateConstant(kIntConst5);
  auto two = new TemplateConstant(kIntConst2);
  auto div_expr = new TemplateDivide(five, two);
  EXPECT_TRUE(div_expr->IsConstant());
  auto result = div_expr->GetValue();
  ASSERT_TRUE(result.status().ok());
  auto template_value = result.value();
  int* value_ptr = std::get_if<int>(&template_value);
  EXPECT_EQ(*value_ptr, kIntConst5 / kIntConst2);
  delete div_expr;
}

// Verify that divide by zero returns an error.
TEST(TemplateExpressionTest, DivideByZero) {
  auto five = new TemplateConstant(kIntConst5);
  auto zero = new TemplateConstant(kIntConst0);
  auto div_expr = new TemplateDivide(five, zero);
  EXPECT_TRUE(div_expr->IsConstant());
  auto result = div_expr->GetValue();
  EXPECT_EQ(result.status().code(), absl::StatusCode::kInternal);
  EXPECT_EQ(result.status().message(), "Divide by zero");
  delete div_expr;
}

TEST(TemplateExpressionTest, TemplateFunctionAdd3) {
  auto* two = new TemplateConstant(kIntConst2);
  auto* args = new TemplateInstantiationArgs{two};
  auto* func = new TemplateFunction(Add3TemplateFunc, args);
  EXPECT_TRUE(func->IsConstant());
  auto expr_value = func->GetValue();
  ASSERT_TRUE(expr_value.status().ok());
  auto* value_ptr = std::get_if<int>(&expr_value.value());
  ASSERT_NE(value_ptr, nullptr);
  EXPECT_EQ(*value_ptr, kIntConst2 + kIntConst3);
  delete func;
}

TEST(TemplateExpressionTest, TemplateFunctionAdd3Evaluate) {
  TemplateFormal* formal_b = new TemplateFormal("b", 0);
  auto* b_plus_two = new TemplateAdd(new TemplateParam(formal_b),
                                     new TemplateConstant(kIntConst2));
  auto* args = new TemplateInstantiationArgs{b_plus_two};
  auto* func = new TemplateFunction(Add3TemplateFunc, args);
  // First attempt at evaluating function should fail, due to the template
  // formal b.
  auto result = func->GetValue();
  EXPECT_FALSE(result.status().ok());
  // Create a deep copy of func.
  auto* deep_func = func->DeepCopy();
  // Specialize based on b = 3.
  auto* three = new TemplateConstant(kIntConst3);
  TemplateInstantiationArgs instance_b{three};
  auto eval_result = func->Evaluate(&instance_b);
  ASSERT_TRUE(eval_result.status().ok());
  auto* eval_func = eval_result.value();
  // Specialize the deep copy too.
  auto deep_eval_result = deep_func->Evaluate(&instance_b);
  ASSERT_TRUE(deep_eval_result.status().ok());
  auto* deep_eval_func = deep_eval_result.value();
  // Now get the value.
  result = eval_func->GetValue();
  auto* value_ptr = std::get_if<int>(&result.value());
  ASSERT_NE(value_ptr, nullptr);
  EXPECT_EQ(*value_ptr, kIntConst2 + kIntConst3 + kIntConst3);
  // Get the value from the deep copy too.
  result = deep_eval_func->GetValue();
  value_ptr = std::get_if<int>(&result.value());
  ASSERT_NE(value_ptr, nullptr);
  EXPECT_EQ(*value_ptr, kIntConst2 + kIntConst3 + kIntConst3);
  // Clean up.
  delete formal_b;
  delete three;
  delete func;
  delete eval_func;
  delete deep_func;
  delete deep_eval_func;
}

// This tests assumes the definition of a template akin to:
// template <int b> B where the expression b + 2 is used in B.
// Compute b + 2 given instantiation argument of B<3>
TEST(TemplateExpressionTest, TemplateParameter) {
  auto* three = new TemplateConstant(kIntConst3);
  TemplateFormal* formal_b = new TemplateFormal("b", 0);
  auto* b_plus_two = new TemplateAdd(new TemplateParam(formal_b),
                                     new TemplateConstant(kIntConst2));
  TemplateInstantiationArgs instance_b{three};
  auto result = b_plus_two->Evaluate(&instance_b);
  ASSERT_TRUE(result.status().ok());
  auto expr_value = result.value()->GetValue();
  ASSERT_TRUE(expr_value.status().ok());
  int* value_ptr = std::get_if<int>(&expr_value.value());
  ASSERT_NE(value_ptr, nullptr);
  EXPECT_EQ(*value_ptr, kIntConst2 + kIntConst3);
  delete formal_b;
  delete b_plus_two;
  delete three;
  delete result.value();
}

}  // namespace
}  // namespace instruction_set
}  // namespace machine_description
}  // namespace sim
}  // namespace mpact
