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

#ifndef LMPACT_SIM_DECODER_BUNDLE_H_
#define LMPACT_SIM_DECODER_BUNDLE_H_

#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include "mpact/sim/decoder/opcode.h"
#include "mpact/sim/decoder/slot.h"
#include "absl/container/flat_hash_map.h"
#include "absl/strings/string_view.h"

namespace mpact {
namespace sim {
namespace machine_description {
namespace instruction_set {

class InstructionSet;

// A bundle refers to the instruction set grouping of one or more instructions
// or sub-bundles that are to be issued together. Bundle refers to type and
// layout of the grouping, not any particular instance thereof. A bundle
// consists of one or more (sub) bundles and/or slots. A slot corresponds to a
// single instruction issue slot.
class Bundle {
 public:
  // Constructor and destructor.
  Bundle(absl::string_view name, InstructionSet *instruction_set);
  virtual ~Bundle() = default;

  // Append a slot to the bundle. In case the slot has multiple instances,
  // a non-empty vector of instance numbers specify which slot instances are
  // part of this bundle.
  void AppendSlot(absl::string_view slot_name,
                  const std::vector<int> &instance_vec);
  // Append a sub bundle to this bundle.
  void AppendBundleName(absl::string_view bundle_name);
  // Return string for  bundle class declaration.
  std::string GenerateClassDeclaration(absl::string_view encoding_type) const;
  // Return string for bundle class definition.
  std::string GenerateClassDefinition(absl::string_view encoding_type) const;

  // Getters and setters.
  const std::string &name() const { return name_; }
  const std::string &pascal_name() const { return pascal_name_; }
  const std::vector<std::pair<std::string, const std::vector<int>>> &slot_uses()
      const {
    return slot_uses_;
  }
  const std::vector<std::string> &bundle_names() const { return bundle_names_; }
  InstructionSet *instruction_set() const { return instruction_set_; }
  bool is_marked() const { return is_marked_; }
  void set_is_marked(bool value) { is_marked_ = value; }

 private:
  // The is_marked flag is used to ensure bundle classes are only added once.
  bool is_marked_ = false;
  // Parent instruction set.
  InstructionSet *instruction_set_;
  std::string name_;
  // Name in PascalCase.
  std::string pascal_name_;
  // The slots contained within this bundle, including instance indices.
  std::vector<std::pair<std::string, const std::vector<int>>> slot_uses_;
  // The bundles contained within this bundle.
  std::vector<std::string> bundle_names_;
  // The PascalCase names of the bundles contained within this bundle.
  std::vector<std::string> bundle_pascal_names_;
};

}  // namespace instruction_set
}  // namespace machine_description
}  // namespace sim
}  // namespace mpact

#endif  // LMPACT_SIM_DECODER_BUNDLE_H_
