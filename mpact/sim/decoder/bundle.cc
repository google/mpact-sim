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

#include "mpact/sim/decoder/bundle.h"

#include <memory>
#include <string>
#include <vector>

#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "mpact/sim/decoder/format_name.h"
#include "mpact/sim/decoder/instruction_set.h"
#include "mpact/sim/decoder/instruction_set_contexts.h"

namespace mpact {
namespace sim {
namespace machine_description {
namespace instruction_set {

Bundle::Bundle(absl::string_view name, InstructionSet* instruction_set,
               BundleDeclCtx* ctx)
    : ctx_(ctx),
      instruction_set_(instruction_set),
      name_(name),
      pascal_name_(ToPascalCase(name)) {}

void Bundle::AppendBundleName(absl::string_view name) {
  // emplace_back accepts a string_view, push_back does not.
  bundle_names_.emplace_back(name);
}

void Bundle::AppendSlot(absl::string_view name,
                        const std::vector<int>& instance_vec) {
  slot_uses_.push_back({std::string(name), instance_vec});
}

// Returns a string containing the bundle class declaration (.h file).
std::string Bundle::GenerateClassDeclaration(
    absl::string_view encoding_type) const {
  std::string output;
  absl::StrAppend(&output, "class ", pascal_name(),
                  "Decoder {\n"
                  " public:\n"
                  "  explicit ",
                  pascal_name(),
                  "Decoder(ArchState *arch_state);\n"
                  "  virtual ~",
                  pascal_name(),
                  "Decoder() = default;\n"
                  "  virtual Instruction *Decode(uint64_t address, ",
                  encoding_type,
                  " *encoding);\n"
                  "  virtual SemFunc GetSemanticFunction() = 0;\n"
                  "\n");
  for (const auto& bundle_name : bundle_names()) {
    absl::StrAppend(&output, "  ", ToPascalCase(bundle_name), "Decoder *",
                    bundle_name, "_decoder() { return ", bundle_name,
                    "_decoder_.get(); }\n");
  }
  for (const auto& [slot_name, unused] : slot_uses()) {
    absl::StrAppend(&output, "  ", ToPascalCase(slot_name), "Slot *", slot_name,
                    "_decoder() { return ", slot_name, "_decoder_.get(); }\n");
  }
  absl::StrAppend(&output, " private:\n");
  for (const auto& bundle_name : bundle_names()) {
    absl::StrAppend(&output, "  std::unique_ptr<", ToPascalCase(bundle_name),
                    "Decoder> ", bundle_name, "_decoder_;\n");
  }
  for (const auto& [slot_name, unused] : slot_uses()) {
    absl::StrAppend(&output, "  std::unique_ptr<", ToPascalCase(slot_name),
                    "Slot> ", slot_name, "_decoder_;\n");
  }
  absl::StrAppend(&output,
                  "  ArchState *arch_state_;\n"
                  "};\n"
                  "\n");
  return output;
}

// Returns a string containing the bundle class definition (.cc file).
std::string Bundle::GenerateClassDefinition(
    absl::string_view encoding_type) const {
  std::string output;
  std::string class_name = pascal_name() + "Bundle";
  // Constructor.
  absl::StrAppend(&output, class_name, "::", class_name,
                  "(ArchState *arch_state) :\n"
                  "  arch_state_(arch_state)\n"
                  "{\n");

  for (const auto& bundle_name : bundle_names()) {
    absl::StrAppend(&output, "  ", bundle_name, "_decoder = std::make_unique<",
                    ToPascalCase(bundle_name), "Decoder>(arch_state_);\n");
  }
  for (const auto& [slot_name, unused] : slot_uses()) {
    absl::StrAppend(&output, "  ", slot_name, "_decoder = std::make_unique<",
                    ToPascalCase(slot_name), "Slot>(arch_state_);\n");
  }
  absl::StrAppend(&output, "}\n");
  // Decode.
  absl::StrAppend(
      &output, "Instruction *", class_name, "::Decode(uint64_t address, ",
      encoding_type,
      " *encoding) {\n"
      "  Instruction *inst = new Instruction(address, arch_state_);\n"
      "  Instruction *tmp_inst;\n");
  // Decoded bundles are added to the child list.
  for (const auto& bundle_name : bundle_names()) {
    absl::StrAppend(&output, "  tmp_inst = ", bundle_name,
                    "_decoder_->Decode(address, encoding);\n"
                    "  inst->AppendChild(tmp_inst);\n");
  }
  // Instructions for decoded slots are added to the "next" list.
  for (const auto& [slot_name, instance_vec] : slot_uses()) {
    if (instance_vec.empty()) {
      absl::StrAppend(&output, "  tmp_inst = ", slot_name,
                      "_decoder_->Decode(address, encoding, 0);\n"
                      "  inst->Append(tmp_inst);\n");
    } else {
      for (auto index : instance_vec) {
        absl::StrAppend(&output, "  tmp_inst = ", slot_name,
                        "_decoder_->Decode(address, encoding, ", index,
                        ");\n"
                        "  inst->Append(tmp_inst);\n");
      }
    }
  }
  // Set semantic function for this bundle.
  absl::StrAppend(
      &output,
      "  inst->set_semantic_function(this->GetSemanticFunction());\n"
      "}\n");
  return output;
}

}  // namespace instruction_set
}  // namespace machine_description
}  // namespace sim
}  // namespace mpact
