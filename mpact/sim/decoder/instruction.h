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

#ifndef MPACT_SIM_DECODER_INSTRUCTION_H_
#define MPACT_SIM_DECODER_INSTRUCTION_H_

#include <string>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "mpact/sim/decoder/opcode.h"

namespace mpact {
namespace sim {
namespace machine_description {
namespace instruction_set {

struct ResourceReference;
class Slot;
class TemplateExpression;

// This class models an instruction in a slot by combining an opcode (which is
// a globally unique in an instruction set definition), with instance specific
// attributes for disassembly, semantic functions, resource specification, and
// instruction attributes. This allows an instruction to be inherited from one
// slot to another, override individual attributes, while keeping the opcode
// globally unique.
class Instruction {
 public:
  Instruction(Opcode *opcode, Slot *slot);
  Instruction(Opcode *opcode, Instruction *child, Slot *slot);
  virtual ~Instruction();

  // Append a child instruction.
  void AppendChild(Instruction *child);
  // Create an instruction object that inherits from another. This may require
  // evaluatioin of expressions within the instruction/opcode that rely on
  // template instantiation arguments.
  absl::StatusOr<Instruction *> CreateDerivedInstruction(
      TemplateInstantiationArgs *args) const;
  // Resources used and acquired/released.
  void AppendResourceUse(const ResourceReference *resource_ref);
  void AppendResourceAcquire(const ResourceReference *resource_ref);
  // Instruction attributes. These methods add the instruction attribute to the
  // opcode if it does not already exist. If it exists, then the new attribute
  // definition overrides the previous (replaces the expression).
  void AddInstructionAttribute(absl::string_view attr_name,
                               TemplateExpression *expression);
  void AddInstructionAttribute(absl::string_view attr_name);
  // Append disassembly format string and info.
  void AppendDisasmFormat(DisasmFormat *disasm_format);

  // Searches opcodes in the instruction (and any child instructions) for the
  // destination op with name op_name.
  DestinationOperand *GetDestOp(absl::string_view op_name) const;

  // Methods to clear parts of the instructions that may be overridden when
  // inherited.
  void ClearDisasmFormat();
  void ClearSemfuncCodeString();
  void ClearResourceSpecs();
  void ClearAttributeSpecs();

  // Getters and setters.
  Opcode *opcode() const { return opcode_; }
  Instruction *child() const { return child_; }
  void set_semfunc_code_string(std::string code_string) {
    semfunc_code_string_ = code_string;
  }
  const std::string &semfunc_code_string() const {
    return semfunc_code_string_;
  }

  const std::vector<const ResourceReference *> &resource_use_vec() const {
    return resource_use_vec_;
  }
  const std::vector<const ResourceReference *> &resource_acquire_vec() const {
    return resource_acquire_vec_;
  }

  const std::vector<DisasmFormat *> &disasm_format_vec() const {
    return disasm_format_vec_;
  }

  const absl::flat_hash_map<std::string, TemplateExpression *> &attribute_map()
      const {
    return attribute_map_;
  }

 private:
  absl::StatusOr<ResourceReference *> CreateDerivedResourceRef(
      const ResourceReference *ref, TemplateInstantiationArgs *args) const;
  Opcode *opcode_;
  Instruction *child_;
  Slot *slot_;
  std::vector<const ResourceReference *> resource_use_vec_;
  std::vector<const ResourceReference *> resource_acquire_vec_;
  std::string semfunc_code_string_;
  std::vector<DisasmFormat *> disasm_format_vec_;
  absl::flat_hash_map<std::string, TemplateExpression *> attribute_map_;
};

}  // namespace instruction_set
}  // namespace machine_description
}  // namespace sim
}  // namespace mpact

#endif  // MPACT_SIM_DECODER_INSTRUCTION_H_
