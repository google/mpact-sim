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

#include "mpact/sim/decoder/encoding_group.h"

#include <algorithm>
#include <string>
#include <vector>

#include "mpact/sim/decoder/extract.h"
#include "mpact/sim/decoder/format_name.h"
#include "mpact/sim/decoder/instruction_encoding.h"
#include "mpact/sim/decoder/instruction_group.h"
#include "absl/container/flat_hash_set.h"
#include "absl/functional/bind_front.h"
#include "absl/log/log.h"
#include "absl/numeric/bits.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"

namespace mpact {
namespace sim {
namespace decoder {
namespace bin_format {

using machine_description::instruction_set::ToPascalCase;

// Indexed by the members of the ConstraintType enum in instruction_encoding.h.
const char *kComparison[] = {
    /* kEq */ "==",
    /* kNe */ "!=",
    /* kLt */ "<",
    /* kLe */ "<=",
    /* kGt */ ">",
    /* kGe */ ">=",
};

// Helper function to provide a less than comparison of instruction encodings.
// This is used in call to std::sort.
static bool EncodingLess(uint64_t mask, InstructionEncoding *lhs,
                         InstructionEncoding *rhs) {
  uint64_t lhs_val = lhs->GetValue() & mask;
  uint64_t rhs_val = rhs->GetValue() & mask;
  return lhs_val < rhs_val;
}

EncodingGroup::EncodingGroup(InstructionGroup *inst_group, uint64_t ignore)
    : EncodingGroup(nullptr, inst_group, ignore) {}

EncodingGroup::EncodingGroup(EncodingGroup *parent,
                             InstructionGroup *inst_group, uint64_t ignore)
    : inst_group_(inst_group), parent_(parent), ignore_(ignore) {}

EncodingGroup::~EncodingGroup() {
  for (auto *group : encoding_group_vec_) {
    delete group;
  }
  encoding_group_vec_.clear();
}

// Adjust the masks and resort the encoding_vec.
void EncodingGroup::AdjustMask() {
  if (parent_ != nullptr) {
    uint64_t parent_mask = parent_->mask();
    constant_ &= ~parent_mask;
    mask_ &= ~parent_mask;
    varying_ &= ~parent_mask;
    value_ &= ~parent_mask;
  }
  std::sort(encoding_vec_.begin(), encoding_vec_.end(),
            absl::bind_front(&EncodingLess, mask_ & ~constant_ & ~ignore_));
}

// Adds an instruction encoding to the encoding group.
void EncodingGroup::AddEncoding(InstructionEncoding *enc) {
  if (encoding_vec_.empty()) {
    last_value_ = enc->GetValue();
    mask_ = enc->GetMask();
  }
  encoding_vec_.push_back(enc);
  mask_ &= enc->GetMask() & ~ignore_;
  uint64_t value = enc->GetValue();
  varying_ |= (value ^ last_value_);
  constant_ = ((~varying_) & mask_) & ~ignore_;
  last_value_ = value;
  value_ = encoding_vec_[0]->GetValue() & constant_;
}

// Returns true if there is overlap between the encoding and those already
// in the group. Returns false if it would clear the mask of common "fixed"
// bits.
bool EncodingGroup::CanAddEncoding(InstructionEncoding *enc) {
  if (encoding_vec_.empty()) return true;
  uint64_t new_mask = mask_ & enc->GetMask();
  if (new_mask == 0) return false;
  return true;
}

// Returns true if the decode can be done by simple opcode table lookup as
// opposed to a function that performs comparisons.
bool EncodingGroup::IsSimpleDecode() {
  if (!encoding_group_vec().empty()) return false;
  for (auto *enc : encoding_vec_) {
    if (!enc->other_constraints().empty()) return false;
    if (!enc->equal_extracted_constraints().empty()) return false;
  }
  return discriminator_ == varying_;
}

// This method takes the current group and seeks to break the encodings into
// subgroups based on the bits of the instructions that vary across the members
// of the group.
void EncodingGroup::AddSubGroups() {
  AdjustMask();
  discriminator_ = mask_ & ~constant_;
  simple_decoding_ = IsSimpleDecode();
  discriminator_recipe_ = GetExtractionRecipe(mask_ & ~constant_);
  discriminator_size_ =
      discriminator_ == 0 ? 0 : 1 << absl::popcount(discriminator_);
  unsigned shift =
      absl::bit_width(static_cast<unsigned>(inst_group_->width())) - 1;
  if (absl::popcount(static_cast<unsigned>(inst_group_->width())) > 1) {
    shift++;
  }
  shift = std::max(shift, 3U);
  if (shift > 6) {
    inst_word_type_ = "\n#error instruction word wider than 64 bits\n";
  } else {
    inst_word_type_ = absl::StrCat("uint", 1 << shift, "_t");
  }
  // Get the recipe for extracting only the varying bits that are part of the
  // mask into a compressed (i.e., bits are all adjacent) value.
  auto recipe = GetExtractionRecipe(discriminator_);
  // Iterate across the possible values of the compressed varying bits.
  for (int i = 0; i < (1 << absl::popcount(discriminator_)); i++) {
    // Create a new group for the current value 'i'.
    auto *encoding_group = new EncodingGroup(
        this, inst_group_, ignore_ | constant_ | discriminator_);
    for (auto *enc : encoding_vec_) {
      // Add all encodings that have value 'i' for the varying bits.
      if (ExtractValue(enc->GetValue(), recipe) != static_cast<unsigned>(i))
        continue;
      encoding_group->AddEncoding(enc);
    }
    // Avoid useless groups and infinite recursion by deleting any groups that
    // are empty and where the all the encodings ended up in the same subgroup.
    if (encoding_group->encoding_vec().empty()) {
      delete encoding_group;
      continue;
    }
    if (encoding_group->encoding_vec().size() == encoding_vec_.size()) {
      delete encoding_group;
      return;
    }
    // Remove the bits used to break up the current group from the mask of the
    // subgroup, as they have already been "used" on the path from the top to
    // the subgroup.
    encoding_group->AdjustMask();
    // If there are still bits that are part of the mask, try to recursively
    // break down the current group.
    if ((encoding_group->varying() | encoding_group->constant()) !=
        encoding_group->mask()) {
      simple_decoding_ = false;
      encoding_group->AddSubGroups();
      // But undo it if the max number of "varying" bits across the groups
      // is less than 2.
      int max = 0;
      for (auto *group : encoding_group->encoding_group_vec_) {
        max = std::max(max, absl::popcount(group->varying()));
      }
      if (max < 2) {
        for (auto *group : encoding_group->encoding_group_vec_) {
          delete group;
        }
        encoding_group->encoding_group_vec_.clear();
      }
    } else {
      encoding_group->discriminator_ =
          encoding_group->mask_ & ~encoding_group->constant_;
      encoding_group->discriminator_recipe_ = GetExtractionRecipe(
          encoding_group->mask_ & ~encoding_group->constant_);
      encoding_group->discriminator_size_ =
          1 << absl::popcount(encoding_group->discriminator_);
      encoding_group->simple_decoding_ = encoding_group->IsSimpleDecode();
      unsigned shift =
          absl::bit_width(static_cast<unsigned>(inst_group_->width())) - 1;
      if (absl::popcount(static_cast<unsigned>(inst_group_->width())) > 1) {
        shift++;
      }
      shift = std::max(shift, 3U);
      if (shift > 6) {
        encoding_group->inst_word_type_ =
            "\n#error instruction word wider than 64 bits\n";
      } else {
        encoding_group->inst_word_type_ =
            absl::StrCat("uint", 1 << shift, "_t");
      }
    }
    encoding_group_vec_.push_back(encoding_group);
  }
}

// Check for collisions of opcodes.
void EncodingGroup::CheckEncodings() const {
  if (encoding_group_vec_.empty() && simple_decoding_) {
    // The encodings are in order of discriminator value, so checking for
    // duplicates are easy.
    int prev_value = -1;
    InstructionEncoding *prev_enc = nullptr;
    for (auto *enc : encoding_vec_) {
      int value = ExtractValue(enc->GetValue(), discriminator_recipe_);
      if (value == prev_value) {
        inst_group_->encoding_info()->error_listener()->semanticError(
            nullptr, absl::StrCat("Duplicate encodings in instruction group ",
                                  inst_group_->name(), ": ", enc->name(),
                                  " and ", prev_enc->name()));
      }
      prev_enc = enc;
      prev_value = value;
    }
  }
  for (auto const *enc_grp : encoding_group_vec_) {
    enc_grp->CheckEncodings();
  }
}

// Emit the initializers of the decode function tables/opcode tables used
// by the decoding functions.
void EncodingGroup::EmitInitializers(absl::string_view name,
                                     std::string *initializers_ptr,
                                     const std::string &opcode_enum) const {
  if (discriminator_size_ == 0) return;
  absl::StrAppend(initializers_ptr, "constexpr int kParseGroup", name,
                  "_Size = ", discriminator_size_, ";\n\n");
  absl::StrAppend(initializers_ptr, "absl::AnyInvocable<", opcode_enum, "(",
                  inst_word_type_,
                  ")>"
                  " parse_group_",
                  name, "[kParseGroup", name, "_Size] = {\n");
  size_t encoding_index = 0;
  // Compute how many function names per line accounting for ',' and white
  // space.
  size_t per_line = 76 / (8 + name.size() + 1 + 2 + 2);
  for (uint64_t i = 0; i < discriminator_size_; i++) {
    uint64_t value = 0xffff'ffff'ffff'ffff;
    if (encoding_index < encoding_group_vec_.size()) {
      value = ExtractValue(
          encoding_group_vec_[encoding_index]->encoding_vec_[0]->GetValue(),
          discriminator_recipe_);
    }
    // Line start indent.
    if ((i % per_line) == 0) {
      absl::StrAppend(initializers_ptr, "   ");
    }
    if (i == value) {
      absl::StrAppend(initializers_ptr, " &Decode", name, "_", absl::Hex(i),
                      ",");
      encoding_index++;
    } else {
      absl::StrAppend(initializers_ptr, " &Decode", inst_group_->name(),
                      "None,");
    }

    // Break the line every 4 iterms.
    if ((i % per_line) == per_line - 1) {
      absl::StrAppend(initializers_ptr, "\n");
    }
  }
  absl::StrAppend(initializers_ptr, "};\n\n");
  for (auto const *enc_grp : encoding_group_vec_) {
    // Don't create initializers for leaf encoding groups. They don't need them.
    if (enc_grp->encoding_group_vec_.empty()) continue;
    std::string grp_name = absl::StrCat(
        name, "_",
        absl::Hex(ExtractValue(enc_grp->encoding_vec_[0]->GetValue(),
                               discriminator_recipe_)));
    enc_grp->EmitInitializers(grp_name, initializers_ptr, opcode_enum);
  }
}

// Generate the code for the decoders, both the declarations in the .h file as
// well as the definitions in the .cc file.
void EncodingGroup::EmitDecoders(absl::string_view name,
                                 std::string *declarations_ptr,
                                 std::string *definitions_ptr,
                                 const std::string &opcode_enum) const {
  // Generate the decode function signature.
  std::string signature = absl::StrCat(opcode_enum, " Decode", name, "(",
                                       inst_word_type_, " inst_word)");
  absl::StrAppend(declarations_ptr, signature, ";\n");
  absl::StrAppend(definitions_ptr, signature, " {\n");
  // Generate the index extraction code if the discriminator size is > 0.
  std::string index_extraction;
  if (!discriminator_recipe_.empty()) {
    index_extraction = absl::StrCat("  ", inst_word_type_, " index;\n");
    absl::StrAppend(
        &index_extraction,
        WriteExtraction(discriminator_recipe_, "inst_word", "index", "  "));
  }
  // If the encoding group has a constant value, generate that test.
  std::string constant_test;
  if (constant_) {
    uint64_t const_value = encoding_vec_[0]->GetValue() & constant_;
    absl::StrAppend(&constant_test, "  if ((inst_word & 0x",
                    absl::Hex(constant_), ") != 0x", absl::Hex(const_value),
                    ") return ", opcode_enum, "::kNone;\n");
  }
  if (!encoding_group_vec_.empty()) {
    // Create decoder for a non-leaf encoding group. Just extract the index
    // and call the next level decode.
    absl::StrAppend(definitions_ptr, constant_test, index_extraction);
    absl::StrAppend(definitions_ptr, "  return parse_group_", name,
                    "[index](inst_word);\n");
  } else {
    // Create decoder for a leaf encoding group.
    if (simple_decoding_) {
      // Simple decoding means that there are no special values in the opcodes
      // that have to be extracted and checked. A simple table lookup is
      // sufficient.
      if (encoding_vec_.size() == 1) {
        // If the table size is 1, no need to generate the table, just return
        // the opcode.
        absl::StrAppend(definitions_ptr, constant_test, "  return ",
                        opcode_enum, "::k",
                        ToPascalCase(encoding_vec_[0]->name()), ";\n");
      } else {
        // First generate the table of opcodes. The opcodes are in order of the
        // extracted discriminator value. If there are gaps, fill them in with
        // kNone values.
        int count = 1 << absl::popcount(discriminator_);
        absl::StrAppend(definitions_ptr, "  static constexpr ", opcode_enum,
                        " opcodes[", count, "] = {\n");
        int entry = 0;
        for (auto *enc : encoding_vec_) {
          int value = ExtractValue(enc->GetValue(), discriminator_recipe_);
          while (entry < value) {
            absl::StrAppend(definitions_ptr, "    ", opcode_enum, "::kNone,\n");
            entry++;
          }
          absl::StrAppend(definitions_ptr, "    ", opcode_enum, "::k",
                          ToPascalCase(enc->name()), ",\n");
          entry++;
        }
        while (entry < count) {
          absl::StrAppend(definitions_ptr, "    ", opcode_enum, "::kNone,\n");
          entry++;
        }
        absl::StrAppend(definitions_ptr, "  };\n");
        // Return the appropriate opcode.
        absl::StrAppend(definitions_ptr, constant_test, index_extraction);
        absl::StrAppend(definitions_ptr, "  return opcodes[index];\n");
      }
    } else {  // if (simple_decoding_)
      // Non simple decoding requires a sequence of if statements, as some
      // of the opcodes may have additional constraints == or != on bitfields
      // or overlays.
      absl::StrAppend(definitions_ptr, constant_test, index_extraction);
      EmitComplexDecoderBody(definitions_ptr, index_extraction, opcode_enum);
    }
  }

  absl::StrAppend(definitions_ptr, "}\n\n");

  if (encoding_group_vec_.empty()) return;

  for (auto const *enc_grp : encoding_group_vec_) {
    uint64_t value = ExtractValue(enc_grp->encoding_vec_[0]->GetValue(),
                                  discriminator_recipe_);
    std::string grp_name = absl::StrCat(name, "_", absl::Hex(value));
    enc_grp->EmitDecoders(grp_name, declarations_ptr, definitions_ptr,
                          opcode_enum);
  }
}

void EncodingGroup::EmitComplexDecoderBody(
    std::string *definitions_ptr, absl::string_view index_extraction,
    absl::string_view opcode_enum) const {
  // For each instruction in the encoding vec, generate the if statement
  // to see if the instruction is matched.
  absl::flat_hash_set<std::string> extracted;
  // For equal constraints, some can be ignored because those bits are wholly
  // considered by the parent groups or the discriminator.
  for (auto *encoding : encoding_vec_) {
    for (auto *constraint : encoding->equal_constraints()) {
      if (constraint->field != nullptr) {
        // Field constraint.
        Field *field = constraint->field;
        std::string name = absl::StrCat(field->name, "_value");
        if (extracted.contains(name)) continue;
        uint64_t mask = ((1ULL << field->width) - 1);
        // If the bits in the field are already handled by a parent or
        // the discriminator, ignore the field. There is no need to emit
        // a check for it.
        if (((mask << field->low) & ~(ignore_ | discriminator_)) == 0) {
          constraint->can_ignore = true;
        }
        continue;
      }

      // It's an overlay constraint.
      Overlay *overlay = constraint->overlay;
      std::string name = absl::StrCat(overlay->name(), "_value");
      uint64_t mask = 0;
      // Get the bits that correspond to the overlay.
      auto result = overlay->GetBitField((1 << overlay->declared_width()) - 1);
      if (result.ok()) {
        mask = result.value();
      } else {
        absl::StrAppend(definitions_ptr,
                        "#error Internal error: cannot extract value from ",
                        overlay->name());
        continue;
      }
      // If the bits in the overlay are already handled by a parent or
      // the discriminator, ignore the field. There is no need to emit
      // a check for it.
      if ((mask & ~(ignore_ | discriminator_)) == 0) {
        constraint->can_ignore = true;
      }
    }
    // Write any field/overlay extractions needed for the encoding (that
    // haven't already been extracted).
    EmitExtractions(encoding->equal_constraints(), extracted, definitions_ptr);
    EmitExtractions(encoding->equal_extracted_constraints(), extracted,
                    definitions_ptr);
    EmitExtractions(encoding->other_constraints(), extracted, definitions_ptr);

    // Get the discriminator value.
    uint64_t index_value =
        ExtractValue(encoding->GetValue(), discriminator_recipe_);
    // Construct the if statement. First the discriminator value (index).
    std::string condition;
    std::string connector;
    int count = 0;
    // Equal constraints.
    count += EmitConstraintConditions(encoding->equal_constraints(),
                                      "==", connector, &condition);
    count += EmitConstraintConditions(encoding->equal_extracted_constraints(),
                                      "==", connector, &condition);
    count += EmitConstraintConditions(encoding->other_constraints(),
                                      "!=", connector, &condition);
    // Write out the full if statement.
    if (!index_extraction.empty()) {
      // If there is an index extraction, then add that to the if statement.
      // Check to see how many additional conjunctions in the if statement and
      // adjust the number of parentheses required.
      if (count > 0) {
        absl::StrAppend(definitions_ptr, "  if ((index == 0x",
                        absl::Hex(index_value), ") &&\n      ", condition,
                        ")\n");
      } else {
        absl::StrAppend(definitions_ptr, "  if (index == 0x",
                        absl::Hex(index_value), ")\n");
      }
    } else {
      // There is no index extraction. Ensure the number of parentheses are
      // appropriate to the number of conjunctions in the if statement.
      if (count > 1) {
        absl::StrAppend(definitions_ptr, "  if (", condition, ")\n");
      } else {
        absl::StrAppend(definitions_ptr, "  if ", condition, "\n");
      }
    }
    absl::StrAppend(definitions_ptr, "    return ", opcode_enum, "::k",
                    ToPascalCase(encoding->name()), ";\n");
  }
  absl::StrAppend(definitions_ptr, "  return ", opcode_enum, "::kNone;\n");
}

void EncodingGroup::EmitExtractions(
    const std::vector<Constraint *> &constraints,
    absl::flat_hash_set<std::string> &extracted,
    std::string *definitions_ptr) const {
  // Write any field/overlay extractions needed for the constraints.
  // Note, the extractions may be wider than the instruction word width, due
  // to constant bits being added, so make sure to use appropriate type for each
  // extraction.
  for (auto const *constraint : constraints) {
    if (constraint->can_ignore) continue;
    if (constraint->field != nullptr) {
      Field *field = constraint->field;
      std::string name = absl::StrCat(field->name, "_value");
      if (!extracted.contains(name)) {
        std::string data_type;
        if (field->width > inst_group_->width()) {
          auto shift = absl::bit_width(static_cast<unsigned>(field->width)) - 1;
          if (absl::popcount(static_cast<unsigned>(field->width)) > 1) shift++;
          shift = std::max(shift, 3);
          if (shift > 6) {
            LOG(ERROR) << "Field '" << field->name
                       << "' width: " << field->width << " > 64 bits";
            data_type =
                absl::StrCat("#error field width ", field->width, " > 64 bits");
          } else {
            data_type = absl::StrCat("uint", 1 << shift, "_t");
          }
        } else {
          data_type = inst_word_type_;
        }
        uint64_t mask = ((1ULL << field->width) - 1);
        absl::StrAppend(definitions_ptr, "  ", data_type, " ", name,
                        " = (inst_word >> ", field->low, ") & 0x",
                        absl::Hex(mask), ";\n");
        extracted.insert(name);
      }
    } else {
      Overlay *overlay = constraint->overlay;
      std::string name = absl::StrCat(overlay->name(), "_value");
      if (!extracted.contains(name)) {
        auto ovl_width = overlay->declared_width();
        std::string data_type;
        if (ovl_width > inst_group_->width()) {
          auto shift = absl::bit_width(static_cast<unsigned>(ovl_width)) - 1;
          if (absl::popcount(static_cast<unsigned>(ovl_width)) > 1) shift++;
          shift = std::max(shift, 3);
          if (shift > 6) {
            LOG(ERROR) << "Field '" << overlay->name()
                       << "' width: " << ovl_width << " > 64 bits";
            data_type =
                absl::StrCat("#error overlay width ", ovl_width, " > 64 bits");
          } else {
            data_type = absl::StrCat("uint", 1 << shift, "_t");
          }
        } else {
          data_type = inst_word_type_;
        }
        absl::StrAppend(definitions_ptr, "  ", data_type, " ", name, ";\n");
        absl::StrAppend(definitions_ptr,
                        overlay->WriteSimpleValueExtractor("inst_word", name));
        extracted.insert(name);
      }
    }
  }
}

int EncodingGroup::EmitConstraintConditions(
    const std::vector<Constraint *> &constraints, absl::string_view comparison,
    std::string &connector, std::string *condition) const {
  int count = 0;
  for (auto const *constraint : constraints) {
    std::string comparison(kComparison[static_cast<int>(constraint->type)]);
    if (constraint->can_ignore) continue;
    std::string name = absl::StrCat((constraint->field != nullptr)
                                        ? constraint->field->name
                                        : constraint->overlay->name(),
                                    "_value");
    absl::StrAppend(condition, connector, "(", name, " ", comparison, " 0x",
                    absl::Hex(constraint->value), ")");
    connector = " &&\n      ";
    count++;
  }
  return count;
}

// This method dumps statistics about the group useful for development.
// TODO: remove when no longer needed.
std::string EncodingGroup::DumpGroup(std::string prefix, std::string indent) {
  std::string output;
  auto pad = absl::PadSpec::kZeroPad8;
  uint64_t grp_value;
  if (parent_ == nullptr) {
    auto grp_recipe = GetExtractionRecipe(constant_);
    grp_value = ExtractValue(encoding_vec_[0]->GetValue(), grp_recipe);
  } else {
    auto grp_recipe = GetExtractionRecipe(parent_->mask() & parent_->varying());
    grp_value = ExtractValue(encoding_vec_[0]->GetValue(), grp_recipe);
  }
  uint64_t const_value = encoding_vec_[0]->GetValue() & constant_;
  uint64_t discriminator = mask_ & ~constant_;
  absl::StrAppend(
      &output, "//", indent, prefix, "GROUP:\n//", indent,
      "  mask:          ", absl::Hex(mask_, pad), "\n//", indent,
      "  ignore:        ", absl::Hex(ignore_, pad), "\n//", indent,
      "  constant:      ", absl::Hex(constant_, pad), " : ",
      absl::Hex(const_value, pad), "\n//", indent, indent,
      "  varying:       ", absl::Hex(varying_, pad), "\n//", indent,
      "  value:         ", absl::Hex(grp_value, pad), "\n//", indent,
      "  discriminator: ", absl::Hex(discriminator, pad), "\n//", indent,
      "  simple:       ", simple_decoding_ ? "true\n//" : "false\n//", indent,
      "  leaf:         ",
      encoding_group_vec_.empty() ? "true\n//" : "false\n//",
      "  encodings:     ", encoding_vec_.size(), "\n");
  if (encoding_group_vec_.empty()) {
    auto recipe = GetExtractionRecipe(varying_ & mask_ & ~ignore_);
    for (auto *enc : encoding_vec_) {
      uint64_t value = ExtractValue(enc->GetValue(), recipe);
      absl::StrAppend(&output, "//", indent, "  ", enc->name(), ": ",
                      absl::Hex(enc->GetValue() & varying_ & mask_, pad), " : ",
                      absl::Hex(value, pad), ": ");
      uint64_t mask = enc->GetCombinedMask();  // ^ mask_;
      if (parent_ != nullptr) {
        mask &= ~(parent_->mask());
      }
      if (mask != 0) {
        mask &= ~ignore_;
        absl::StrAppend(&output, absl::Hex(mask, pad), ": ");
      }
      for (auto *constraint : enc->equal_extracted_constraints()) {
        std::string name = constraint->field == nullptr
                               ? constraint->overlay->name()
                               : constraint->field->name;
        absl::StrAppend(&output, " ", name, " == ",
                        absl::Hex(constraint->value, absl::PadSpec::kZeroPad8),
                        " ");
      }
      for (auto *constraint : enc->other_constraints()) {
        std::string name = constraint->field == nullptr
                               ? constraint->overlay->name()
                               : constraint->field->name;
        absl::StrAppend(&output, " ", name, " != ",
                        absl::Hex(constraint->value, absl::PadSpec::kZeroPad8),
                        " ");
      }
      absl::StrAppend(&output, "\n");
    }
  } else {
    for (auto *group : encoding_group_vec_) {
      absl::StrAppend(&output, group->DumpGroup("SUB" + prefix, indent + "  "));
    }
  }
  return output;
}

}  // namespace bin_format
}  // namespace decoder
}  // namespace sim
}  // namespace mpact
