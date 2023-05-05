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

#ifndef MPACT_SIM_DECODER_INSTRUCTION_ENCODING_H_
#define MPACT_SIM_DECODER_INSTRUCTION_ENCODING_H_

#include <string>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "mpact/sim/decoder/format.h"
#include "mpact/sim/decoder/overlay.h"

namespace mpact {
namespace sim {
namespace decoder {
namespace bin_format {

enum class ConstraintType : int { kEq = 0, kNe, kLt, kLe, kGt, kGe };

// Helper struct to group the information of a constraint (either == or !=).
struct Constraint {
  ConstraintType type;
  Field *field = nullptr;
  Overlay *overlay = nullptr;
  bool can_ignore = false;
  uint64_t value;
};

// Class for an individual instruction encoding. The instruction encoding
// captures the constraints on field values in an instruction format that
// determines the encoding of a specific opcode/instruction. It computes the
// constant bit values and masks from these constraints
class InstructionEncoding {
 public:
  // Disable default constructor and assignment operator.
  InstructionEncoding() = delete;
  InstructionEncoding(std::string name, Format *format);
  InstructionEncoding(const InstructionEncoding &encoding);
  InstructionEncoding &operator=(const InstructionEncoding &) = delete;
  ~InstructionEncoding();

  // Add a constraint on a field/overlay (in the format associated with the
  // instruction) needing to be equal to a value.
  absl::Status AddEqualConstraint(std::string field_name, int64_t value);
  // Add a constraint on a field/overlay (in the format associated with the
  // instruction) needing a different comparison (ne, lt, le, etc.).
  absl::Status AddOtherConstraint(ConstraintType type, std::string field_name,
                                  int64_t value);

  // Get the value of the constant bits in the instruction (as defined by the
  // equal constraints).
  uint64_t GetValue();
  // Get the mask of the instruction word based on the bits that are specified
  // in the equal constraints.
  uint64_t GetMask();
  // Get the mask of the instruction word based on the bits in both equal and
  // not equal constraints.
  uint64_t GetCombinedMask();

  // Accessors.
  const std::string &name() const { return name_; }
  // Return the vector of constraints on the values of this encoding. These
  // constraints determine the value that a masked set of bits have to be equal
  // to in order to match this encoding.
  const std::vector<Constraint *> &equal_constraints() const {
    return equal_constraints_;
  }
  // Additionally, overlays may add constant bits to field references. These
  // constraints have to be compared one by one after performing an overlay
  // extraction that adds in the bits as specified in the overlay. Thus, they
  // cannot be used in a simple mask and compare.
  const std::vector<Constraint *> &equal_extracted_constraints() const {
    return equal_extracted_constraints_;
  }
  // The vector of not-equal, greater, less, etc., constraints that have to be
  // satisfied for an instruction to match this encoding.
  const std::vector<Constraint *> &other_constraints() const {
    return other_constraints_;
  }

 private:
  // Internal helper to create and check a constraint.
  absl::StatusOr<Constraint *> CreateConstraint(ConstraintType type,
                                                std::string field_name,
                                                int64_t value);
  // Recomputes the masks and values.
  absl::Status ComputeMaskAndValue();

  std::string name_;
  std::string format_name_;
  Format *format_ = nullptr;
  std::vector<Constraint *> equal_constraints_;
  std::vector<Constraint *> equal_extracted_constraints_;
  std::vector<Constraint *> other_constraints_;
  bool mask_set_ = false;
  uint64_t mask_ = 0;
  uint64_t other_mask_ = 0;
  uint64_t extracted_mask_ = 0;
  uint64_t value_ = 0;
};

}  // namespace bin_format
}  // namespace decoder
}  // namespace sim
}  // namespace mpact

#endif  // MPACT_SIM_DECODER_INSTRUCTION_ENCODING_H_
