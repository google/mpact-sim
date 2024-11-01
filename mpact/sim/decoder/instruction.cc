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

#include "mpact/sim/decoder/instruction.h"

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "mpact/sim/decoder/instruction_set.h"
#include "mpact/sim/decoder/opcode.h"
#include "mpact/sim/decoder/slot.h"
#include "mpact/sim/decoder/template_expression.h"

namespace mpact {
namespace sim {
namespace machine_description {
namespace instruction_set {

Instruction::Instruction(Opcode *opcode, Slot *slot)
    : Instruction(opcode, nullptr, slot) {}
Instruction::Instruction(Opcode *opcode, Instruction *child, Slot *slot)
    : opcode_(opcode), child_(child), slot_(slot) {}

Instruction::~Instruction() {
  delete child_;
  delete opcode_;
  for (auto *ref : resource_use_vec_) {
    delete ref;
  }
  resource_use_vec_.clear();
  for (auto *ref : resource_acquire_vec_) {
    delete ref;
  }
  resource_acquire_vec_.clear();
  for (auto *disasm_format : disasm_format_vec_) {
    delete disasm_format;
  }
  disasm_format_vec_.clear();
  for (auto &[ignored, expr] : attribute_map_) {
    delete expr;
  }
  attribute_map_.clear();
}

// Append is implemented recursively. Few instructions have child instances,
// and when they do it's likely to be a very small number. Not concern for
// efficiency here.
void Instruction::AppendChild(Instruction *child) {
  if (child_ == nullptr) {
    child_ = child;
    return;
  }
  child_->AppendChild(child);
}

void Instruction::AppendResourceUse(const ResourceReference *resource_ref) {
  resource_use_vec_.push_back(resource_ref);
}

void Instruction::AppendResourceAcquire(const ResourceReference *resource_ref) {
  resource_acquire_vec_.push_back(resource_ref);
}

void Instruction::AddInstructionAttribute(absl::string_view attr_name,
                                          TemplateExpression *expression) {
  // See if the attribute is already defined. If not, create a new attribute
  // in the map, otherwise update the expression.
  auto iter = attribute_map_.find(attr_name);
  if (iter == attribute_map_.end()) {
    attribute_map_.emplace(attr_name, expression);
    return;
  }
  delete iter->second;
  iter->second = expression;
}

// Add an attribute with the default value 1.
void Instruction::AddInstructionAttribute(absl::string_view attr_name) {
  AddInstructionAttribute(attr_name, new TemplateConstant(1));
}

void Instruction::AppendDisasmFormat(DisasmFormat *disasm_format) {
  disasm_format_vec_.push_back(disasm_format);
}

// Creating a derived instruction involves copying attributes and re-evaluating
// any expressions that depend on any slot template instantiation values.
absl::StatusOr<Instruction *> Instruction::CreateDerivedInstruction(
    TemplateInstantiationArgs *args) const {
  // First try to create a derived opcode object. Fail if it fails.
  auto op_result =
      slot_->instruction_set()->opcode_factory()->CreateDerivedOpcode(opcode(),
                                                                      args);
  if (!op_result.ok()) return op_result.status();

  // Create a new instruction object with the derived opcode object.
  auto *new_inst = new Instruction(op_result.value(), slot_);

  // Disassembly format.
  for (auto const *disasm_fmt : disasm_format_vec()) {
    new_inst->AppendDisasmFormat(new DisasmFormat(*disasm_fmt));
  }
  // Semantic function string.
  new_inst->set_semfunc_code_string(semfunc_code_string());

  // Resource uses.
  for (auto const &resource_use : resource_use_vec()) {
    auto ref_result = CreateDerivedResourceRef(resource_use, args);
    if (!ref_result.status().ok()) {
      delete new_inst;
      return ref_result.status();
    }
    new_inst->AppendResourceUse(ref_result.value());
  }

  // Resource reservations.
  for (auto const *resource_def : resource_acquire_vec()) {
    auto ref_result = CreateDerivedResourceRef(resource_def, args);
    if (!ref_result.status().ok()) {
      delete new_inst;
      return ref_result.status();
    }
    new_inst->AppendResourceAcquire(ref_result.value());
  }

  // Instruction attributes.
  for (auto const &[attr_name, expr_ptr] : attribute_map_) {
    auto result = expr_ptr->Evaluate(args);
    if (result.ok()) {
      new_inst->AddInstructionAttribute(attr_name, result.value());
    } else {
      delete new_inst;
      return absl::InternalError(absl::StrCat(
          "Failed to create derived instruction for '", opcode()->name(), "'"));
    }
  }

  // Recursively hande child instructions.
  if (child() == nullptr) return new_inst;

  auto result = child()->CreateDerivedInstruction(args);
  if (result.ok()) {
    new_inst->AppendChild(result.value());
    return new_inst;
  }
  delete new_inst;
  return result.status();
}

absl::StatusOr<ResourceReference *> Instruction::CreateDerivedResourceRef(
    const ResourceReference *ref, TemplateInstantiationArgs *args) const {
  TemplateExpression *begin_expr = nullptr;
  TemplateExpression *end_expr = nullptr;
  // Evaluate the begin expression in the context of any template instantiation
  // arguments.
  if (ref->begin_expression != nullptr) {
    auto result = ref->begin_expression->Evaluate(args);
    if (!result.ok()) {
      return absl::InternalError(absl::StrCat(
          "Failed to create derived instruction for '", opcode()->name(),
          "': error evaluating begin expression"));
    }
    begin_expr = result.value();
  }
  // Evaluate the end expression in the context of any template instantiation
  // arguments.
  if (ref->end_expression != nullptr) {
    auto result = ref->end_expression->Evaluate(args);
    if (!result.ok()) {
      return absl::InternalError(absl::StrCat(
          "Failed to create derived instruction for '", opcode()->name(),
          "': error evaluating begin expression"));
    }
    end_expr = result.value();
  }
  auto *new_ref = new ResourceReference(ref->resource, ref->is_array,
                                        ref->dest_op, begin_expr, end_expr);
  return new_ref;
}

// The destination op is stored in the opcode object, however, the child pointer
// is in the instruction object, so traverse the instructions along the child
// chain to find the destination operand.
DestinationOperand *Instruction::GetDestOp(absl::string_view op_name) const {
  auto dest_op = opcode()->GetDestOp(op_name);
  if (dest_op != nullptr) return dest_op;

  if (child() != nullptr) {
    return child()->GetDestOp(op_name);
  }
  return nullptr;
}

// The following methods are used to clear the different instruction attributes.
// This is called prior to overriding the attribute value to clean up any
// allocated memory.
void Instruction::ClearDisasmFormat() {
  for (auto *disasm_format : disasm_format_vec_) {
    delete disasm_format;
  }
  disasm_format_vec_.clear();
}

void Instruction::ClearSemfuncCodeString() { semfunc_code_string_.clear(); }

void Instruction::ClearResourceSpecs() {
  for (auto *ref : resource_use_vec_) {
    delete ref;
  }
  resource_use_vec_.clear();
  for (auto *ref : resource_acquire_vec_) {
    delete ref;
  }
  resource_acquire_vec_.clear();
}

void Instruction::ClearAttributeSpecs() {
  for (auto &[ignored, expr] : attribute_map_) {
    delete expr;
  }
  attribute_map_.clear();
}

}  // namespace instruction_set
}  // namespace machine_description
}  // namespace sim
}  // namespace mpact
