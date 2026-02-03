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

#include "mpact/sim/decoder/instruction_set.h"

#include <memory>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "absl/container/btree_map.h"
#include "absl/container/btree_set.h"
#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "mpact/sim/decoder/bundle.h"
#include "mpact/sim/decoder/format_name.h"
#include "mpact/sim/decoder/instruction.h"
#include "mpact/sim/decoder/opcode.h"
#include "mpact/sim/decoder/resource.h"
#include "mpact/sim/decoder/slot.h"

namespace mpact {
namespace sim {
namespace machine_description {
namespace instruction_set {

static void EmitEnumNames(const absl::btree_set<std::string>& names,
                          absl::string_view namespace_name,
                          absl::string_view op_name, std::string& h_output,
                          std::string& cc_output) {
  // Emit array of enum names.
  absl::StrAppend(&cc_output, "const char *k", op_name,
                  "Names[static_cast<int>(", op_name,
                  "Enum::kPastMaxValue)] = {\n"
                  "  ",
                  namespace_name, "::kNoneName,\n");
  absl::StrAppend(&h_output, "namespace ", namespace_name,
                  " {\n"
                  "  constexpr char kNoneName[] = \"none\";\n");
  for (auto const& name : names) {
    absl::StrAppend(&h_output, "  constexpr char k", name, "Name[] = \"", name,
                    "\";\n");
    absl::StrAppend(&cc_output, "  ", namespace_name, "::k", name, "Name,\n");
  }
  absl::StrAppend(&cc_output, "};\n\n");
  absl::StrAppend(&h_output, "}  // namespace ", namespace_name,
                  "\n\n"
                  "  extern const char *k",
                  op_name, "Names[static_cast<int>(", op_name,
                  "Enum::kPastMaxValue)];\n\n");
}

InstructionSet::InstructionSet(absl::string_view name)
    : opcode_factory_(std::make_unique<OpcodeFactory>()),
      resource_factory_(std::make_unique<ResourceFactory>()),
      name_(name),
      pascal_name_(ToPascalCase(name)) {}

InstructionSet::~InstructionSet() {
  delete bundle_;
  for (auto& [unused, bundle_ptr] : bundle_map_) {
    delete bundle_ptr;
  }
  for (auto& [unused, slot_ptr] : slot_map_) {
    delete slot_ptr;
  }
  bundle_map_.clear();
  slot_map_.clear();
}

void InstructionSet::AddBundle(Bundle* bundle) {
  bundle_map_.insert({bundle->name(), bundle});
}

void InstructionSet::AddSlot(Slot* slot) {
  slot_map_.insert({slot->name(), slot});
}

// Lookup bundle and slot by name.
Bundle* InstructionSet::GetBundle(absl::string_view bundle_name) const {
  auto iter = bundle_map_.find(bundle_name);
  if (iter == bundle_map_.end()) return nullptr;
  return iter->second;
}

Slot* InstructionSet::GetSlot(absl::string_view slot_name) const {
  auto iter = slot_map_.find(slot_name);
  if (iter == slot_map_.end()) return nullptr;
  return iter->second;
}

absl::Status InstructionSet::AnalyzeResourceUse() {
  for (auto const* slot : slot_order_) {
    for (auto& [unused, inst_ptr] : slot->instruction_map()) {
      for (auto const* def : inst_ptr->resource_acquire_vec()) {
        if (def->begin_expression != nullptr) {
          auto result = def->begin_expression->GetValue();
          if (!result.ok()) return result.status();
          if (std::get<int>(result.value()) != 0) {
            def->resource->set_is_simple(false);
          }
        }
        if (def->end_expression != nullptr) {
          auto result = def->end_expression->GetValue();
          if (!result.ok()) return result.status();
        }
      }
    }
  }
  return absl::OkStatus();
}

void InstructionSet::ComputeSlotAndBundleOrders() {
  // Compute order of slot definitions
  for (auto const& [unused, slot_ptr] : slot_map_) {
    if (slot_ptr->is_marked()) continue;
    AddToSlotOrder(slot_ptr);
  }

  // Compute order of bundle definitions
  for (auto const& [unused, bundle_ptr] : bundle_map_) {
    if (bundle_ptr->is_marked()) continue;
    AddToBundleOrder(bundle_ptr);
  }
}

void InstructionSet::AddToBundleOrder(Bundle* bundle) {
  if (bundle->is_marked()) return;
  for (auto const& bundle_name : bundle->bundle_names()) {
    Bundle* sub_bundle = bundle_map_[bundle_name];
    AddToBundleOrder(sub_bundle);
  }
  bundle_order_.push_back(bundle);
  bundle->set_is_marked(true);
}

void InstructionSet::AddToSlotOrder(Slot* slot) {
  if (slot->is_marked()) return;
  for (auto const& base_slot : slot->base_slots()) {
    AddToSlotOrder(slot_map_[base_slot.base->name()]);
  }
  slot->set_is_marked(true);
  slot_order_.push_back(slot);
}

// Return a string containing class header file, for all bundles and slots.
// As an example the following shows an abbreviated example of code that is
// generated.
//
// class MyEncodingType {
//  public:
//   virtual ~MyEncodingType() = default;
//
//   virtual OpcodeEnum GetOpcode(SlotEnum slot, int entry) = 0;
//   virtual ResourceOperandInterface *GetSimpleResource(
//               SlotEnum slot,
//               int entry,
//               OpcodeEnum opcode,
//               SimpleResourceEnum resource) = 0;
//   virtual ResourceOperandInterface *GetComplexResource(
//               SlotEnum slot,
//               int entry,
//               OpcodeEnum opcode,
//               ComplexResourceEnum) = 0;
//   virtual PredicateOperandInterface *GetPredicate(SlotEnum slot,
//                                                   int entry,
//                                                   OpcodeEnum opcode,
//                                                   PredOpEnum pred) = 0;
//   virtual SourceOperandInterface *GetSource(SlotEnum slot,
//                                             int entry,
//                                             OpcodeEnum opcode,
//                                             SourceOpEnum source,
//                                             int source_no) = 0;
//   virtual std::vector<SourceOperandInterface *> GetSources(
//               SlotEnum slot,
//               int entry,
//               OpcodeEnum opcode,
//               ListSourceOpEnum list_source_op,
//               int source_no) = 0;
//   virtual DestinationOperandInterface *GetDestination(int latency,
//                                                       SlotEnum slot,
//                                                       int entry,
//                                                       OpcodeEnum opcode,
//                                                       DestOpEnum dest,
//                                                       int dest_no) = 0;
//   virtual std::vector<DestinationOperandInterface *> GetDestinations(
//               SlotEnum slot,
//               int entry,
//               OpcodeEnum opcode,
//               ListDestOpEnum dest_op,
//               int dest_no) = 0;
//   virtual int GetLatency(SlotEnum slot, int entry, OpcodeEnum opcode,
//                          DestOpEnum dest) = 0;
//   virtual ResourceOperandInterface *GetSimpleResourceOperand(
//               SlotEnum slot,
//               int entry,
//               OpcodeEnum opcode,
//               SimpleResourceVector resource_vec,
//               int end) = 0;
//   virtual ResourceOperandInterface *GetComplexResourceOperand(
//               SlotEnum slot,
//               int entry,
//               OpcodeEnum opcode,
//               ComplexResourceEnum resource,
//               int begin,
//               int end) = 0;
// };
//
//  // Alu0 Slot class.
//  class ScalarAlu0Slot {
//   public:
//    explicit ScalarAlu0Slot(ArchState *arch_state);
//    virtual ~ScalarAlu0Slot() = default;
//    virtual SemFunc GetNopSemFunc() = 0;
//    virtual SemFunc GetSvctS32F32SemFunc() = 0;
//    virtual SemFunc GetScvtF32S32SemFunc() = 0;
//    ...
//    Instruction *Decode(uint64_t address, MyEncodingType* isa_encoding,
//                        MyEncodingType::SlotEnum, int entry);
//
//   private:
//    ArchState *arch_state_;
//    SemFuncGetters semantic_function_getter_;
//    OperandSetters set_operands_;
//    static constexpr MyEncodingType::SlotEnum slot_ =
//        MyEncodingType::SlotEnum::kScalarAlu0;
//  };
//
//  class ScalarBundleDecoder {
//   public:
//    explicit ScalarBundleDecoder(ArchState *arch_state);
//    virtual ~ScalarBundleDecoder() = default;
//    virtual Instruction *Decode(uint64_t address, MyEncodingType *encoding);
//    virtual SemFunc GetSemanticFunction() = 0;
//
//    ScalarAlu0Slot *scalar_alu_0_decoder() {
//      return scalar_alu_0_decoder_.get();
//    }
//    ScalarAlu1Slot *scalar_alu_1_decoder() {
//      return scalar_alu_1_decoder_.get();
//    }
//
//   private:
//    std::unique_ptr<ScalarAlu0Slot> scalar_alu_0_decoder_;
//    std::unique_ptr<ScalarAlu1Slot> scalar_alu_1_decoder_;
//    ArchState *arch_state_;
//  };
//
//  class InstructionSetFactory{
//   public:
//    InstructionSetFactory() = default;
//    virtual ~InstructionSetFactory() = default;
//    virtual std::unique_ptr<ScalarBundleDecoder> CreateScalarBundleDecoder(
//                                                     ArchState *) = 0;
//    virtual std::unique_ptr<ScalarAlu0Slot> CreateScalarAlu0Slot(
//                                                     ArchState *) = 0;
//    virtual std::unique_ptr<ScalarAlu1Slot> CreateScalarAlu1Slot(
//                                                     ArchState *) = 0;
//  };
//
//  class InstructionSet {
//   public:
//    InstructionSet(ArchState *arch_state, InstructionSetFactory *factory);
//    Instruction *Decode(uint64_t address, MyEncodingType *encoding);
//
//   private:
//    std::unique_ptr<ScalarBundleDecoder> scalar_bundle_decoder_;
//    ArchState *arch_state_;
//  };

std::string InstructionSet::GenerateClassDeclarations(
    absl::string_view file_name, absl::string_view opcode_file_name,
    absl::string_view encoding_type) const {
  std::string output;

  std::string factory_class_name = pascal_name() + "InstructionSetFactory";

  absl::StrAppend(&output, "class ", factory_class_name, ";\n");
  for (auto const* slot : slot_order_) {
    absl::StrAppend(&output, slot->GenerateClassDeclaration(encoding_type));
  }

  for (auto const* bundle : bundle_order_) {
    absl::StrAppend(&output, bundle->GenerateClassDeclaration(encoding_type));
  }
  // Generate factory class.
  absl::StrAppend(&output, "class ", factory_class_name,
                  "{\n"
                  " public:\n"
                  "  ",
                  factory_class_name,
                  "() = default;\n"
                  "  virtual ~",
                  factory_class_name, "() = default;\n");
  for (auto const bundle : bundle_order_) {
    if (bundle->is_marked()) {
      std::string bundle_class = bundle->pascal_name() + "Decoder";
      absl::StrAppend(&output, "  virtual std::unique_ptr<", bundle_class,
                      "> Create", bundle_class, "(ArchState *) = 0;\n");
    }
  }
  for (auto const slot : slot_order_) {
    if (slot->is_referenced()) {
      std::string slot_class = slot->pascal_name() + "Slot";
      absl::StrAppend(&output, "  virtual std::unique_ptr<", slot_class,
                      "> Create", slot_class, "(ArchState *) = 0;\n");
    }
  }
  absl::StrAppend(&output,
                  "};\n"
                  "\n");
  // Generate InstructionSet class.
  absl::StrAppend(&output, "class ", pascal_name(),
                  "InstructionSet {\n"
                  " public:\n"
                  "  ",
                  pascal_name(), "InstructionSet(ArchState *arch_state,\n",
                  Indent(absl::StrCat("  ", pascal_name(), "InstructionSet(")),
                  factory_class_name,
                  " *factory);\n"
                  "  virtual ~",
                  pascal_name(),
                  "InstructionSet();\n"
                  "  Instruction *Decode(uint64_t address, ",
                  encoding_type,
                  " *encoding);\n"
                  "\n"
                  " private:\n");
  for (auto const& bundle_name : bundle_->bundle_names()) {
    absl::StrAppend(&output, "  std::unique_ptr<", ToPascalCase(bundle_name),
                    "Decoder> ", bundle_name, "_decoder_;\n");
  }
  for (auto const& [slot_name, unused] : bundle_->slot_uses()) {
    absl::StrAppend(&output, "  std::unique_ptr<", ToPascalCase(slot_name),
                    "Slot> ", slot_name, "_decoder_;\n");
  }
  absl::StrAppend(&output,
                  "  ArchState *arch_state_;\n"
                  "};\n"
                  "\n");
  return output;
}

// Return string containing .cc file for all bundles and slots.
std::string InstructionSet::GenerateClassDefinitions(
    absl::string_view include_file, absl::string_view encoding_type) const {
  std::string output;

  std::string class_name = pascal_name() + "InstructionSet";
  std::string factory_class_name = class_name + "Factory";
  for (auto* slot : slot_order_) {
    absl::StrAppend(&output, slot->GenerateClassDefinition(encoding_type));
  }
  // Constructor.
  absl::StrAppend(&output, class_name, "::", class_name,
                  "(ArchState *arch_state, ", factory_class_name,
                  "*factory) : \n"
                  "  arch_state_(arch_state) {\n");
  for (auto const& bundle_name : bundle_->bundle_names()) {
    absl::StrAppend(&output, "  ", bundle_name, "_decoder_ = factory->Create",
                    ToPascalCase(bundle_name), "Decoder(arch_state_);\n");
  }
  for (auto const& [slot_name, unused] : bundle_->slot_uses()) {
    absl::StrAppend(&output, "  ", slot_name, "_decoder_ = factory->Create",
                    ToPascalCase(slot_name), "Slot(arch_state_);\n");
  }
  absl::StrAppend(&output, "}\n");
  // Destructor.
  absl::StrAppend(&output, class_name, "::~", class_name, "() {\n");
  absl::StrAppend(&output, "  // empty for now.\n");
  absl::StrAppend(&output, "}\n");
  // Generate the top level decode function body.
  absl::StrAppend(&output, "Instruction *", class_name,
                  "::Decode(uint64_t address, ", encoding_type,
                  " *encoding) {\n"
                  "  Instruction *inst = nullptr;"
                  "  Instruction *tmp_inst;\n"
                  "  bool success = false;\n"
                  "  int size = 0;\n");
  if (!bundle_->bundle_names().empty()) {
    // If there are bundles, then a "parent instruction" is created. Bundles
    // are added as children of this parent instruction. The parent instruction
    // is responsible for controlling issue of the bundles.
    absl::StrAppend(&output,
                    "  inst = new Instruction(address, arch_state_);\n");
    // Generate calls to each of the top level bundle Decode methods.
    for (auto const& bundle_name : bundle_->bundle_names()) {
      absl::StrAppend(&output, "  tmp_inst = ", bundle_name,
                      "_decoder_->Decode(address, encoding);\n"
                      "  inst->AppendChild(tmp_inst);\n"
                      "  size += tmp_inst->size();\n"
                      "  tmp_inst->DecRef();\n"
                      "  success |= (nullptr != tmp_inst);\n");
    }
  }
  // Generate calls to each of the top level slot Decode methods.
  for (auto const& [slot_name, instance_vec] : bundle_->slot_uses()) {
    std::string enum_name =
        absl::StrCat("SlotEnum::", "k", ToPascalCase(slot_name));
    if (instance_vec.empty()) {
      absl::StrAppend(&output, "  tmp_inst = ", slot_name,
                      "_decoder_->Decode(address, encoding, ", enum_name,
                      ", 0);\n"
                      "  if (tmp_inst != nullptr) size += tmp_inst->size();\n"
                      "  if (inst == nullptr) {\n"
                      "    inst = tmp_inst;\n"
                      "  } else {\n"
                      "    inst->Append(tmp_inst);\n"
                      "    tmp_inst->DecRef();\n"
                      "  }\n"
                      "  success |= (nullptr != tmp_inst);\n");
    } else {
      for (auto const index : instance_vec) {
        absl::StrAppend(&output, "  tmp_inst = ", slot_name,
                        "_decoder_->Decode(address, encoding, , ", enum_name,
                        ", ", index,
                        ");\n"
                        "  if (tmp_inst != nullptr) size += tmp_inst->size();\n"
                        "  if (inst == nullptr) {\n"
                        "    inst = tmp_inst;\n"
                        "  } else {\n"
                        "    inst->Append(tmp_inst);\n"
                        "    tmp_inst->DecRef();\n"
                        "  }\n"
                        "  success |= (nullptr != tmp_inst);\n");
      }
    }
  }
  // If the decode failed, DecRef the instruction and return nullptr.
  absl::StrAppend(&output,
                  "  inst->set_size(size);\n"
                  "  if (!success) {\n"
                  "    inst->DecRef();\n"
                  "    inst = nullptr;\n"
                  "  }\n"
                  "  return inst;\n"
                  "}\n");
  return output;
}

InstructionSet::StringPair InstructionSet::GenerateEnums(
    absl::string_view file_name) {
  std::string h_output;
  std::string cc_output;

  // Emit slot enumeration type.
  absl::StrAppend(&h_output,
                  "  enum class SlotEnum {\n"
                  "    kNone = 0,\n");
  absl::btree_map<std::string, const Slot*> slots_by_name;
  for (auto const* slot : slot_order_) {
    if (slot->is_referenced()) {
      std::string name = slot->pascal_name();
      slots_by_name.emplace(name, slot);
    }
  }
  for (auto const& [name, unused] : slots_by_name) {
    absl::StrAppend(&h_output, "    k", name, ",\n");
  }
  absl::StrAppend(&h_output, "  };\n\n");

  // Btree sets to sort by name.
  absl::btree_set<std::string> predicate_operands;
  absl::btree_set<std::string> source_operands;
  absl::btree_set<std::string> list_source_operands;
  absl::btree_set<std::string> dest_operands;
  absl::btree_set<std::string> list_dest_operands;
  absl::btree_set<std::string> dest_latency;
  // Insert PascalCase operand names into the sets to select unique names.
  for (auto const& [unused, slot] : slots_by_name) {
    // Slot specific operands.
    absl::btree_set<std::string> slot_predicate_operands;
    absl::btree_set<std::string> slot_source_operands;
    absl::btree_set<std::string> slot_list_source_operands;
    absl::btree_set<std::string> slot_dest_operands;
    absl::btree_set<std::string> slot_list_dest_operands;
    for (auto const& [unused, inst_ptr] : slot->instruction_map()) {
      auto* inst = inst_ptr;
      while (inst != nullptr) {
        auto* opcode = inst->opcode();
        if (!opcode->predicate_op_name().empty()) {
          predicate_operands.insert(ToPascalCase(opcode->predicate_op_name()));
          slot_predicate_operands.insert(
              ToPascalCase(opcode->predicate_op_name()));
        }
        for (auto const& source_op : opcode->source_op_vec()) {
          if (source_op.is_array) {
            list_source_operands.insert(ToPascalCase(source_op.name));
            slot_list_source_operands.insert(ToPascalCase(source_op.name));
          } else {
            source_operands.insert(ToPascalCase(source_op.name));
            slot_source_operands.insert(ToPascalCase(source_op.name));
          }
        }
        for (auto const* dest_op : opcode->dest_op_vec()) {
          if (dest_op->is_array()) {
            list_dest_operands.insert(dest_op->pascal_case_name());
            slot_list_dest_operands.insert(dest_op->pascal_case_name());
          } else {
            dest_operands.insert(dest_op->pascal_case_name());
            slot_dest_operands.insert(dest_op->pascal_case_name());
          }
          if (dest_op->expression() == nullptr) {
            dest_latency.insert(dest_op->pascal_case_name());
          }
        }
        inst = inst->child();
      }
    }
    auto slot_name = slot->pascal_name();
    absl::StrAppend(&h_output, "  // Enums for slot: ", slot_name, ".\n");
    // Create Slot specific enum for predicate operands.
    absl::StrAppend(&h_output, "  enum class ", slot_name, "PredOpEnum {\n");
    int pred_count = 0;
    absl::StrAppend(&h_output, "    kNone = ", pred_count++, ",\n");
    for (auto const& pred_name : slot_predicate_operands) {
      pred_op_map_.insert({pred_name, pred_count});
      absl::StrAppend(&h_output, "    k", pred_name, " = ", pred_count++,
                      ",\n");
    }
    absl::StrAppend(&h_output, "    kPastMaxValue = ", pred_count,
                    ",\n"
                    "  };\n\n");
    // Create Slot specific enum for source operands.
    absl::StrAppend(&h_output, "  enum class ", slot_name, "SourceOpEnum {\n");
    int src_count = 0;
    absl::StrAppend(&h_output, "    kNone = ", src_count++, ",\n");
    for (auto const& source_name : slot_source_operands) {
      source_op_map_.insert({source_name, src_count});
      absl::StrAppend(&h_output, "    k", source_name, " = ", src_count++,
                      ",\n");
    }
    absl::StrAppend(&h_output, "    kPastMaxValue = ", src_count,
                    ",\n"
                    "  };\n\n");
    // Create Slot specific enum for list source operands.
    absl::StrAppend(&h_output, "  enum class ", slot_name,
                    "ListSourceOpEnum {\n");
    int list_src_count = 0;
    absl::StrAppend(&h_output, "    kNone = ", list_src_count++, ",\n");
    for (auto const& source_name : slot_list_source_operands) {
      list_source_op_map_.insert({source_name, list_src_count});
      absl::StrAppend(&h_output, "    k", source_name, " = ", list_src_count++,
                      ",\n");
    }
    absl::StrAppend(&h_output, "    kPastMaxValue = ", list_src_count,
                    ",\n"
                    "  };\n\n");
    // Create Slot specific enum for destination operands.
    absl::StrAppend(&h_output, "  enum class ", slot_name, "DestOpEnum {\n");
    int dst_count = 0;
    absl::StrAppend(&h_output, "    kNone = ", dst_count++, ",\n");
    for (auto const& dest_name : slot_dest_operands) {
      dest_op_map_.insert({dest_name, dst_count});
      absl::StrAppend(&h_output, "    k", dest_name, " = ", dst_count++, ",\n");
    }
    absl::StrAppend(&h_output, "    kPastMaxValue = ", dst_count,
                    ",\n"
                    "  };\n\n");
    // Create Slot specific enum for list destination operands.
    absl::StrAppend(&h_output, "  enum class ", slot_name,
                    "ListDestOpEnum {\n");
    int list_dst_count = 0;
    absl::StrAppend(&h_output, "    kNone = ", list_dst_count++, ",\n");
    for (auto const& dest_name : slot_list_dest_operands) {
      list_dest_op_map_.insert({dest_name, list_dst_count});
      absl::StrAppend(&h_output, "    k", dest_name, " = ", list_dst_count++,
                      ",\n");
    }
    absl::StrAppend(&h_output, "    kPastMaxValue = ", list_dst_count,
                    ",\n"
                    "  };\n\n");
  }
  // Create enums for the global view.

  absl::StrAppend(&h_output, "  // Enums for the global view.\n");
  // Create enum for predicate operands.
  absl::StrAppend(&h_output, "  enum class PredOpEnum {\n");
  int pred_count = 0;
  absl::StrAppend(&h_output, "    kNone = ", pred_count++, ",\n");
  for (auto const& pred_name : predicate_operands) {
    pred_op_map_.insert({pred_name, pred_count});
    absl::StrAppend(&h_output, "    k", pred_name, " = ", pred_count++, ",\n");
  }
  absl::StrAppend(&h_output, "    kPastMaxValue = ", pred_count,
                  ",\n"
                  "  };\n\n");
  // Create enum for source operands.
  absl::StrAppend(&h_output, "  enum class SourceOpEnum {\n");
  int src_count = 0;
  absl::StrAppend(&h_output, "    kNone = ", src_count++, ",\n");
  for (auto const& source_name : source_operands) {
    source_op_map_.insert({source_name, src_count});
    absl::StrAppend(&h_output, "    k", source_name, " = ", src_count++, ",\n");
  }
  absl::StrAppend(&h_output, "    kPastMaxValue = ", src_count,
                  ",\n"
                  "  };\n\n");
  // Create enum for list source operands.
  absl::StrAppend(&h_output, "  enum class ListSourceOpEnum {\n");
  int list_src_count = 0;
  absl::StrAppend(&h_output, "    kNone = ", list_src_count++, ",\n");
  for (auto const& source_name : list_source_operands) {
    list_source_op_map_.insert({source_name, list_src_count});
    absl::StrAppend(&h_output, "    k", source_name, " = ", list_src_count++,
                    ",\n");
  }
  absl::StrAppend(&h_output, "    kPastMaxValue = ", list_src_count,
                  ",\n"
                  "  };\n\n");
  // Create enum for destination operands.
  absl::StrAppend(&h_output, "  enum class DestOpEnum {\n");
  int dst_count = 0;
  absl::StrAppend(&h_output, "    kNone = ", dst_count++, ",\n");
  for (auto const& dest_name : dest_operands) {
    dest_op_map_.insert({dest_name, dst_count});
    absl::StrAppend(&h_output, "    k", dest_name, " = ", dst_count++, ",\n");
  }
  absl::StrAppend(&h_output, "    kPastMaxValue = ", dst_count,
                  ",\n"
                  "  };\n\n");
  // Create enum for list destination operands.
  absl::StrAppend(&h_output, "  enum class ListDestOpEnum {\n");
  int list_dst_count = 0;
  absl::StrAppend(&h_output, "    kNone = ", list_dst_count++, ",\n");
  for (auto const& dest_name : list_dest_operands) {
    list_dest_op_map_.insert({dest_name, list_dst_count});
    absl::StrAppend(&h_output, "    k", dest_name, " = ", list_dst_count++,
                    ",\n");
  }
  absl::StrAppend(&h_output, "    kPastMaxValue = ", list_dst_count,
                  ",\n"
                  "  };\n\n");
  // Emit opcode enumeration type.
  absl::StrAppend(&h_output,
                  "  enum class OpcodeEnum {\n"
                  "    kNone = 0,\n");
  absl::btree_set<std::string> name_set;
  for (auto const* opcode : opcode_factory_->opcode_vec()) {
    name_set.insert(opcode->pascal_name());
  }
  int opcode_value = 1;
  for (auto const& name : name_set) {
    absl::StrAppend(&h_output, "    k", name, " = ", opcode_value++, ",\n");
  }
  absl::StrAppend(&h_output, "    kPastMaxValue = ", opcode_value, "\n");
  absl::StrAppend(&h_output, "  };\n\n");

  EmitEnumNames(predicate_operands, "pred_op_names", "PredOp", h_output,
                cc_output);
  EmitEnumNames(source_operands, "source_op_names", "SourceOp", h_output,
                cc_output);
  EmitEnumNames(list_source_operands, "list_source_op_names", "ListSourceOp",
                h_output, cc_output);
  EmitEnumNames(dest_operands, "dest_op_names", "DestOp", h_output, cc_output);
  EmitEnumNames(list_dest_operands, "list_dest_op_names", "ListDestOp",
                h_output, cc_output);
  // Emit array of opcode names.
  absl::StrAppend(&cc_output,
                  "const char *kOpcodeNames[static_cast<int>("
                  "OpcodeEnum::kPastMaxValue)] = {\n"
                  "  kNoneName,\n");
  absl::StrAppend(&h_output, "  constexpr char kNoneName[] = \"none\";\n");
  for (auto const& name : name_set) {
    absl::StrAppend(&h_output, "  constexpr char k", name, "Name[] = \"", name,
                    "\";\n");
    absl::StrAppend(&cc_output, "  k", name, "Name,\n");
  }
  absl::StrAppend(&cc_output, "};\n\n");
  absl::StrAppend(&h_output,
                  "  extern const char *kOpcodeNames[static_cast<int>(\n"
                  "      OpcodeEnum::kPastMaxValue)];\n\n");
  // Emit resource enumeration types.
  absl::StrAppend(&h_output,
                  "  enum class SimpleResourceEnum {\n"
                  "    kNone = 0,\n");
  int resource_count = 1;
  name_set.clear();
  for (auto const& [unused, resource_ptr] : resource_factory_->resource_map()) {
    if (resource_ptr->is_simple()) {
      name_set.insert(resource_ptr->pascal_name());
    }
  }
  for (auto const& name : name_set) {
    absl::StrAppend(&h_output, "    k", name, " = ", resource_count++, ",\n");
  }
  absl::StrAppend(&h_output, "    kPastMaxValue = ", resource_count,
                  "\n  };\n\n");
  EmitEnumNames(name_set, "simple_resource_names", "SimpleResource", h_output,
                cc_output);
  // Complex resource enumeration type.
  absl::StrAppend(&h_output,
                  "  enum class ComplexResourceEnum {\n"
                  "    kNone = 0,\n");
  resource_count = 1;
  name_set.clear();
  for (auto const& [unused, resource_ptr] : resource_factory_->resource_map()) {
    if (!resource_ptr->is_simple() && !resource_ptr->is_array()) {
      name_set.insert(resource_ptr->pascal_name());
    }
  }
  for (auto const& name : name_set) {
    absl::StrAppend(&h_output, "    k", name, " = ", resource_count++, ",\n");
  }
  absl::StrAppend(&h_output, "    kPastMaxValue = ", resource_count,
                  "\n  };\n\n");
  EmitEnumNames(name_set, "complex_resource_names", "ComplexResource", h_output,
                cc_output);
  // List complex resource enumeration type.
  absl::StrAppend(&h_output,
                  "  enum class ListComplexResourceEnum {\n"
                  "    kNone = 0,\n");
  resource_count = 1;
  name_set.clear();
  for (auto const& [unused, resource_ptr] : resource_factory_->resource_map()) {
    if (!resource_ptr->is_simple() && resource_ptr->is_array()) {
      name_set.insert(resource_ptr->pascal_name());
    }
  }
  for (auto const& name : name_set) {
    absl::StrAppend(&h_output, "    k", name, " = ", resource_count++, ",\n");
  }
  absl::StrAppend(&h_output, "    kPastMaxValue = ", resource_count,
                  "\n  };\n\n");
  EmitEnumNames(name_set, "list_complex_resource_names", "ListComplexResource",
                h_output, cc_output);
  // Emit instruction attribute types.

  for (auto const& [name, slot_ptr] : slots_by_name) {
    if (slot_ptr->attribute_names().empty()) continue;
    absl::StrAppend(&h_output, "namespace ", ToSnakeCase(name), " {\n\n");
    absl::StrAppend(&h_output, "  enum class AttributeEnum {\n");
    int attribute_count = 0;
    if (!slot_ptr->attribute_names().empty()) {
      for (auto const& attribute_name : slot_ptr->attribute_names()) {
        absl::StrAppend(&h_output, "    k", ToPascalCase(attribute_name), " = ",
                        attribute_count++, ",\n");
      }
    }
    absl::StrAppend(&h_output, "    kPastMaxValue = ", attribute_count,
                    "\n  };\n\n");
    absl::StrAppend(&h_output, "}  // namespace ", name, "\n\n");
  }

  return {h_output, cc_output};
}

std::string InstructionSet::GenerateOperandEncoder(
    int position, absl::string_view op_name, const OperandLocator& locator,
    const Opcode* opcode) const {
  std::string output;
  switch (locator.type) {
    case OperandLocator::kPredicate: {
      std::string pred_op =
          absl::StrCat("PredOpEnum::k", ToPascalCase(op_name));
      absl::StrAppend(&output, "  // Predicate operand ", op_name, "\n");
      absl::StrAppend(
          &output, "  result = encoder->GetPredOpEncoding(address, operands[",
          position,
          "], slot, "
          "entry, opcode, ",
          pred_op, ", resolver);\n");
      break;
    }
    case OperandLocator::kSource: {
      std::string source_op =
          absl::StrCat("SourceOpEnum::k", ToPascalCase(op_name));
      absl::StrAppend(&output, "  // Source operand ", op_name, "\n");
      if (locator.is_reloc) {
        absl::StrAppend(&output,
                        "  auto status = encoder->AppendSrcOpRelocation(\n"
                        "      address, operands[",
                        position, "], slot, entry, opcode, ", source_op, ", ",
                        locator.instance,
                        ", resolver, relocations);\n"
                        "  if (!status.ok()) return status;\n");
      }
      absl::StrAppend(&output,
                      "  result = encoder->GetSrcOpEncoding(address, operands[",
                      position,
                      "], slot, "
                      "entry, opcode, ",
                      source_op, ", ", locator.instance, ", resolver);\n");
      break;
    }
    case OperandLocator::kSourceArray: {
      std::string list_source_op =
          absl::StrCat("ListSourceOpEnum::k", ToPascalCase(op_name));
      absl::StrAppend(&output, "  // Source array operand ", op_name, "\n");
      absl::StrAppend(
          &output,
          "  result = encoder->GetListSrcOpEncoding(address, operands[",
          position,
          "], slot, "
          "entry, opcode, ",
          list_source_op, ", ", locator.instance, ", resolver);\n");
      break;
    }
    case OperandLocator::kDestination: {
      std::string dest_op =
          absl::StrCat("DestOpEnum::k", ToPascalCase(op_name));
      absl::StrAppend(&output, "  // Destination operand ", op_name, "\n");
      if (locator.is_reloc) {
        absl::StrAppend(&output,
                        "  auto status = encoder->AppendDestOpRelocation(\n"
                        "      address, operands[",
                        position, "], slot, entry, opcode, ", dest_op, ", ",
                        locator.instance,
                        ", resolver, relocations);\n"
                        "  if (!status.ok()) return status;\n");
      }
      absl::StrAppend(
          &output, "  result = encoder->GetDestOpEncoding(address, operands[",
          position,
          "], slot, "
          "entry, opcode, ",
          dest_op, ", ", locator.instance, ", resolver);\n");
      break;
    }
    case OperandLocator::kDestinationArray: {
      std::string list_dest_op =
          absl::StrCat("ListDestOpEnum::k", ToPascalCase(op_name));
      absl::StrAppend(&output, "  // Destination array operand ", op_name,
                      "\n");
      absl::StrAppend(
          &output,
          "  result = encoder->GetListDestOpEncoding(addres, operands[",
          position,
          "], slot, "
          "entry, opcode, ",
          list_dest_op, ", ", locator.instance, ", resolver);\n");
      break;
    }
    default:
      absl::StrAppend(&output, "  #error Unknown operand type ", op_name, "\n");
      break;
  }
  absl::StrAppend(&output,
                  "  if (!result.ok()) return result.status();\n"
                  "  encoding |= result.value();\n");
  return output;
}

std::tuple<std::string, std::string> InstructionSet::GenerateEncClasses(
    absl::string_view file_name, absl::string_view opcode_file_name,
    absl::string_view encoder_type) const {
  std::string h_output;
  std::string cc_output;
  std::string encoder = absl::StrCat(pascal_name(), "EncoderInterfaceBase");
  // Generate the bin encoder base class.
  absl::StrAppend(&h_output,
                  "using ::mpact::sim::util::assembler::RelocationInfo;\n"
                  "using ::mpact::sim::util::assembler::ResolverInterface;\n"
                  "\n"
                  "class ",
                  encoder,
                  " {\n"
                  " public:\n"
                  "  virtual ~",
                  encoder,
                  "() = default;\n"
                  R"(
  // Returns the opcode encoding and size (in bits) of the opcode.
  virtual absl::StatusOr<std::tuple<uint64_t, int>> GetOpcodeEncoding(
      SlotEnum slot, int entry, OpcodeEnum opcode, ResolverInterface *resolver) = 0;
  virtual absl::StatusOr<uint64_t> GetSrcOpEncoding(uint64_t address,
      absl::string_view text, SlotEnum slot, int entry, OpcodeEnum opcode,
      SourceOpEnum source_op, int source_num, ResolverInterface *resolver) = 0;
  virtual absl::Status AppendSrcOpRelocation(uint64_t address,
      absl::string_view text, SlotEnum slot, int entry, OpcodeEnum opcode,
      SourceOpEnum source_op, int source_num, ResolverInterface *resolver,
      std::vector<RelocationInfo> &relocations) = 0;
  virtual absl::StatusOr<uint64_t> GetDestOpEncoding(uint64_t address,
      absl::string_view text, SlotEnum slot, int entry, OpcodeEnum opcode,
      DestOpEnum dest_op, int dest_num, ResolverInterface *resolver) = 0;
  virtual absl::Status AppendDestOpRelocation(uint64_t address,
      absl::string_view text, SlotEnum slot, int entry, OpcodeEnum opcode,
      DestOpEnum dest_op, int dest_num, ResolverInterface *resolver,
      std::vector<RelocationInfo> &relocations) = 0;
  virtual absl::StatusOr<uint64_t> GetListSrcOpEncoding( uint64_t address,
      absl::string_view text,SlotEnum slot, int entry, OpcodeEnum opcode,
      ListSourceOpEnum source_op, int source_num, ResolverInterface *resolver) = 0;
  virtual absl::StatusOr<uint64_t> GetListDestOpEncoding(uint64_t address,
      absl::string_view text, SlotEnum slot, int entry, OpcodeEnum opcode,
      ListDestOpEnum dest_op, int dest_num, ResolverInterface *resolver) = 0;
  virtual absl::StatusOr<uint64_t> GetPredOpEncoding(uint64_t address,
      absl::string_view text, SlotEnum slot, int entry, OpcodeEnum opcode,
      PredOpEnum pred_op, ResolverInterface *resolver) = 0;
};

)");

