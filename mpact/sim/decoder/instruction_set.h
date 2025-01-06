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

#ifndef MPACT_SIM_DECODER_INSTRUCTION_SET_H_
#define MPACT_SIM_DECODER_INSTRUCTION_SET_H_

#include <memory>
#include <string>
#include <tuple>
#include <vector>

#include "absl/container/btree_map.h"
#include "absl/container/btree_set.h"
#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "mpact/sim/decoder/bundle.h"
#include "mpact/sim/decoder/instruction.h"
#include "mpact/sim/decoder/opcode.h"
#include "mpact/sim/decoder/resource.h"
#include "mpact/sim/decoder/slot.h"

namespace mpact {
namespace sim {
namespace machine_description {
namespace instruction_set {

// This class represents the top level of an isa decode declaration. The
// InstructionSet class contains (has-a) bundle, which is the top level bundle
// of the instruction set architecture. Its name is the same as that of the
// InstructionSet.
class InstructionSet {
 public:
  struct StringPair {
    std::string h_output;
    std::string cc_output;
  };

  // Constructor and destructor.
  explicit InstructionSet(absl::string_view name);
  virtual ~InstructionSet();

  // Add bundle and slot to instruction set.
  void AddBundle(Bundle *bundle);
  void AddSlot(Slot *slot);
  void PrependNamespace(absl::string_view namespace_name);

  // Look up bundle and slot names and return pointers to their respective
  // objects. If not found, return nullptr.
  Bundle *GetBundle(absl::string_view) const;
  Slot *GetSlot(absl::string_view) const;
  // Compute the set of reachable bundles and slots.
  void ComputeSlotAndBundleOrders();
  // Analyze the resource use in the opcodes in the slots. This is done to
  // determine which resources must be modeled by complex resource types vs
  // simple resource types.
  absl::Status AnalyzeResourceUse();
  // Return strings containing class declarations, definitions, and opcode enum
  // class.
  std::string GenerateClassDeclarations(absl::string_view file_name,
                                        absl::string_view opcode_file_name,
                                        absl::string_view encoding_type) const;
  std::string GenerateClassDefinitions(absl::string_view include_file,
                                       absl::string_view encoding_type) const;
  std::tuple<std::string, std::string> GenerateEncClasses(
      absl::string_view file_name, absl::string_view opcode_file_name,
      absl::string_view encoder_type) const;
  // This method is static, as it considers all the instruction sets that were
  // defined.
  StringPair GenerateEnums(absl::string_view file_name);

  static void AddAttributeName(const std::string &name);

  void AddInstruction(Instruction *inst) {
    if (instruction_map_.contains(inst->opcode()->name())) return;
    instruction_map_.emplace(inst->opcode()->name(), inst);
  }

  // Getters and setters.
  std::vector<std::string> &namespaces() { return namespaces_; }
  const std::string &name() const { return name_; }
  const std::string &pascal_name() const { return pascal_name_; }
  void set_bundle(Bundle *bundle) { bundle_ = bundle; }
  Bundle *bundle() { return bundle_; }
  OpcodeFactory *opcode_factory() const { return opcode_factory_.get(); }
  ResourceFactory *resource_factory() const { return resource_factory_.get(); }
  // Attribute names are shared across the isa's.
  static const absl::btree_set<std::string> *attribute_names() {
    return attribute_names_;
  }
  absl::flat_hash_map<std::string, Bundle *> &bundle_map() {
    return bundle_map_;
  }
  absl::flat_hash_map<std::string, Slot *> &slot_map() { return slot_map_; }

  // Maps from operand names to enum values.
  absl::flat_hash_map<std::string, int> &pred_op_map() { return pred_op_map_; }
  absl::flat_hash_map<std::string, int> &source_op_map() {
    return source_op_map_;
  }
  absl::flat_hash_map<std::string, int> &list_source_op_map() {
    return list_source_op_map_;
  }
  absl::flat_hash_map<std::string, int> &dest_op_map() { return dest_op_map_; }
  absl::flat_hash_map<std::string, int> &list_dest_op_map() {
    return list_dest_op_map_;
  }

  std::string GenerateEncodingFunctions() const;

 private:
  std::string GenerateOperandEncoder(int position, absl::string_view op_name,
                                     const OperandLocator &locator,
                                     const Opcode *opcode) const;
  // Add bundle and slot to list of classes that need to be generated.
  void AddToBundleOrder(Bundle *);
  void AddToSlotOrder(Slot *);
  // Data members.
  std::vector<std::string> namespaces_;
  std::vector<Slot *> slot_order_;
  std::vector<Bundle *> bundle_order_;
  std::unique_ptr<OpcodeFactory> opcode_factory_;
  std::unique_ptr<ResourceFactory> resource_factory_;
  std::string name_;
  // Name in PascalCase.
  std::string pascal_name_;
  Bundle *bundle_ = nullptr;
  // Map from instruction name to pointer.
  absl::btree_map<std::string, Instruction *> instruction_map_;
  // Maps from names to bundle/slot pointers.
  absl::flat_hash_map<std::string, Bundle *> bundle_map_;
  absl::flat_hash_map<std::string, Slot *> slot_map_;
  // Attribute name list - shared across all the isas.
  static absl::btree_set<std::string> *attribute_names_;
  // Maps from operand names to enum values.
  absl::flat_hash_map<std::string, int> pred_op_map_;
  absl::flat_hash_map<std::string, int> source_op_map_;
  absl::flat_hash_map<std::string, int> list_source_op_map_;
  absl::flat_hash_map<std::string, int> dest_op_map_;
  absl::flat_hash_map<std::string, int> list_dest_op_map_;
};

}  // namespace instruction_set
}  // namespace machine_description
}  // namespace sim
}  // namespace mpact

#endif  // MPACT_SIM_DECODER_INSTRUCTION_SET_H_
