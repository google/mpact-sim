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

#ifndef MPACT_SIM_DECODER_TEMPLATE_EXPRESSION_H_
#define MPACT_SIM_DECODER_TEMPLATE_EXPRESSION_H_

#include <cstddef>
#include <functional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "absl/status/statusor.h"

// This file contains classes that represent and can evaluate expressions
// consisting of literals, template parameters, and operations: unary minus,
// add, subtract, multiply and divide. The value of the expression is
// abstracted to an absl::variant type. Currently, the only type that is
// supported is int, but putting the variant in now makes it easier to add
// support for others later.

namespace mpact {
namespace sim {
namespace machine_description {
namespace instruction_set {

// Variant with possible value types of template parameters.
using TemplateValue = ::std::variant<int>;

// A template formal parameter has a name and a position in the template
// argument list.
class TemplateFormal {
 public:
  TemplateFormal(std::string name, int position)
      : name_(std::move(name)), position_(position) {}

  const std::string& name() const { return name_; }
  size_t position() const { return position_; }

 private:
  std::string name_;
  size_t position_;
};

// Template instantiation arguments are represented as a vector of template
// argument expressions that match to the positions of the template formal
// parameters.

class TemplateExpression;
using TemplateInstantiationArgs = std::vector<TemplateExpression*>;

// Virtual base class for template expressions.
class TemplateExpression {
 public:
  virtual ~TemplateExpression() = default;
  // Returns the value of the expression provided it can be computed without
  // having to resolve any template parameters (i.e., the expression tree
  // does not contain any template parameter nodes.
  virtual absl::StatusOr<TemplateValue> GetValue() const = 0;
  // Returns a new expression tree where any template parameters have been
  // resolved against the argument expressions that are passed in. Constant
  // subexpressions are collapsed into constant nodes wherever possible. Note
  // that the argument expressions may themselves contain template parameters
  // for the "surrounding" template, so it may not resolve to a constant value.
  virtual absl::StatusOr<TemplateExpression*> Evaluate(
      TemplateInstantiationArgs*) = 0;
  // Returns true if the expression can be evaluated to a constant.
  virtual bool IsConstant() const = 0;
  virtual TemplateExpression* DeepCopy() const = 0;
};

// Constant value expression node.
class TemplateConstant : public TemplateExpression {
 public:
  explicit TemplateConstant(int val) : value_(val) {}
  explicit TemplateConstant(TemplateValue val) : value_(val) {}
  absl::StatusOr<TemplateValue> GetValue() const override { return value_; }
  absl::StatusOr<TemplateExpression*> Evaluate(
      TemplateInstantiationArgs*) override;
  bool IsConstant() const override { return true; }
  TemplateExpression* DeepCopy() const override;

 private:
  TemplateValue value_;
};

// Using the "curiously recurring template pattern" to define a templated
// base class for binary expression node classes. Each binary operator class
// will inherit from this class, while passing itself as the template argument.
template <typename T>
class BinaryTemplateExpression : public TemplateExpression {
 public:
  TemplateExpression* DeepCopy() const final {
    return new T(lhs_->DeepCopy(), rhs_->DeepCopy());
  }
  absl::StatusOr<TemplateExpression*> Evaluate(
      TemplateInstantiationArgs* args) final {
    auto lhs = lhs_->Evaluate(args);
    if (!lhs.ok()) return lhs.status();

    auto rhs = rhs_->Evaluate(args);
    if (!rhs.ok()) {
      delete lhs.value();
      return rhs.status();
    }
    // Return a constant node if the right and left subexpressions are constant.
    if (lhs.value()->IsConstant() && rhs.value()->IsConstant()) {
      auto result =
          T::Operator(lhs.value()->GetValue(), rhs.value()->GetValue());
      delete lhs.value();
      delete rhs.value();
      if (result.ok()) {
        return new TemplateConstant(result.value());
      } else {
        return result.status();
      }
    } else {
      return new T(lhs.value(), rhs.value());
    }
  }

  absl::StatusOr<TemplateValue> GetValue() const final {
    return T::Operator(lhs_->GetValue(), rhs_->GetValue());
  }

  bool IsConstant() const final {
    return lhs_->IsConstant() && rhs_->IsConstant();
  }

 protected:
  BinaryTemplateExpression() = delete;
  BinaryTemplateExpression(TemplateExpression* lhs, TemplateExpression* rhs)
      : lhs_(lhs), rhs_(rhs) {}
  ~BinaryTemplateExpression() override {
    delete lhs_;
    delete rhs_;
    lhs_ = nullptr;
    rhs_ = nullptr;
  }

