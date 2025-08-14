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

#include <functional>
#include <variant>

#include "absl/status/status.h"
#include "absl/status/statusor.h"

namespace mpact {
namespace sim {
namespace machine_description {
namespace instruction_set {

// Static operator overloads are used to implement operations on expression
// nodes. If there is an error in the expression evaluation the error is
// propagated to the top.
static absl::StatusOr<TemplateValue> operator-(
    const absl::StatusOr<TemplateValue>& lhs) {
  if (lhs.ok()) {
    auto variant_value = lhs.value();
    int* value_ptr = std::get_if<int>(&variant_value);
    if (value_ptr != nullptr) {
      return -*value_ptr;
    } else {
      return absl::InternalError("int type expected");
    }
  }
  return lhs.status();
}

// Operator helper function. The typename template argument must be a type
// that is member of the TemplateValue variant.
template <typename T>
static absl::StatusOr<TemplateValue> OperatorHelper(
    const absl::StatusOr<TemplateValue>& lhs,
    const absl::StatusOr<TemplateValue>& rhs,
    std::function<absl::StatusOr<TemplateValue>(T, T)> func) {
  if (!lhs.ok()) return lhs.status();
  if (!rhs.ok()) return rhs.status();
  auto lhs_variant = lhs.value();
  auto rhs_variant = rhs.value();

  int* lhs_value = std::get_if<T>(&lhs_variant);
  int* rhs_value = std::get_if<T>(&rhs_variant);

  if (lhs_value == nullptr) return absl::InternalError("int type expected");
  if (rhs_value == nullptr) return absl::InternalError("int type expected");

  return func(*lhs_value, *rhs_value);
}

static absl::StatusOr<TemplateValue> operator+(
    const absl::StatusOr<TemplateValue>& lhs,
    const absl::StatusOr<TemplateValue>& rhs) {
  return OperatorHelper<int>(
      lhs, rhs,
      [](int lhs_value, int rhs_value) -> absl::StatusOr<TemplateValue> {
        return lhs_value + rhs_value;
      });
}

static absl::StatusOr<TemplateValue> operator-(
    const absl::StatusOr<TemplateValue>& lhs,
    const absl::StatusOr<TemplateValue>& rhs) {
  return OperatorHelper<int>(
      lhs, rhs,
      [](int lhs_value, int rhs_value) -> absl::StatusOr<TemplateValue> {
        return lhs_value - rhs_value;
      });
}

static absl::StatusOr<TemplateValue> operator*(
    const absl::StatusOr<TemplateValue>& lhs,
    const absl::StatusOr<TemplateValue>& rhs) {
  return OperatorHelper<int>(
      lhs, rhs,
      [](int lhs_value, int rhs_value) -> absl::StatusOr<TemplateValue> {
        return lhs_value * rhs_value;
      });
}

static absl::StatusOr<TemplateValue> operator/(
    const absl::StatusOr<TemplateValue>& lhs,
    const absl::StatusOr<TemplateValue>& rhs) {
  return OperatorHelper<int>(
      lhs, rhs,
      [](int lhs_value, int rhs_value) -> absl::StatusOr<TemplateValue> {
        if (rhs_value == 0) return absl::InternalError("Divide by zero");
        return lhs_value / rhs_value;
      });
}

// Evaluate of constant only returns a copy.
absl::StatusOr<TemplateExpression*> TemplateConstant::Evaluate(
    TemplateInstantiationArgs*) {
  return new TemplateConstant(value_);
}

TemplateExpression* TemplateConstant::DeepCopy() const {
  return new TemplateConstant(value_);
}

SlotConstant::~SlotConstant() { delete expr_; }

absl::StatusOr<TemplateValue> SlotConstant::GetValue() const {
  return expr_->GetValue();
}

absl::StatusOr<TemplateExpression*> SlotConstant::Evaluate(
    TemplateInstantiationArgs* args) {
  return expr_->Evaluate(args);
}

TemplateExpression* SlotConstant::DeepCopy() const { return expr_->DeepCopy(); }

// A template parameter has no value in the expression unless replaced by
// the actual argument expression tree.
absl::StatusOr<TemplateValue> TemplateParam::GetValue() const {
  return absl::InternalError("Cannot return value of template parameter");
}

// Returns an evaluated copy of the corresponding argument expression tree.
absl::StatusOr<TemplateExpression*> TemplateParam::Evaluate(
    TemplateInstantiationArgs* args) {
  // No template arguments available, so just return the template parameter.
  if (args == nullptr) {
    return new TemplateParam(param_);
  }
  if (param_->position() >= args->size()) {
    return absl::InternalError("Template parameter position out of range");
  }
  TemplateExpression* expr = (*args)[param_->position()];
  if (expr->IsConstant()) {
    auto result = expr->GetValue();
    if (result.ok()) return new TemplateConstant(result.value());

    return result.status();
  }
  // Evaluate without template arguments, as the argument expression is
  // defined in a different template instantiation context.
  return (*args)[param_->position()]->Evaluate(nullptr);
}

TemplateExpression* TemplateParam::DeepCopy() const {
  return new TemplateParam(param_);
}

TemplateNegate::~TemplateNegate() {
  delete expr_;
  expr_ = nullptr;
}

absl::StatusOr<TemplateValue> TemplateNegate::GetValue() const {
  auto res = expr_->GetValue();
  return -expr_->GetValue();
}

absl::StatusOr<TemplateExpression*> TemplateNegate::Evaluate(
    TemplateInstantiationArgs* args) {
  auto expr = expr_->Evaluate(args);
  if (!expr.ok()) return expr.status();
  // If expression is constant then can return a constant node.
  if (expr.value()->IsConstant()) {
    auto result = -expr.value()->GetValue();
    if (result.ok()) {
      delete expr.value();
      return new TemplateConstant(result.value());
    } else {
      return expr.status();
    }
  } else {
    return new TemplateNegate(expr.value());
  }
}

TemplateExpression* TemplateNegate::DeepCopy() const {
  return new TemplateNegate(expr_->DeepCopy());
}

absl::StatusOr<TemplateValue> TemplateMultiply::Operator(
    const absl::StatusOr<TemplateValue>& lhs,
    const absl::StatusOr<TemplateValue>& rhs) {
  return lhs * rhs;
}

absl::StatusOr<TemplateValue> TemplateDivide::Operator(
    const absl::StatusOr<TemplateValue>& lhs,
    const absl::StatusOr<TemplateValue>& rhs) {
  return lhs / rhs;
}

absl::StatusOr<TemplateValue> TemplateAdd::Operator(
    const absl::StatusOr<TemplateValue>& lhs,
    const absl::StatusOr<TemplateValue>& rhs) {
  return lhs + rhs;
}

absl::StatusOr<TemplateValue> TemplateSubtract::Operator(
    const absl::StatusOr<TemplateValue>& lhs,
    const absl::StatusOr<TemplateValue>& rhs) {
  return lhs - rhs;
}

TemplateFunction::~TemplateFunction() {
  for (auto* arg : *args_) {
    delete arg;
  }
  delete args_;
}

absl::StatusOr<TemplateValue> TemplateFunction::GetValue() const {
  if (IsConstant()) {
    return evaluator_(args_);
  }
  return absl::InternalError("Cannot evaluate function with unbound arguments");
}

absl::StatusOr<TemplateExpression*> TemplateFunction::Evaluate(
    TemplateInstantiationArgs* args) {
  auto new_arguments = new TemplateInstantiationArgs;
  for (auto* arg : *args_) {
    auto result = arg->Evaluate(args);
    if (!result.ok()) {
      for (auto* tmp : *new_arguments) {
        delete tmp;
      }
      delete new_arguments;
      return result.status();
    }
    new_arguments->push_back(result.value());
  }
  return new TemplateFunction(evaluator_, new_arguments);
}

bool TemplateFunction::IsConstant() const {
  for (auto* arg : *args_) {
    if (!arg->IsConstant()) return false;
  }
  return true;
}

TemplateExpression* TemplateFunction::DeepCopy() const {
  auto* args = new TemplateInstantiationArgs;
  for (auto* arg : *args_) {
    args->push_back(arg->DeepCopy());
  }
  return new TemplateFunction(evaluator_, args);
}

}  // namespace instruction_set
}  // namespace machine_description
}  // namespace sim
}  // namespace mpact
