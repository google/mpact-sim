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

#include "mpact/sim/decoder/opcode.h"

#include <functional>
#include <string>
#include <utility>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "mpact/sim/decoder/format_name.h"
#include "mpact/sim/decoder/template_expression.h"

namespace mpact {
namespace sim {
namespace machine_description {
namespace instruction_set {

// There is not much to the Opcode class. Note that the AppendSourceOpName
// method use emplace since a string_view can be passed to a std::string
// constructor but cannot be passed directly to push_back().
Opcode::Opcode(absl::string_view name, int value)
    : name_(name), pascal_name_(ToPascalCase(name)), value_(value) {}

Opcode::~Opcode() {
  for (auto *dest_op : dest_op_vec_) {
    delete dest_op;
  }
  dest_op_vec_.clear();
  dest_op_map_.clear();
}

void Opcode::AppendSourceOp(absl::string_view op_name, bool is_array,
                            bool is_reloc) {
  source_op_vec_.emplace_back(std::string(op_name), is_array, is_reloc);
}

void Opcode::AppendDestOp(absl::string_view op_name, bool is_array,
                          bool is_reloc) {
  auto *op = new DestinationOperand(std::string(op_name), is_array, is_reloc);
  dest_op_vec_.push_back(op);
  dest_op_map_.insert(std::make_pair(std::string(op_name), op));
}

void Opcode::AppendDestOp(absl::string_view op_name, bool is_array,
                          bool is_reloc, TemplateExpression *expression) {
  auto *op = new DestinationOperand(std::string(op_name), is_array, is_reloc,
                                    expression);
  dest_op_vec_.push_back(op);
  dest_op_map_.insert(std::make_pair(std::string(op_name), op));
}

DestinationOperand *Opcode::GetDestOp(absl::string_view op_name) {
  auto iter = dest_op_map_.find(op_name);
  if (iter != dest_op_map_.end()) return iter->second;

  return nullptr;
}

bool Opcode::ValidateDestLatencies(
    const std::function<bool(int)> &validator) const {
  for (auto const *dest_op : dest_op_vec_) {
    if (dest_op->expression() != nullptr) {
      auto result = dest_op->GetLatency();
      if (!result.ok()) return false;
      if (!validator(result.value())) return false;
    }
  }
  return true;
}

Opcode *OpcodeFactory::CreateDefaultOpcode() { return new Opcode("", -1); }

absl::StatusOr<Opcode *> OpcodeFactory::CreateOpcode(absl::string_view name) {
  if (opcode_names_.contains(name)) {
    return absl::InternalError(
        absl::StrCat("Opcode '", name, "' already declared"));
  }
  // Using emplace since name is a string_view which insert doesn't accept.
  opcode_names_.emplace(name);
  auto opcode = new Opcode(name, opcode_value_++);
  opcode_vec_.push_back(opcode);
  return opcode;
}

Opcode *OpcodeFactory::CreateChildOpcode(Opcode *opcode) const {
  if (opcode == nullptr) return nullptr;
  auto *child = new Opcode(opcode->name(), -1);
  return child;
}

absl::StatusOr<Opcode *> OpcodeFactory::CreateDerivedOpcode(
    const Opcode *opcode, TemplateInstantiationArgs *args) {
  // Allocate a new opcode. Copy the basic information.
  auto new_opcode = new Opcode(opcode->name(), opcode->value());
  new_opcode->set_instruction_size(opcode->instruction_size());
  new_opcode->predicate_op_name_ = opcode->predicate_op_name();
  new_opcode->op_locator_map_ = opcode->op_locator_map();
  for (auto const &src_op : opcode->source_op_vec()) {
    new_opcode->AppendSourceOp(src_op.name, src_op.is_array, src_op.is_reloc);
  }

  // Copy destination operands, but evaluate any latencies using the template
  // instantiation arguments, in case those expressions use them.
  for (auto const *dest_op : opcode->dest_op_vec()) {
    if (dest_op->expression() == nullptr) {
      new_opcode->AppendDestOp(dest_op->name(), dest_op->is_array(),
                               dest_op->is_reloc());
    } else {
      // For each destination operand that has an expression, evaluate it in the
      // context of the passed in TemplateInstantiationArgs. This creates a copy
      // of the expression tree where any constant subexpressions are
      // recursively folded into constant nodes.
      auto result = dest_op->expression()->Evaluate(args);
      if (result.ok()) {
        new_opcode->AppendDestOp(dest_op->name(), dest_op->is_array(),
                                 dest_op->is_reloc(), result.value());
      } else {
        delete new_opcode;
        return absl::InternalError(absl::StrCat(
            "Failed to create derived opcode for '", opcode->name(), "'"));
      }
    }
  }

  return new_opcode;
}

}  // namespace instruction_set
}  // namespace machine_description
}  // namespace sim
}  // namespace mpact