  absl::StrAppend(&cc_output,
                  "using ::mpact::sim::util::assembler::ResolverInterface;\n"
                  "\n"
                  "namespace {\n\n"
                  "absl::StatusOr<std::tuple<uint64_t, int>> EncodeNone(",
                  encoder,
                  "*, SlotEnum, int, OpcodeEnum, uint64_t, const "
                  "std::vector<std::string> &, ResolverInterface *, "
                  "std::vector<RelocationInfo> &) {\n"
                  "  return absl::NotFoundError(\"No such opcode\");\n"
                  "}\n\n");
  std::string array;
  absl::StrAppend(
      &array,
      "using EncodeFcn = absl::StatusOr<std::tuple<uint64_t, int>> (*)(",
      encoder,
      "*, SlotEnum, int, OpcodeEnum, uint64_t, const "
      "std::vector<std::string> "
      "&, ResolverInterface *, std::vector<RelocationInfo> &);\n"
      "EncodeFcn encode_fcns[] = {\n"
      "  EncodeNone,\n");
  for (auto& [name, inst_ptr] : instruction_map_) {
    std::string prefix;
    std::string suffix;
    auto* opcode = inst_ptr->opcode();
    absl::StrAppend(&array, "  Encode", opcode->pascal_name(), ",\n");
    absl::StrAppend(&prefix, "absl::StatusOr<std::tuple<uint64_t, int>> Encode",
                    opcode->pascal_name(), "(\n     ", encoder,
                    " *encoder, SlotEnum slot, int entry, OpcodeEnum opcode,\n"
                    "     uint64_t address, const "
                    "std::vector<std::string> &operands,\n"
                    "     ResolverInterface *resolver, "
                    "std::vector<RelocationInfo> &relocations) "
                    "{\n");
    absl::StrAppend(&suffix,
                    "  auto res_opcode = encoder->GetOpcodeEncoding(slot, "
                    "entry, opcode, resolver);\n"
                    "  if (!res_opcode.ok()) return res_opcode.status();\n"
                    "  auto [encoding, bit_size] = res_opcode.value();\n"
                    "  absl::StatusOr<uint64_t> result;\n");
    int position = 0;
    for (auto const* disasm_format : inst_ptr->disasm_format_vec()) {
      for (auto const* format_info : disasm_format->format_info_vec) {
        if (format_info->op_name.empty()) continue;
        auto iter = opcode->op_locator_map().find(format_info->op_name);
        if (iter == opcode->op_locator_map().end()) {
          absl::StrAppend(&suffix, "  #error ", format_info->op_name,
                          " not found in instruction opcodes\n");
          continue;
        }
        auto locator = iter->second;
        absl::StrAppend(&suffix,
                        GenerateOperandEncoder(position++, format_info->op_name,
                                               locator, opcode));
      }
    }
    absl::StrAppend(&suffix,
                    "  return std::make_tuple(encoding, bit_size);\n"
                    "}\n\n");
    absl::StrAppend(&cc_output, prefix,
                    "  auto num_args = operands.size();\n"
                    "  if (num_args != ",
                    position,
                    ") {\n"
                    "    return absl::InvalidArgumentError(\n"
                    "        absl::StrCat(\"",
                    opcode->pascal_name(),
                    ": Invalid number of operands (\", "
                    "num_args, \") - expected ",
                    position,
                    "\"));\n"
                    "  }\n",
                    suffix);
  }
  absl::StrAppend(&array, "};\n\n");
  absl::StrAppend(&cc_output, array, "\n}  // namespace\n\n");

  // Generate the regex matcher base class.
  absl::StrAppend(
      &h_output,
      "// Assembly matcher.\n"
      "class SlotMatcherInterface {\n"
      " public:\n"
      "  virtual ~SlotMatcherInterface() = default;\n"
      "  virtual absl::StatusOr<std::tuple<uint64_t, int>> Encode(\n"
      "      uint64_t address, absl::string_view text, int entry,\n"
      "      ResolverInterface *resolver,\n"
      "      std::vector<RelocationInfo> &relocations) = 0;\n"
      "};\n\n");

  // Generate the regex matchers for each slot.
  for (auto* slot : slot_order_) {
    if (!slot->is_referenced()) continue;
    auto [h_slot, cc_slot] = slot->GenerateAsmRegexMatcher();
    absl::StrAppend(&h_output, h_slot);
    absl::StrAppend(&cc_output, cc_slot);
  }
  return {h_output, cc_output};
}

}  // namespace instruction_set
}  // namespace machine_description
}  // namespace sim
}  // namespace mpact
