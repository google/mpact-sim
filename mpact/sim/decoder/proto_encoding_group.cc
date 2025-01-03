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

#include "mpact/sim/decoder/proto_encoding_group.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

#include "absl/container/btree_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_replace.h"
#include "absl/strings/string_view.h"
#include "mpact/sim/decoder/decoder_error_listener.h"
#include "mpact/sim/decoder/format_name.h"
#include "mpact/sim/decoder/proto_constraint_expression.h"
#include "mpact/sim/decoder/proto_constraint_value_set.h"
#include "mpact/sim/decoder/proto_encoding_info.h"
#include "mpact/sim/decoder/proto_format_contexts.h"
#include "mpact/sim/decoder/proto_instruction_encoding.h"
#include "mpact/sim/decoder/proto_instruction_group.h"
#include "src/google/protobuf/descriptor.h"

namespace mpact {
namespace sim {
namespace decoder {
namespace proto_fmt {

using ::mpact::sim::machine_description::instruction_set::ToPascalCase;
using ::mpact::sim::machine_description::instruction_set::ToSnakeCase;

struct FieldInfo {
  const google::protobuf::FieldDescriptor *field;
  const google::protobuf::OneofDescriptor *oneof;
  QualifiedIdentCtx *ctx;
  absl::btree_multimap<int64_t, const ProtoInstructionEncoding *> value_map;
  int64_t min_value = 0;
  int64_t max_value = 0;
  size_t unique_values = 0;
  double density = 0.0;
};

using ConstraintValueRange = ProtoConstraintValueSet::SubRange;

ProtoEncodingGroup::ProtoEncodingGroup(ProtoInstructionGroup *inst_group,
                                       int level,
                                       DecoderErrorListener *error_listener)
    : ProtoEncodingGroup(nullptr, inst_group, level, error_listener) {}

ProtoEncodingGroup::ProtoEncodingGroup(ProtoEncodingGroup *parent,
                                       ProtoInstructionGroup *inst_group,
                                       int level,
                                       DecoderErrorListener *error_listener)
    : inst_group_(inst_group),
      parent_(parent),
      error_listener_(error_listener),
      level_(level) {}

ProtoEncodingGroup::~ProtoEncodingGroup() {
  for (auto const &[unused, field_info] : field_map_) {
    delete field_info;
  }
  field_map_.clear();
  for (auto const *enc : encoding_vec_) {
    delete enc;
  }
  encoding_vec_.clear();
  inst_group_ = nullptr;
  for (auto const *enc_group : encoding_group_vec_) {
    delete enc_group;
  }
  encoding_group_vec_.clear();
}

void ProtoEncodingGroup::AddEncoding(ProtoInstructionEncoding *enc) {
  // All constraints in equal_constraints are kEq constraints on integer
  // fields, or are kHas constraints which are kEq constraints on the
  // '_value()' function of the one_of field (which is an int value).
  // The first step is to determine which constraints differentiate the most
  // instructions in the encoding group.
  for (auto *eq_constraint : enc->equal_constraints()) {
    auto const *field = eq_constraint->field_descriptor;
    auto const *oneof = field->containing_oneof();
    auto const *expr = eq_constraint->expr;
    auto op = eq_constraint->op;
    auto *qualifed_ident_ctx = eq_constraint->ctx->qualified_ident();
    int64_t value;  // Store the value in an int64_t.
    if (op == ConstraintType::kEq) {
      switch (expr->variant_type()) {
        case *ProtoValueIndex::kInt32:
          value = expr->GetValueAs<int32_t>();
          break;
        case *ProtoValueIndex::kInt64:
          value = expr->GetValueAs<int64_t>();
          break;
        case *ProtoValueIndex::kUint32:
          value = expr->GetValueAs<uint32_t>();
          break;
        case *ProtoValueIndex::kUint64: {
          // If the value overflows int64_t, just flag an error. Keep it
          // simple.
          uint64_t tmp = expr->GetValueAs<uint64_t>();
          if (tmp > std::numeric_limits<int64_t>::max()) {
            error_listener()->semanticError(
                eq_constraint->ctx->start,
                absl::StrCat("Expression value for field '", field->name(),
                             "' overflows int64_t."));
            return;
          }
          value = static_cast<int64_t>(tmp);
          break;
        }
        default:
          error_listener()->semanticError(
              eq_constraint->ctx->start,
              absl::StrCat(
                  "Illegal type in expression in constraint for field '",
                  field->name(), "'."));
          return;
      }
      oneof = nullptr;
    } else if (op == ConstraintType::kHas) {
      value = field->index();
      oneof = field->containing_oneof();
      field = nullptr;
    } else {
      error_listener()->semanticError(
          eq_constraint->ctx->start,
          absl::StrCat("Illegal constraint op for field '", field->name(),
                       "' in equality constraints."));
      return;
    }
    eq_constraint->value = value;
    // If the field_info doesn't exist, add a new field_info.
    auto name = field != nullptr ? field->name() : oneof->name();
    auto iter = field_map_.find(name);
    FieldInfo *field_info = nullptr;
    if (iter == field_map_.end()) {
      field_info = new FieldInfo{field, oneof};
      field_info->min_value = std::numeric_limits<int64_t>::max();
      field_info->max_value = std::numeric_limits<int64_t>::min();
      field_info->ctx = qualifed_ident_ctx;
      field_map_.emplace(name, field_info);
    } else {
      field_info = iter->second;
    }
    // Add a new entry for the value.
    field_info->unique_values += !field_info->value_map.contains(value);
    field_info->min_value = std::min(field_info->min_value, value);
    field_info->max_value = std::max(field_info->max_value, value);
    field_info->value_map.insert({value, enc});
  }
  encoding_vec_.push_back(enc);
  // Populate the other_* sets. These are used later to ensure that sub groups
  // aren't added with differentiators that are also used in other constraints.
  for (auto *constraint : enc->other_constraints()) {
    auto const *field = constraint->field_descriptor;
    auto const *oneof = field->containing_oneof();
    if (oneof != nullptr) {
      other_oneof_set_.insert(oneof);
      continue;
    }
    other_field_set_.insert(field);
  }
}

void ProtoEncodingGroup::AddSubGroups() {
  // If there is only one encoding, return.
  if (encoding_vec_.size() == 1) return;
  // First determine which field is the most productive to use to split up the
  // group. This is determined by how many values it has, with some thought to
  // the number of total values in its interval.
  // To start with just pick the one with the largest number of unique values,
  // as that should create a shallower decoding tree.
  FieldInfo *best_field = nullptr;
  absl::flat_hash_set<ProtoInstructionEncoding *> encodings;
  for (auto enc : encoding_vec_) {
    encodings.insert(enc);
  }
  for (auto &[unused, field_info] : field_map_) {
    // First check if the field is used in any other constraints, e.g., '>' or
    // '!='. If so, it is not a candidate for a direct lookup of the value.
    if (other_field_set_.contains(field_info->field)) continue;
    if (other_oneof_set_.contains(field_info->oneof)) continue;
    if (best_field == nullptr) {
      best_field = field_info;
    } else {
      if (field_info->unique_values > best_field->unique_values) {
        best_field = field_info;
      }
    }
  }
  // If there is no best field, or it doesn't differentiate we're done, but
  // first check the encodings to make sure there are no ambiguities or
  // duplicate encodings.
  if ((best_field == nullptr) || (best_field->unique_values == 1)) {
    CheckEncodings();
    return;
  }

  // Save the differentiating field info in this group.
  differentiator_ = best_field;

  // Next, create an encoding group for each value of the field, adding the
  // encodings that match the value to the corresponding groups.
  for (auto iter = best_field->value_map.begin();
       iter != best_field->value_map.end();
       /*empty*/) {
    auto *enc_group =
        new ProtoEncodingGroup(this, inst_group_, level_ + 1, error_listener_);
    int64_t value = iter->first;
    enc_group->set_value(value);
    while ((iter != best_field->value_map.end()) && (value == iter->first)) {
      // First create a copy of the encoding and remove the constraint
      // that corresponds with the field info, so it will not be considered
      // below.
      ProtoInstructionEncoding *enc =
          new ProtoInstructionEncoding(*(iter->second));
      // Remove the best_field constraint from the new encoding object.
      auto v_iter = enc->equal_constraints().begin();
      ProtoConstraint *constraint = nullptr;
      while (v_iter != enc->equal_constraints().end()) {
        constraint = *v_iter;
        if (constraint->op == ConstraintType::kEq) {
          if ((best_field->field != nullptr) &&
              (best_field->field == constraint->field_descriptor)) {
            break;
          }
        } else {  // This constraint is a kHas.
          if ((best_field->oneof != nullptr) &&
              (best_field->oneof ==
               constraint->field_descriptor->containing_oneof())) {
            break;
          }
        }
        ++v_iter;
      }
      if (v_iter != enc->equal_constraints().end()) {
        delete constraint->expr;
        delete constraint;
        enc->equal_constraints().erase(v_iter);
      }
      enc_group->AddEncoding(enc);
      // Remove the encoding from the map.
      encodings.erase(iter->second);
      ++iter;
    }
    encoding_group_vec_.push_back(enc_group);
  }
  // Any encodings remaining in the map have to be added to each of the sub
  // groups, as they weren't selected by value.
  for (auto enc : encodings) {
    for (auto *enc_group : encoding_group_vec_) {
      auto enc_copy = new ProtoInstructionEncoding(*enc);
      enc_group->AddEncoding(enc_copy);
    }
  }
  encodings.clear();
  // Recursively try to split the child encoding groups.
  for (auto *enc_group : encoding_group_vec_) {
    enc_group->AddSubGroups();
  }
}

// Check the encodings to make sure there aren't ambiguities.
void ProtoEncodingGroup::CheckEncodings() {
  // If there is only one encoding, there is no ambiguity.
  if (encoding_vec_.size() <= 1) return;
  // Encodings have to have additional constraints to differentiate between each
  // other, so check to see if any of them have none, and if so, signal an
  // error.
  for (auto *enc : encoding_vec_) {
    if (enc->equal_constraints().empty() && enc->other_constraints().empty()) {
      std::string msg =
          absl::StrCat("Decoding ambiguity between '", enc->name(), "' and :");
      for (auto *other_enc : encoding_vec_) {
        if (enc == other_enc) continue;
        absl::StrAppend(&msg, " '", other_enc->name(), "'");
      }
      error_listener()->semanticError(nullptr, msg);
      return;
    }
  }

  // Check for identical or overlapping constraints.

  // First sort the constraints in each vector.
  std::vector<std::vector<ProtoConstraint *>> constraints;
  constraints.reserve(encoding_vec_.size());
  for (auto *enc : encoding_vec_) {
    constraints.push_back({});
    for (auto *constraint : enc->equal_constraints()) {
      constraints.back().push_back(constraint);
    }
    for (auto *constraint : enc->other_constraints()) {
      constraints.back().push_back(constraint);
    }
  }
  for (int i = 0; i < constraints.size(); ++i) {
    std::sort(
        constraints[i].begin(), constraints[i].end(),
        [](const ProtoConstraint *lhs, const ProtoConstraint *rhs) -> bool {
          return lhs->field_descriptor->full_name() <
                 rhs->field_descriptor->full_name();
        });
  }
  // Now create value value sets for each field descriptor, combining multiple
  // constraints on the same field descriptor into a single set of values.
  std::vector<std::vector<ProtoConstraintValueSet *>> value_sets;
  value_sets.reserve(encoding_vec_.size());
  for (auto const &constraint_vec : constraints) {
    const google::protobuf::FieldDescriptor *previous = nullptr;
    value_sets.push_back({});
    for (auto const *constraint : constraint_vec) {
      // If it's the first occurance of a field descriptor, create a new
      // range on this constraint.
      ProtoConstraintValueSet *value_set = nullptr;
      if (previous != constraint->field_descriptor) {
        previous = constraint->field_descriptor;
        value_set = new ProtoConstraintValueSet(constraint);
        value_sets.back().push_back(value_set);
        continue;
      }
      // This is not the first occurance of a field descriptor. Intersect
      // with the current range.
      auto status =
          value_set->IntersectWith(ProtoConstraintValueSet(constraint));
      // Check for error.
      if (!status.ok()) {
        // Clean up.
        for (auto &value_set_list : value_sets) {
          for (auto *value_set : value_set_list) delete value_set;
        }
        value_sets.clear();
        // Signal error.
        error_listener()->semanticError(nullptr, status.message());
        return;
      }
    }
  }
  for (int i = 0; i < value_sets.size(); ++i) {
    for (int j = i + 1; j < value_sets.size(); ++j) {
      if (DoConstraintsOverlap(value_sets[i], value_sets[j])) {
        error_listener()->semanticError(
            nullptr, absl::StrCat("Encoding group '", inst_group_->name(),
                                  "': encoding ambiguity between '",
                                  encoding_vec_[i]->name(), " and ",
                                  encoding_vec_[j]->name(), "'"));
      }
    }
  }
  // Clean up.
  for (auto &value_set_list : value_sets) {
    for (auto *value_set : value_set_list) delete value_set;
  }
}

// Determine if the constraints overlap for two encodings lhs and rhs based on
// the value sets.
bool ProtoEncodingGroup::DoConstraintsOverlap(
    const std::vector<ProtoConstraintValueSet *> &lhs,
    const std::vector<ProtoConstraintValueSet *> &rhs) {
  auto iter_lhs = lhs.begin();
  auto iter_rhs = rhs.begin();
  while ((iter_lhs != lhs.end()) && (iter_rhs != rhs.end())) {
    // The constraint value sets are sorted by field descriptor name, so if
    // the field descriptors are different, then the constraints do not overlap.
    if ((*iter_lhs)->field_descriptor()->full_name() !=
        (*iter_rhs)->field_descriptor()->full_name()) {
      return false;
    }
    ProtoConstraintValueSet lhs_copy(**(iter_lhs));
    auto status = lhs_copy.IntersectWith(**(iter_rhs));
    // If there is an error taking the intersection, return true to signify
    // an overlap, even if there isn't one.
    if (!status.ok()) return true;
    // If the intersection is empty, then they don't overlap. No need to check
    // further.
    if (lhs_copy.IsEmpty()) return false;
    ++iter_lhs;
    ++iter_rhs;
  }
  // If there are additional constraint value sets for either instruction, then
  // they don't overlap.
  if ((iter_lhs != lhs.end()) || (iter_rhs != rhs.end())) {
    return false;
  }
  return true;
}

constexpr char kDecodeMsgName[] = "inst_proto";

std::string ProtoEncodingGroup::EmitLeafDecoder(
    absl::string_view fcn_name, absl::string_view opcode_enum,
    absl::string_view message_type_name, int indent_width) const {
  std::string output;
  std::string if_sep;
  std::string decoder_class =
      ToPascalCase(inst_group_->encoding_info()->decoder()->name()) + "Decoder";
  absl::StrAppend(&output, std::string(indent_width, ' '), opcode_enum, " ",
                  fcn_name, "(", message_type_name, " ", kDecodeMsgName, ", ",
                  decoder_class, " *decoder) {\n");
  indent_width += 2;
  std::string indent(indent_width, ' ');
  // Check for the case when there is only a single encoding with no
  // constraints.
  if ((encoding_vec_.size() == 1) &&
      encoding_vec_[0]->equal_constraints().empty() &&
      encoding_vec_[0]->other_constraints().empty()) {
    absl::StrAppend(
        &output, encoding_vec_[0]->GetSetterCode(kDecodeMsgName, indent_width),
        "return ", opcode_enum, "::k", ToPascalCase(encoding_vec_[0]->name()),
        ";\n");
    indent_width -= 2;
    absl::StrAppend(&output, std::string(indent_width, ' '), "}\n\n");
    return output;
  }

  // Helper lambda.
  auto generate_condition =
      [](const ProtoConstraint *constraint) -> std::string {
    std::string output;
    if (constraint->op == ConstraintType::kHas) {
      std::string ident = constraint->ctx->qualified_ident()->getText();
      auto pos = ident.find_last_of('.');
      std::string prefix;
      if (pos != std::string::npos) {
        prefix = absl::StrCat(".", ident.substr(0, pos + 1));
      }
      auto oneof_desc = constraint->field_descriptor->containing_oneof();
      auto oneof_name = oneof_desc->name();
      std::string parent_name;
      for (auto parent = oneof_desc->containing_type(); parent != nullptr;
           parent = parent->containing_type()) {
        absl::StrAppend(&parent_name, ToPascalCase(parent->name()), "::");
      }
      auto package = absl::StrReplaceAll(
          constraint->field_descriptor->file()->package(), {{".", "::"}});
      return absl::StrCat(
          "(", kDecodeMsgName, prefix, ".", oneof_name, "_case() == ", package,
          "::", parent_name, ToPascalCase(oneof_name), "Case::k",
          ToPascalCase(constraint->field_descriptor->name()), ")");
    } else {
      return absl::StrCat("(", kDecodeMsgName, ".",
                          constraint->ctx->field->getText(), " ",
                          GetOpText(constraint->op), " ",
                          constraint->ctx->constraint_expr()->getText(), ")");
    }
  };

  // Generate a chained if-else if-else-statement for the encodings in the
  // encoding vector.
  std::string indent_body(indent_width + 2, ' ');
  for (auto *enc : encoding_vec_) {
    // Generate the if statement conditions.
    absl::StrAppend(&output, indent, if_sep, "if (");
    std::string cond_sep;
    for (auto const *constraint : enc->equal_constraints()) {
      absl::StrAppend(&output, cond_sep, generate_condition(constraint));
      cond_sep = " && ";
    }
    for (auto const *constraint : enc->other_constraints()) {
      absl::StrAppend(&output, cond_sep, generate_condition(constraint));
      cond_sep = " && ";
    }
    absl::StrAppend(&output, ") {\n");

    // Generate if statement body.
    absl::StrAppend(&output, indent_body,
                    enc->GetSetterCode(kDecodeMsgName, indent_width + 2),
                    "return ", opcode_enum, "::k", ToPascalCase(enc->name()),
                    ";\n");

    if_sep = "} else ";
  }
  // Generate the fall through.
  absl::StrAppend(&output, indent, "}\n", indent, "return ", opcode_enum,
                  "::kNone;\n");
  indent_width -= 2;
  absl::StrAppend(&output, std::string(indent_width, ' '), "}\n\n");
  return output;
}

namespace {

bool LessThan(ProtoEncodingGroup *lhs, ProtoEncodingGroup *rhs) {
  return lhs->value() < rhs->value();
}

}  // namespace

std::string ProtoEncodingGroup::EmitComplexDecoder(
    absl::string_view fcn_name, absl::string_view opcode_enum,
    absl::string_view message_type_name) {
  std::string output;
  if (encoding_group_vec_.empty()) {
    return EmitLeafDecoder(fcn_name, opcode_enum, message_type_name, 0);
  }
  std::string decoder_class =
      ToPascalCase(inst_group_->encoding_info()->decoder()->name()) + "Decoder";
  // First emit the function call tables.
  // Sort the encoding_group_vec according to differentiator value.
  std::sort(encoding_group_vec_.begin(), encoding_group_vec_.end(), LessThan);
  // Now emit the decoder function.
  double density =
      (double)differentiator_->unique_values /
      (double)(differentiator_->max_value - differentiator_->min_value);
  if (density < 0.75) {
    std::string map_name = absl::StrCat(ToSnakeCase(fcn_name), "_map");
    absl::StrAppend(
        &output,
        "absl::NoDestructor<absl::flat_hash_map<int32_t, std::function<",
        opcode_enum, "(", message_type_name, ", ", decoder_class, "*)>>> ",
        map_name, "({\n");
    for (auto *enc_group : encoding_group_vec_) {
      auto enc_value = enc_group->value();
      absl::StrAppend(&output, "  {", enc_value, ", ", fcn_name, "_", enc_value,
                      "},\n");
    }
    absl::StrAppend(&output, "});\n\n");
    // Emit the function body.
    auto call =
        absl::StrReplaceAll(differentiator_->ctx->getText(), {{".", "()."}});
    absl::StrAppend(&output, opcode_enum, " ", fcn_name, "(", message_type_name,
                    " ", kDecodeMsgName, ", ", decoder_class, " *decoder) {\n",
                    "  auto iter = ", map_name, "->find(", kDecodeMsgName, ".",
                    call, "());\n", "  if (iter == ", map_name,
                    "->end()) return ", opcode_enum, "::kNone;\n",
                    "  return iter->second(", kDecodeMsgName,
                    ", decoder);\n}\n\n");
  } else {
    auto min = differentiator_->min_value;
    auto max = differentiator_->max_value;
    auto num_values = max - min + 1;
    auto iter = encoding_group_vec_.begin();
    absl::StrAppend(&output, "std::function<", opcode_enum, "(",
                    message_type_name, ", ", decoder_class, "*)> ",
                    ToSnakeCase(fcn_name), "_table[", num_values, "] = {\n");
    // Fill in the entries in the function table.
    for (int i = 0; i < num_values; ++i) {
      auto enc_value = iter != encoding_group_vec_.end()
                           ? (*iter)->value()
                           : std::numeric_limits<int64_t>::min();
      auto index = enc_value - min;
      if (index != i) {
        absl::StrAppend(&output, "  Decode", ToPascalCase(inst_group_->name()),
                        "_None,\n");
      } else {
        absl::StrAppend(&output, "  ", fcn_name, "_", enc_value, ",\n");
        ++iter;
      }
    }
    // Emit the function body.
    auto call =
        absl::StrReplaceAll(differentiator_->ctx->getText(), {{".", "()."}});
    absl::StrAppend(
        &output, "};\n\n", opcode_enum, " ", fcn_name, "(", message_type_name,
        " ", kDecodeMsgName, ", ", decoder_class, " *decoder) {\n", "  return ",
        ToSnakeCase(fcn_name), "_table[", kDecodeMsgName, ".", call, "() - ",
        differentiator_->min_value, "](", kDecodeMsgName, ", decoder);\n}\n\n");
  }
  return output;
}

std::string ProtoEncodingGroup::EmitDecoders(
    absl::string_view fcn_name, absl::string_view opcode_enum,
    absl::string_view message_type_name) {
  std::string output;
  // Emit decoders for subordinate groups (lower in the hierarchy).
  for (auto *enc_group : encoding_group_vec_) {
    absl::StrAppend(
        &output,
        enc_group->EmitDecoders(absl::StrCat(fcn_name, "_", enc_group->value()),
                                opcode_enum, message_type_name));
  }
  // Emit decoder for this group.
  absl::StrAppend(&output,
                  EmitComplexDecoder(fcn_name, opcode_enum, message_type_name));
  return output;
}

}  // namespace proto_fmt
}  // namespace decoder
}  // namespace sim
}  // namespace mpact