 private:
  TemplateExpression* lhs_;
  TemplateExpression* rhs_;
};

// Slot constant.
class SlotConstant : public TemplateExpression {
 public:
  explicit SlotConstant(TemplateExpression* expr) : expr_(expr) {}
  ~SlotConstant() override;
  absl::StatusOr<TemplateValue> GetValue() const override;
  absl::StatusOr<TemplateExpression*> Evaluate(
      TemplateInstantiationArgs* args) override;
  bool IsConstant() const override { return expr_->IsConstant(); }
  TemplateExpression* DeepCopy() const override;

 private:
  TemplateExpression* expr_ = nullptr;
};

// Template formal parameter reference expression node.
class TemplateParam : public TemplateExpression {
 public:
  explicit TemplateParam(TemplateFormal* param) : param_(param) {}
  absl::StatusOr<TemplateValue> GetValue() const override;
  absl::StatusOr<TemplateExpression*> Evaluate(
      TemplateInstantiationArgs* args) override;
  bool IsConstant() const override { return false; }
  TemplateExpression* DeepCopy() const override;

 private:
  TemplateFormal* param_;
};

// Negate expression node.
class TemplateNegate : public TemplateExpression {
 public:
  explicit TemplateNegate(TemplateExpression* expr) : expr_(expr) {}
  ~TemplateNegate() override;
  absl::StatusOr<TemplateValue> GetValue() const override;
  absl::StatusOr<TemplateExpression*> Evaluate(
      TemplateInstantiationArgs* args) override;
  bool IsConstant() const override { return expr_->IsConstant(); }
  TemplateExpression* DeepCopy() const override;

 private:
  TemplateExpression* expr_;
};

// Multiply expression node.
class TemplateMultiply : public BinaryTemplateExpression<TemplateMultiply> {
 public:
  TemplateMultiply(TemplateExpression* lhs, TemplateExpression* rhs)
      : BinaryTemplateExpression(lhs, rhs) {}
  static absl::StatusOr<TemplateValue> Operator(
      const absl::StatusOr<TemplateValue>& lhs,
      const absl::StatusOr<TemplateValue>& rhs);
};

// Divide expression node.
class TemplateDivide : public BinaryTemplateExpression<TemplateDivide> {
 public:
  TemplateDivide(TemplateExpression* lhs, TemplateExpression* rhs)
      : BinaryTemplateExpression(lhs, rhs) {}
  static absl::StatusOr<TemplateValue> Operator(
      const absl::StatusOr<TemplateValue>& lhs,
      const absl::StatusOr<TemplateValue>& rhs);
};

// Add expression node.
class TemplateAdd : public BinaryTemplateExpression<TemplateAdd> {
 public:
  TemplateAdd(TemplateExpression* lhs, TemplateExpression* rhs)
      : BinaryTemplateExpression(lhs, rhs) {}
  static absl::StatusOr<TemplateValue> Operator(
      const absl::StatusOr<TemplateValue>& lhs,
      const absl::StatusOr<TemplateValue>& rhs);
};

// Subtract expression node.
class TemplateSubtract : public BinaryTemplateExpression<TemplateSubtract> {
 public:
  TemplateSubtract(TemplateExpression* lhs, TemplateExpression* rhs)
      : BinaryTemplateExpression(lhs, rhs) {}
  static absl::StatusOr<TemplateValue> Operator(
      const absl::StatusOr<TemplateValue>& lhs,
      const absl::StatusOr<TemplateValue>& rhs);
};

// Function expression node.
class TemplateFunction : public TemplateExpression {
 public:
  using Evaluator =
      std::function<absl::StatusOr<TemplateValue>(TemplateInstantiationArgs*)>;
  TemplateFunction(Evaluator evaluator, TemplateInstantiationArgs* args)
      : evaluator_(std::move(evaluator)), args_(args) {}
  ~TemplateFunction() override;
  absl::StatusOr<TemplateValue> GetValue() const override;
  absl::StatusOr<TemplateExpression*> Evaluate(
      TemplateInstantiationArgs* args) override;
  bool IsConstant() const override;
  TemplateExpression* DeepCopy() const override;

 private:
  Evaluator evaluator_;
  TemplateInstantiationArgs* args_;
};

}  // namespace instruction_set
}  // namespace machine_description
}  // namespace sim
}  // namespace mpact

#endif  // MPACT_SIM_DECODER_TEMPLATE_EXPRESSION_H_
