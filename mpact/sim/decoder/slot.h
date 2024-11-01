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

#ifndef MPACT_SIM_DECODER_SLOT_H_
#define MPACT_SIM_DECODER_SLOT_H_

#include <limits>
#include <string>
#include <vector>

#include "absl/container/btree_map.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "mpact/sim/decoder/InstructionSetParser.h"
#include "mpact/sim/decoder/base_class.h"
#include "mpact/sim/decoder/instruction_set_contexts.h"
#include "mpact/sim/decoder/opcode.h"
#include "mpact/sim/decoder/template_expression.h"

namespace mpact {
namespace sim {
namespace machine_description {
namespace instruction_set {

class Instruction;
class InstructionSet;
class Resource;

// A structure that holds the resources specified by a named resource specifier.
struct ResourceSpec {
  std::string name;
  std::vector<ResourceReference *> use_vec;
  std::vector<ResourceReference *> acquire_vec;
};

// A slot class instance represents one or more identical instruction slots
// in an instruction word where a defined set of opcodes may be executed. A
// slot may inherit from a base slot to facilitate the factoring of common
// subsets of instruction opcodes into "base slots". These "base slots" need
// not be directly used in a bundle, in which case, they are not part of the
// instruction word encoding per se.
class Slot {
 public:
  using BaseSlot = BaseClass<Slot>;
  using ResourceDetailsCtx = ::sim::machine_description::instruction_set::
      generated::InstructionSetParser::Resource_detailsContext;

  // Constructor and destructor.
  Slot(absl::string_view name, InstructionSet *instruction_set,
       bool is_templated, SlotDeclCtx *ctx);
  ~Slot();

  // Add declared opcode to the current slot.
  absl::Status AppendInstruction(Instruction *inst);
  // Add an opcode inherited from a base slot to the current slot.
  absl::Status AppendInheritedInstruction(Instruction *inst,
                                          TemplateInstantiationArgs *args);
  // Add default instruction attribute.
  void AddInstructionAttribute(const std::string &name,
                               TemplateExpression *expr);
  bool HasInstruction(const std::string &opcode_name) const;
  // Return string for the header file declarations for this class.
  std::string GenerateClassDeclaration(absl::string_view encoding_type) const;
  // Return string for the .cc file definitions for this class.
  std::string GenerateClassDefinition(absl::string_view encoding_type) const;

  // Add a non-templated slot as a base.
  absl::Status AddBase(const Slot *base);
  // Add a templated slot as a base with the vector of expressions as the
  // template parameter values.
  absl::Status AddBase(const Slot *base, TemplateInstantiationArgs *arguments);
  // Add a declared constant (scoped to the slot).
  absl::Status AddConstant(const std::string &ident, const std::string &type,
                           TemplateExpression *expression);
  TemplateExpression *GetConstExpression(const std::string &ident) const;
  // When the current slot is templated, adds an identifier as a template
  // formal parameter.
  absl::Status AddTemplateFormal(const std::string &name);
  TemplateFormal *GetTemplateFormal(const std::string &name) const;

  // Resources
  Resource *GetOrInsertResource(const std::string &name);

