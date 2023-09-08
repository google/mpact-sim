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

#ifndef MPACT_SIM_DECODER_ENCODING_GROUP_H_
#define MPACT_SIM_DECODER_ENCODING_GROUP_H_

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "absl/container/flat_hash_set.h"
#include "absl/strings/string_view.h"
#include "mpact/sim/decoder/extract.h"

namespace mpact {
namespace sim {
namespace decoder {
namespace bin_format {

class InstructionGroup;
class InstructionEncoding;
struct Constraint;

// The encoding group is a class that allows instruction encodings to be grouped
// together to facilitate breaking the instruction encodings into a tree like
// hierarchy of instructions that have overlapping bits in the encoding that
// can be used to differentiate the encodings.
class EncodingGroup {
 public:
  EncodingGroup(InstructionGroup *inst_group, uint64_t ignore);
  EncodingGroup(EncodingGroup *parent, InstructionGroup *inst_group,
                uint64_t ignore);
  ~EncodingGroup();
  // Remove bits from a mask that are already handled by parent encoding group.
  void AdjustMask();

  void AddEncoding(InstructionEncoding *enc);
  // True if the encoding can be added to the group (i.e., there are
  // sufficiently many "overlapping" bits.
  bool CanAddEncoding(InstructionEncoding *enc);
  // True if the encodings can be decoded using a simple lookup table without
  // any further comparisons.
  bool IsSimpleDecode();
  // Break the current group into subgroups recursively.
  void AddSubGroups();
  // Verify that there are no collisions of opcodes in the simple decoders.
  void CheckEncodings() const;
  // Emit code for the initializers (decode tables) and the decoder functions.
  void EmitInitializers(absl::string_view name, std::string *initializers_ptr,
                        const std::string &opcode_enum) const;
  void EmitDecoders(absl::string_view name, std::string *declarations_ptr,
                    std::string *definitions_ptr,
                    const std::string &opcode_enum) const;
  // Return a string containing information about the current group. This will
  // be removed at a later stage.
  // TODO(torerik): remove when no longer needed.
  std::string DumpGroup(std::string prefix, std::string indent);

  // Accessors.
  // Return the parent encoding group if it exists.
  EncodingGroup *parent() const { return parent_; }
  // The mask is the intersection of the bits that are significant to the
  // instruction encodings in this group. The bits that are used to select this
  // group from the parent are removed from the mask.
  uint64_t mask() const { return mask_; }
  // The value of the group is the value of the bits in the instruction
  // encodings that are the same across the encodings in the group.
  uint64_t value() const { return value_; }
  // The varying bits is the subset of the bits in the mask that varies in value
  // across the encodings in this group.
  uint64_t varying() const { return varying_; }
  // The constant bits is the subset of the bits in the mask that are constant
  // across the encodings in this group.
  uint64_t constant() const { return constant_; }
  // The discriminator is non-zero if this encoding group has subgroups. The
  // discriminator is the set of bits used to compute the value from the
  // instruction encoding to determine which sub-group a particular instruction
  // encoding in this group belongs to,
  uint64_t discriminator() const { return discriminator_; }
  // Simple decoding is true if an opcode can be determined solely based on the
  // discriminator, and no further constraints exist (such as a field having to
  // be non-zero).
  bool simple_decoding() const { return simple_decoding_; }
  // The vector of encodings in this group.
  const std::vector<InstructionEncoding *> &encoding_vec() const {
    return encoding_vec_;
  }
  // The vector of subgroups in this group.
  std::vector<EncodingGroup *> &encoding_group_vec() {
    return encoding_group_vec_;
  }

 private:
  // Methods that factors out some of the complexities of the public code
  // emitting methods.
  void EmitComplexDecoderBody(std::string *definitions_ptr,
                              absl::string_view index_extraction,
                              absl::string_view opcode_enum) const;
  void EmitComplexDecoderBodyIfSequence(std::string *definitions_ptr,
                                        absl::string_view opcode_enum) const;
  int EmitEncodingIfStatement(int indent, const InstructionEncoding *encoding,
                              absl::string_view opcode_enum,
                              absl::flat_hash_set<std::string> &extracted,
                              std::string *definitions_ptr) const;
  void ProcessConstraint(const absl::flat_hash_set<std::string> &extracted,
                         Constraint *constraint,
                         std::string *definitions_ptr) const;
  void EmitExtractions(int indent, const std::vector<Constraint *> &constraints,
                       absl::flat_hash_set<std::string> &extracted,
                       std::string *definitions_ptr) const;
  int EmitConstraintConditions(const std::vector<Constraint *> &constraints,
                               absl::string_view comparison,
                               std::string &connector,
                               std::string *condition) const;
  InstructionGroup *inst_group_ = nullptr;
  EncodingGroup *parent_ = nullptr;
  uint64_t varying_ = 0;
  uint64_t discriminator_ = 0;
  size_t discriminator_size_ = 0;
  ExtractionRecipe discriminator_recipe_;
  uint64_t constant_;
  uint64_t mask_ = 0;
  uint64_t value_ = 0;
  uint64_t last_value_ = 0;
  uint64_t ignore_ = 0;
  bool simple_decoding_ = false;
  std::string inst_word_type_;
  std::vector<InstructionEncoding *> encoding_vec_;
  std::vector<EncodingGroup *> encoding_group_vec_;
};

}  // namespace bin_format
}  // namespace decoder
}  // namespace sim
}  // namespace mpact

#endif  // MPACT_SIM_DECODER_ENCODING_GROUP_H_
