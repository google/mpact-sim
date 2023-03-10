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

#include "mpact/sim/decoder/slot.h"

#include <map>
#include <stack>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "absl/container/flat_hash_set.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "mpact/sim/decoder/format_name.h"
#include "mpact/sim/decoder/instruction.h"
#include "mpact/sim/decoder/instruction_set.h"
#include "mpact/sim/decoder/resource.h"
#include "mpact/sim/decoder/template_expression.h"

namespace mpact {
namespace sim {
namespace machine_description {
namespace instruction_set {

// This function translates the location specification into a set of '->'
// references starting with 'inst->' to get to the operand that is implied.
static absl::StatusOr<std::string> TranslateLocator(
    const OperandLocator &locator) {
  std::string code;
  absl::StrAppend(&code, "inst->");
  if (locator.op_spec_number > 0) {
    absl::StrAppend(&code, "child()->");
  }
  for (int i = 1; i < locator.op_spec_number; i++) {
    absl::StrAppend(&code, "next()->");
  }
  if (locator.type == 'p') {
    absl::StrAppend(&code, "Predicate()");
  } else if (locator.type == 's') {
    absl::StrAppend(&code, "Source(", locator.instance, ")");
  } else if (locator.type == 'd') {
    absl::StrAppend(&code, "Destination(", locator.instance, ")");
  } else {
    return absl::InternalError(absl::StrCat("Unknown locator type '",
                                            std::string(locator.type, 1), "'"));
  }
  return code;
}

// Small helper function to just expand the expression specified by the
// FormatInfo from parsing the disassembly specifier.
static std::string ExpandExpression(const FormatInfo &format,
                                    const std::string &locator) {
  // Handle the case when it's just an '@' - i.e., just the address.
  if (format.use_address && format.operation.empty()) {
    return absl::StrCat("(inst->address())");
  } else if (format.operation.empty()) {
    // No +/- for the @ sign, i.e., @ <</>> amount.
    if (locator.empty()) return absl::StrCat("#error missing field locator");
    return absl::StrCat("(", locator, "->AsInt64(0)",
                        format.do_left_shift ? " << " : " >> ",
                        format.shift_amount, ")");
  } else {
    // (@ +/- operand) <</>> shift amount
    if (locator.empty()) return absl::StrCat("#error missing field locator");
    return absl::StrCat("(", format.use_address ? "inst->address() " : "0 ",
                        format.operation, "(", locator, "->AsInt64(0)",
                        format.do_left_shift ? " << " : " >> ",
                        format.shift_amount, "))");
  }
}

Slot::Slot(absl::string_view name, InstructionSet *instruction_set,
           bool is_templated)
    : instruction_set_(instruction_set),
      is_templated_(is_templated),
      name_(name),
      pascal_name_(ToPascalCase(name)) {}

Slot::~Slot() {
  delete default_latency_;
  default_latency_ = nullptr;
  delete default_instruction_;
  default_instruction_ = nullptr;
  for (auto *param : template_parameters_) {
    delete param;
  }
  template_parameters_.clear();
  for (auto &base : base_slots_) {
    if (base.arguments != nullptr) {
      for (auto *expr : *(base.arguments)) {
        delete expr;
      }
      delete base.arguments;
    }
  }
  base_slots_.clear();
  for (auto &[unused, inst_ptr] : instruction_map_) {
    delete inst_ptr;
  }
  instruction_map_.clear();
  for (auto &[unused, element_ptr] : constant_map_) {
    delete element_ptr;
  }
  constant_map_.clear();
  // The ctx objects stored in resource_spec_map_ are owned by the Antlr4
  // parser, so just clear the map (don't delete those objects).
  resource_spec_map_.clear();
  for (auto &[ignored, expr] : attribute_map_) {
    delete expr;
  }
  attribute_map_.clear();
}

absl::Status Slot::AppendInstruction(Instruction *inst) {
  if (!is_templated()) {
    bool valid = inst->opcode()->ValidateDestLatencies(
        [](int l) -> bool { return l >= 0; });
    if (!valid) {
      return absl::InternalError(absl::StrCat("Invalid latency for opcode '",
                                              inst->opcode()->name(), "'"));
    }
  }
  std::string name = inst->opcode()->name();
  if (!instruction_map_.contains(name)) {
    instruction_map_.emplace(name, inst);
    return absl::OkStatus();
  }
  return absl::InternalError(absl::StrCat(
      "Opcode '", name, "' already added to slot '", this->name(), "'"));
}

absl::Status Slot::AppendInheritedInstruction(Instruction *inst,
                                              TemplateInstantiationArgs *args) {
  std::string name = inst->opcode()->name();
  if (!instruction_map_.contains(name)) {
    auto derived = inst->CreateDerivedInstruction(args);

    if (derived.ok()) {
      if (!is_templated()) {
        bool valid = derived.value()->opcode()->ValidateDestLatencies(
            [](int l) -> bool { return l >= 0; });
        if (!valid) {
          return absl::InternalError(absl::StrCat(
              "Invalid latency for opcode '", inst->opcode()->name(), "'"));
        }
      }
      instruction_map_.emplace(name, derived.value());
      return absl::OkStatus();
    }
    return derived.status();
  }
  return absl::InternalError(
      absl::StrCat("instruction already added: ", inst->opcode()->name()));
}

bool Slot::HasInstruction(const std::string &opcode_name) const {
  return instruction_map_.contains(opcode_name);
}

static std::string indent_string(int n) { return std::string(n, ' '); }

std::string Slot::GenerateAttributeSetter(const Instruction *inst) const {
  if (InstructionSet::attribute_names() == nullptr) {
    return "  info->attribute_setter = [](Instruction *inst) {};\n";
  }
  std::string output;
  absl::StrAppend(&output,
                  "  info->attribute_setter = [](Instruction *inst) {\n");

  // Allocate the array and initialize to zero.
  absl::StrAppend(
      &output,
      "    int size = static_cast<int>(AttributeEnum::kPastMaxValue);\n"
      "    int *attrs = new int[size];\n");
  for (auto const &[name, expr] : inst->attribute_map()) {
    auto result = expr->GetValue();
    if (!result.ok()) {
      absl::StrAppend(&output, "    #error Expression for '", name,
                      "' has no constant value\n");
      continue;
    }
    int *value = std::get_if<int>(&result.value());
    if (value == nullptr) {
      absl::StrAppend(&output, "    #error Expression for '", name,
                      "' does not have type int\n");
      continue;
    }
    absl::StrAppend(&output, "    attrs[static_cast<int>(AttributeEnum::k",
                    ToPascalCase(name), ")] = ", *value, ";\n");
  }
  absl::StrAppend(&output,
                  "    inst->SetAttributes(absl::Span<int>(attrs, size));\n"
                  "  };\n\n");
  return output;
}

std::string Slot::GenerateDisassemblySetter(const Instruction *inst) const {
  std::string output;
  if (inst->disasm_format_vec().empty()) {
    return "  info->disassembly_setter = [](Instruction *) {};\n";
  }
  absl::StrAppend(&output,
                  "  info->disassembly_setter = [](Instruction *inst) {\n"
                  "    inst->SetDisassemblyString(");
  int indent = 6;
  int outer_paren = false;
  // This is used to keep track of whether the current code emitted is in
  // a call to strcat or not. It helps reduce the number of strcat calls made
  // in the generated code.
  std::stack<bool> in_strcat;
  absl::StrAppend(&output, "absl::StrCat(\n");
  in_strcat.push(true);
  outer_paren = true;
  std::string outer_sep;
  for (auto const *disasm_fmt : inst->disasm_format_vec()) {
    int inner_paren = 0;
    size_t index = 0;
    std::string inner_sep;
    // If the next string needs to be formatted within a certain width field,
    // start out with a StrFormat call.
    if (disasm_fmt->width != 0) {
      absl::StrAppend(&output, outer_sep, indent_string(indent),
                      "absl::StrFormat(\"%", disasm_fmt->width, "s\",\n");
      indent += 2;
      inner_paren++;
      in_strcat.push(false);
    } else if (!outer_sep.empty()) {
      absl::StrAppend(&output, ", ");
    }
    // If multiple strings will be generated, and we're not currently in a
    // StrCat, start a StrCat.
    if (!((disasm_fmt->format_fragment_vec.size() == 1) &&
          disasm_fmt->format_info_vec.empty()) &&
        !in_strcat.top()) {
      absl::StrAppend(&output, indent_string(indent), "absl::StrCat(\n");
      indent += 2;
      inner_paren++;
      in_strcat.push(true);
    }
    // Generate the strings from the format fragments and the format info.
    for (auto const &frag : disasm_fmt->format_fragment_vec) {
      std::string next_sep;
      if (!frag.empty()) {
        absl::StrAppend(&output, inner_sep, indent_string(indent), "\"", frag,
                        "\"");
        next_sep = ", ";
      }
      if (index < disasm_fmt->format_info_vec.size()) {
        auto *format_info = disasm_fmt->format_info_vec[index];
        if (format_info->op_name.empty()) {
          if (!format_info->is_formatted) {
            absl::StrAppend(&output, "\n#error Missing locator information");
          } else {
            absl::StrAppend(&output, next_sep, "absl::StrFormat(\"",
                            format_info->number_format, "\", ",
                            ExpandExpression(*format_info, ""), ")");
          }
        } else {
          auto key = format_info->op_name;
          auto iter = inst->opcode()->op_locator_map().find(key);
          if (iter == inst->opcode()->op_locator_map().end()) {
            absl::StrAppend(&output, "\n#error ", key,
                            " not found in instruction opcodes\n");
            continue;
          }
          auto result = TranslateLocator(iter->second);
          if (!result.status().ok()) {
            absl::StrAppend(&output, "\n#error ", result.status().message(),
                            "\n");
            continue;
          }
          if (!format_info->is_formatted) {
            absl::StrAppend(&output, next_sep, result.value(), "->AsString()");
          } else {
            absl::StrAppend(&output, next_sep, "absl::StrFormat(\"",
                            format_info->number_format, "\", ",
                            ExpandExpression(*format_info, result.value()),
                            ")");
          }
        }
      }
      index++;
      if (inner_sep.empty()) inner_sep = ",\n";
    }
    // Close up parentheses as required.
    for (int i = 0; i < inner_paren; i++) {
      absl::StrAppend(&output, ")");
      indent -= 2;
      if (!in_strcat.top()) {  // Finished a StrFormat.
        absl::StrAppend(&output, "\n", indent_string(indent));
      }
      in_strcat.pop();
    }
    if (outer_sep.empty()) outer_sep = ",\n";
  }
  if (outer_paren) {
    in_strcat.pop();
    absl::StrAppend(&output, ")");
  }

  absl::StrAppend(&output,
                  ");\n"
                  "  };\n\n");
  return output;
}

std::string Slot::GenerateResourceSetter(
    const Instruction *inst, absl::string_view encoding_type) const {
  std::string output;
  std::string opcode_name = inst->opcode()->pascal_name();
  std::string opcode_enum = absl::StrCat("OpcodeEnum::k", opcode_name);
  std::string signature = absl::StrCat("(Instruction *inst, ", encoding_type,
                                       " *enc, SlotEnum slot, int entry)");
  absl::StrAppend(&output, "  info->resource_setter = []", signature, " {\n");
  if (!inst->resource_use_vec().empty() ||
      !inst->resource_acquire_vec().empty()) {
    absl::StrAppend(&output, "    ResourceOperandInterface *res_op;\n");
  }
  // Get all the simple resources that need to be free, then all the complex
  // resources that need to be free in order to issue the instruction.
  std::vector<const ResourceReference *> complex_refs;
  std::vector<const ResourceReference *> simple_refs;
  for (auto const *ref : inst->resource_use_vec()) {
    // Do the complex refs last.
    if (!ref->resource->is_simple()) {
      complex_refs.push_back(ref);
    } else {
      simple_refs.push_back(ref);
    }
  }
  // Simple resources.
  if (!simple_refs.empty()) {
    // First gather the resource references into a single vector, then request
    // the resource operands for all the resource references in that vector.
    std::string sep = "";
    absl::StrAppend(&output,
                    "    std::vector<SimpleResourceEnum> hold_vec = {");
    for (auto const *simple : simple_refs) {
      absl::StrAppend(&output, sep, "\n        SimpleResourceEnum::k",
                      simple->resource->pascal_name(), ", ");
    }
    absl::StrAppend(&output,
                    "};\n\n"
                    "    res_op = enc->GetSimpleResourceOperand(slot, entry, ",
                    opcode_enum, ", hold_vec, -1);\n",
                    "    if (res_op != nullptr) {\n"
                    "      inst->AppendResourceHold(res_op);\n"
                    "    }\n");
  }
  // Complex resources.
  if (!complex_refs.empty()) {
    for (auto const *complex : complex_refs) {
      // Get the expression values for the begin and end expressions.
      auto begin_value = complex->begin_expression->GetValue();
      auto end_value = complex->end_expression->GetValue();
      if (!begin_value.ok() || !end_value.ok()) {
        absl::StrAppend(&output,
                        "#error Unable to evaluate begin or end expression\n");
        continue;
      }
      // Get the integer values from the begin and end expression values.
      int *begin = std::get_if<int>(&begin_value.value());
      int *end = std::get_if<int>(&end_value.value());
      if ((begin == nullptr) || (end == nullptr)) {
        absl::StrAppend(
            &output, "#error Unable to get value of begin or end expression\n");
        continue;
      }
      absl::StrAppend(
          &output, "    res_op = enc->GetComplexResourceOperand(slot, entry, ",
          opcode_enum, ", ComplexResourceEnum::k",
          complex->resource->pascal_name(), ", ");
      absl::StrAppend(&output, *begin, ", ", *end, ");\n");
      absl::StrAppend(&output,
                      "    if (res_op != nullptr) {\n"
                      "      inst->AppendResourceHold(res_op);\n"
                      "    }\n");
    }
  }

  // Get all the simple resources that need to be reserved, then all the complex
  // resources that need to be reserved when issuing this instruction.
  complex_refs.clear();
  simple_refs.clear();
  for (auto const *ref : inst->resource_acquire_vec()) {
    // Do the complex refs last.
    if (!ref->resource->is_simple()) {
      complex_refs.push_back(ref);
    } else {
      simple_refs.push_back(ref);
    }
  }
  // Simple resources.
  if (!simple_refs.empty()) {
    // Compute the set of latencies. Insert each reference into a multi-map
    // keyed by the latency.
    std::multimap<int, const ResourceReference *> latency_map;
    absl::flat_hash_set<int> latencies;
    for (auto const *simple : simple_refs) {
      if (simple->end_expression == nullptr) {
        continue;
      }
      auto end_value = simple->end_expression->GetValue();
      if (!end_value.ok()) {
        absl::StrAppend(&output, "#error Unable to evaluate end expression\n");
        continue;
      }
      int *end = std::get_if<int>(&end_value.value());
      if (end == nullptr) {
        absl::StrAppend(&output,
                        "#error Unable to get value of  end expression\n");
        continue;
      }
      int latency = *end;
      latencies.insert(latency);
      latency_map.insert(std::make_pair(latency, simple));
    }
    // Process the resources by latencies.
    for (auto latency : latencies) {
      std::string sep = "";
      absl::StrAppend(&output,
                      "    std::vector<SimpleResourceEnum> acquire_vec",
                      latency, " = {");
      for (auto iter = latency_map.lower_bound(latency);
           iter != latency_map.upper_bound(latency); ++iter) {
        auto *simple = iter->second;
        absl::StrAppend(&output, sep, "\n        SimpleResourceEnum::k",
                        simple->resource->pascal_name(), ",");
      }
      absl::StrAppend(
          &output,
          "};\n\n"
          "    res_op = enc->GetSimpleResourceOperand(slot, entry, ",
          opcode_enum, ", acquire_vec", latency, ", ", latency,
          ");\n"
          "    if (res_op != nullptr) {\n"
          "      inst->AppendResourceAcquire(res_op);\n"
          "    }\n");
    }
  }

  // Complex resources.
  if (!complex_refs.empty()) {
    for (auto const *complex : complex_refs) {
      // Get the expression values for the begin and end expressions.
      if (complex->begin_expression == nullptr) continue;
      if (complex->end_expression == nullptr) continue;
      auto begin_value = complex->begin_expression->GetValue();
      auto end_value = complex->end_expression->GetValue();
      if (!begin_value.ok() || !end_value.ok()) {
        absl::StrAppend(&output,
                        "#error Unable to evaluate begin or end expression\n");
        continue;
      }
      // Get the integer values from the begin and end expression values.
      int *begin = std::get_if<int>(&begin_value.value());
      int *end = std::get_if<int>(&end_value.value());
      absl::StrAppend(
          &output,
          "    res_op = enc->GetComplexResourceOperand(ComplexResourceEnum::k",
          complex->resource->pascal_name(), ", ResourceArgumentEnum::k");
      absl::StrAppend(&output, "None, slot, entry, ", *begin, ", ", *end,
                      ");\n");
      absl::StrAppend(&output,
                      "    if (res != nullptr) {\n"
                      "      inst->AppendResourceAcquire(res_op);\n"
                      "    }\n");
    }
  }
  absl::StrAppend(&output, "  };\n\n");
  return output;
}

std::string Slot::ListFuncGetterInitializations(
    absl::string_view encoding_type) const {
  std::string output;
  if (instruction_map_.empty()) return output;
  std::string class_name = pascal_name() + "Slot";
  // For each instruction create two lambda functions. One that is used to
  // obtain the semantic function object for the instruction, the other a lambda
  // that sets the predicate, source and target operands. Both lambdas use calls
  // to virtual functions declared in the current class or a base class thereof.
  std::string signature = absl::StrCat("(Instruction *inst, ", encoding_type,
                                       " *enc, SlotEnum slot, int entry)");
  absl::StrAppend(&output,
                  "  int index;\n"
                  "  InstructionInfo *info;\n"
                  "  // For kNone - unknown instruction.\n"
                  "  index = static_cast<int>(OpcodeEnum::kNone);\n"
                  "  info = new InstructionInfo;\n"
                  "  info->instruction_size = ",
                  min_instruction_size(),
                  ";\n\n"
                  "  info->operand_setter.push_back([]",
                  signature,
                  "{});\n"
                  "  info->semfunc.push_back(",
                  default_instruction_->semfunc_code_string(), ");\n",
                  GenerateResourceSetter(default_instruction_, encoding_type),
                  GenerateDisassemblySetter(default_instruction_),
                  GenerateAttributeSetter(default_instruction_),
                  "  instruction_info_.insert({index, info});\n");
  for (auto const &[unused, inst_ptr] : instruction_map_) {
    auto *instruction = inst_ptr;
    std::string opcode_name = instruction->opcode()->pascal_name();
    std::string opcode_enum = absl::StrCat("OpcodeEnum::k", opcode_name);

    absl::StrAppend(&output, "\n  // ***   ", opcode_name, "   ***\n",
                    "  index = static_cast<int>(", opcode_enum,
                    ");\n"
                    "  info = new InstructionInfo;\n"
                    "  info->instruction_size = ",
                    instruction->opcode()->instruction_size(), ";\n");
    // For the opcode and any child opcodes, add the semantic function and
    // operand_setter_ lambda.
    for (auto const *inst = instruction; inst != nullptr;
         inst = inst->child()) {
      std::string code_str;
      if (inst->semfunc_code_string().empty()) {
        // If there is no code string, use the default one.
        code_str = default_instruction_->semfunc_code_string();
      } else {
        code_str = inst->semfunc_code_string();
      }
      absl::StrAppend(&output, "  info->semfunc.push_back(", code_str,
                      ");\n"
                      "  info->operand_setter.push_back([]",
                      signature, " {\n");
      // Generate code to set predicate operand, if the opcode has one.
      const std::string &op_name = inst->opcode()->predicate_op_name();
      if (!op_name.empty()) {
        std::string pred_op_enum =
            absl::StrCat("PredOpEnum::k", ToPascalCase(op_name));
        absl::StrAppend(&output, "        inst->SetPredicate(enc->GetPredicate",
                        "(slot_, entry, ", opcode_enum, ", ", pred_op_enum,
                        "));\n");
      }
      // Generate code to set the instruction's source operands.
      int source_no = 0;
      for (const auto &src_name : inst->opcode()->source_op_name_vec()) {
        std::string src_op_enum =
            absl::StrCat("SourceOpEnum::k", ToPascalCase(src_name));
        absl::StrAppend(&output, "        inst->AppendSource(enc->GetSource",
                        "(slot_, entry, ", opcode_enum, ", ", src_op_enum, ", ",
                        source_no++, "));\n");
      }
      // Generate code to set the instruction's destination operands.
      int dest_no = 0;
      for (auto const *dst_op : inst->opcode()->dest_op_vec()) {
        std::string dest_op_enum =
            absl::StrCat("DestOpEnum::k", dst_op->pascal_case_name());
        if (dst_op->expression() == nullptr) {
          absl::StrAppend(
              &output, "        inst->AppendDestination(enc->GetDestination(",
              "slot_, entry, ", opcode_enum, ", ", dest_op_enum, ", ", dest_no,
              ", enc->GetLatency(slot_, entry, ", opcode_enum, ", ",
              dest_op_enum, " , ", dest_no, ")));\n");
          dest_no++;
          continue;
        }
        auto result = dst_op->GetLatency();
        if (!result.ok()) {
          absl::StrAppend(&output,
                          "#error \"Failed to get latency for operand '",
                          dst_op->name(), "'\"");
          dest_no++;
          continue;
        }
        absl::StrAppend(&output,
                        "        inst->AppendDestination(enc->GetDestination",
                        "(slot_, entry, ", opcode_enum, ", ", dest_op_enum,
                        ", ", dest_no, ", ", result.value(), "));\n");
        dest_no++;
      }
      absl::StrAppend(&output, "      });\n\n");
    }
    absl::StrAppend(&output, GenerateDisassemblySetter(instruction));
    absl::StrAppend(&output,
                    GenerateResourceSetter(instruction, encoding_type));
    absl::StrAppend(&output, GenerateAttributeSetter(instruction));
    absl::StrAppend(&output, "  instruction_info_.insert({index, info});\n");
  }
  return output;
}

// Generate the class declaration, including all getter functions for semantic
// functions and operand initializers.
std::string Slot::GenerateClassDeclaration(
    absl::string_view encoding_type) const {
  std::string output;
  if (!is_referenced()) return output;
  std::string class_name = pascal_name() + "Slot";
  absl::StrAppend(&output, "class ", class_name,
                  " {\n"
                  " public:\n"
                  "  explicit ",
                  class_name,
                  "(ArchState *arch_state);\n"
                  "  virtual ~",
                  class_name, "();\n");
  // Emit Decode function generated that decodes the slot and creates and
  // initializes an instruction object, as well as private data members.
  absl::StrAppend(&output, "  Instruction *Decode(uint64_t address, ",
                  encoding_type, "* isa_encoding, SlotEnum, int entry);\n",
                  "\n"
                  " private:\n"
                  "  ArchState *arch_state_;\n"
                  "  InstructionInfoMap instruction_info_;\n",
                  "  static constexpr SlotEnum slot_ = SlotEnum::k",
                  pascal_name(),
                  ";\n"
                  "};\n"
                  "\n");
  return output;
}

// Write out the class definition for the Slot class including all initializer
// bodies.
std::string Slot::GenerateClassDefinition(
    absl::string_view encoding_type) const {
  if (!is_referenced()) return "";
  std::string class_name = pascal_name() + "Slot";
  std::string output;
  // Constructor.
  absl::StrAppend(
      &output, class_name, "::", class_name,
      "(ArchState *arch_state) :\n"
      "  arch_state_(arch_state)\n"
      "{\n",
      ListFuncGetterInitializations(encoding_type),
      "}\n"
      "\n"
      "Instruction *",
      class_name, "::Decode(uint64_t address, ", encoding_type,
      " *isa_encoding, SlotEnum slot, int entry) {\n"
      "  OpcodeEnum opcode = isa_encoding->GetOpcode(slot, entry);\n"
      "  int indx = static_cast<int>(opcode);\n"
      "  if (!instruction_info_.contains(indx)) indx = 0;\n"
      "  auto *inst_info = instruction_info_[indx];\n"
      "  Instruction *inst = new Instruction(address, arch_state_);\n"
      "  inst->set_size(inst_info->instruction_size);\n"
      "  inst->set_opcode(static_cast<int>(opcode));\n"
      "  inst->set_semantic_function(inst_info->semfunc[0]);\n"
      "  inst_info->operand_setter[0](inst, isa_encoding, slot, entry);\n"
      "  Instruction *parent = inst;\n"
      "  for (size_t i = 1; i < inst_info->operand_setter.size(); i++) {\n"
      "    Instruction *child = new Instruction(address, arch_state_);\n"
      "    child->set_semantic_function(inst_info->semfunc[i]);\n"
      "    inst_info->operand_setter[i](child, isa_encoding, slot, entry);\n"
      "    parent->AppendChild(child);\n"
      "    child->DecRef();\n"
      "    parent = child;\n"
      "  }\n"
      "  inst_info->resource_setter(inst, isa_encoding, slot, entry);\n"
      "  inst_info->disassembly_setter(inst);\n"
      "  inst_info->attribute_setter(inst);\n"
      "  return inst;\n"
      "}\n");
  // Destructor.
  absl::StrAppend(&output, class_name, "::~", class_name,
                  "() {\n"
                  "  for (auto &[unused, info_ptr] : instruction_info_) {\n"
                  "    delete info_ptr;\n"
                  "  };\n"
                  "  instruction_info_.clear();\n"
                  "}\n");
  return output;
}

absl::Status Slot::CheckPredecessors(const Slot *base) const {
  if (predecessor_set_.contains(base))
    return absl::AlreadyExistsError(
        absl::StrCat("'", base->name(),
                     "' is already in the predecessor set of '", name(), "'"));
  for (auto const *pred : predecessor_set_) {
    auto status = pred->CheckPredecessors(base);
    if (!status.ok()) return status;
  }
  for (auto const *base_pred : base->predecessor_set_) {
    auto status = CheckPredecessors(base_pred);
    if (!status.ok()) return status;
  }
  return absl::OkStatus();
}

absl::Status Slot::AddBase(const Slot *base) {
  // First need to check if the current slot already inherits from base, or
  // any of base's predecessors. Only tree-like inheritance is supported.
  auto status = CheckPredecessors(base);
  if (!status.ok()) return status;
  predecessor_set_.insert(base);
  base_slots_.emplace_back(base);
  return absl::OkStatus();
}

absl::Status Slot::AddBase(const Slot *base,
                           TemplateInstantiationArgs *arguments) {
  auto status = CheckPredecessors(base);
  if (!status.ok()) return status;
  predecessor_set_.insert(base);
  base_slots_.emplace_back(base, arguments);
  return absl::OkStatus();
}

absl::Status Slot::AddConstant(const std::string &ident,
                               const std::string &type,
                               TemplateExpression *expression) {
  // Ignore the type for now - there is only int.
  (void)type;
  // Check if the name already exists or matches a template formal parameter.
  if (template_parameter_map_.contains(ident)) {
    return absl::AlreadyExistsError(
        absl::StrCat("Slot constant '", ident,
                     "' conflicts with template formal with same name"));
  }
  if (constant_map_.contains(ident)) {
    return absl::AlreadyExistsError(
        absl::StrCat("Redefinition of slot constant '", ident, "'"));
  }
  constant_map_.insert(std::make_pair(ident, expression));
  return absl::OkStatus();
}

TemplateExpression *Slot::GetConstExpression(const std::string &ident) const {
  auto iter = constant_map_.find(ident);
  if (iter == constant_map_.end()) return nullptr;
  return iter->second;
}

absl::Status Slot::AddTemplateFormal(const std::string &par_name) {
  if (template_parameter_map_.contains(par_name)) {
    return absl::InternalError(
        absl::StrCat("Duplicate parameter name '", par_name, "'"));
  }
  int indx = template_parameters_.size();
  template_parameters_.push_back(new TemplateFormal(par_name, indx));
  template_parameter_map_.insert(std::make_pair(par_name, indx));
  return absl::OkStatus();
}

TemplateFormal *Slot::GetTemplateFormal(const std::string &name) const {
  auto iter = template_parameter_map_.find(name);
  if (iter == template_parameter_map_.end()) return nullptr;
  return template_parameters_[iter->second];
}

void Slot::AddInstructionAttribute(const std::string &name,
                                   TemplateExpression *expr) {
  attribute_map_.emplace(name, expr);
}

}  // namespace instruction_set
}  // namespace machine_description
}  // namespace sim
}  // namespace mpact