  // Getters and setters.
  InstructionSet *instruction_set() const { return instruction_set_; }
  const SlotDeclCtx *ctx() const { return ctx_; }
  int default_instruction_size() const { return default_instruction_size_; }
  void set_default_instruction_size(int val) {
    default_instruction_size_ = val;
  }
  TemplateExpression *default_latency() const { return default_latency_; }
  void set_default_latency(TemplateExpression *latency_expr) {
    if (default_latency_ != nullptr) delete default_latency_;
    default_latency_ = latency_expr;
  }
  Instruction *default_instruction() const { return default_instruction_; }
  void set_default_instruction(Instruction *inst) {
    default_instruction_ = inst;
  }
  int size() const { return size_; }
  void set_size(int size) { size_ = size; }
  int min_instruction_size() const { return min_instruction_size_; }
  void set_min_instruction_size(int size) { min_instruction_size_ = size; }
  bool is_templated() const { return is_templated_; }
  bool is_marked() const { return is_marked_; }
  void set_is_marked(bool value) { is_marked_ = value; }
  void set_is_referenced(bool value) { is_referenced_ = value; }
  bool is_referenced() const { return is_referenced_; }
  const std::string &name() const { return name_; }
  const std::string &pascal_name() const { return pascal_name_; }
  const std::vector<BaseSlot> &base_slots() const { return base_slots_; }
  const absl::flat_hash_map<std::string, Instruction *> &instruction_map()
      const {
    return instruction_map_;
  }
  const std::vector<TemplateFormal *> &template_parameters() const {
    return template_parameters_;
  }
  const absl::flat_hash_map<std::string, int> &template_parameter_map() const {
    return template_parameter_map_;
  }
  absl::btree_map<std::string, ResourceDetailsCtx *> &resource_spec_map() {
    return resource_spec_map_;
  }
  absl::btree_map<std::string, IdentListCtx *> &resource_array_ref_map() {
    return resource_array_ref_map_;
  }
  const absl::btree_map<std::string, TemplateExpression *> &attribute_map() {
    return attribute_map_;
  }

 private:
  std::string GenerateAttributeSetter(const Instruction *inst) const;
  std::string GenerateDisassemblySetter(const Instruction *inst) const;
  std::string GenerateResourceSetter(const Instruction *inst,
                                     absl::string_view encoding_type) const;
  // Build up a string containing the function getter initializers that are
  // stored in two flat hash maps with the opcode as the key. These functions
  // are lambda's that call the getters for the semantic functions as well as
  // operand getters for each instruction opcode.
  std::string ListFuncGetterInitializations(
      absl::string_view encoding_type) const;
  // Transitively check if base slot is in the predecessor set of the current
  // slot or any of its inheritance predecessors. Returns AlreadyExistsError
  // if the current slot or its predecessors already inherit from base or its
  // predecessors.
  absl::Status CheckPredecessors(const Slot *base) const;
  // Parent instruction_set class.
  InstructionSet *instruction_set_;
  // Parser context.
  SlotDeclCtx *ctx_ = nullptr;
  // The default and minimum opcode size specified for the slot.
  int default_instruction_size_ = 1;
  int min_instruction_size_ = std::numeric_limits<int>::max();
  // Default latency for destination operands.
  TemplateExpression *default_latency_ = nullptr;
  // Fallback opcode for failed decodes.
  Instruction *default_instruction_ = nullptr;
  // Number of instances of this slot in the instruction_set instruction word.
  int size_ = 1;
  // True if the slot is a templated slot.
  bool is_templated_;
  bool is_marked_ = false;
  // True if this slot is referenced in a bundle.
  bool is_referenced_ = false;
  // Name of slot.
  std::string name_;
  // Name of slot in PascalCase.
  std::string pascal_name_;
  // Pointer to slot it inherits from.
  std::vector<BaseSlot> base_slots_;
  absl::flat_hash_set<const Slot *> predecessor_set_;
  // Used to list the unique getters for the operands.
  absl::flat_hash_set<std::string> pred_operand_getters_;
  absl::flat_hash_set<std::string> src_operand_getters_;
  absl::flat_hash_set<std::string> dst_operand_getters_;
  // Map of instructions defined in this slot or inherited.
  absl::flat_hash_map<std::string, Instruction *> instruction_map_;
  absl::flat_hash_set<std::string> operand_setters_;
  // Template parameter names.
  std::vector<TemplateFormal *> template_parameters_;
  absl::flat_hash_map<std::string, int> template_parameter_map_;
  absl::flat_hash_map<std::string, TemplateExpression *> constant_map_;
  // Named resource specifiers.
  absl::btree_map<std::string, ResourceDetailsCtx *> resource_spec_map_;
  absl::btree_map<std::string, IdentListCtx *> resource_array_ref_map_;
  // Default instruction attributes.
  absl::btree_map<std::string, TemplateExpression *> attribute_map_;
};

}  // namespace instruction_set
}  // namespace machine_description
}  // namespace sim
}  // namespace mpact

#endif  // MPACT_SIM_DECODER_SLOT_H_
