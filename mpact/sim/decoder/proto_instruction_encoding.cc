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

#include "mpact/sim/decoder/proto_instruction_encoding.h"

#include <algorithm>
#include <functional>
#include <string>
#include <vector>

#include "absl/container/btree_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_replace.h"
#include "absl/strings/string_view.h"
#include "mpact/sim/decoder/format_name.h"
#include "mpact/sim/decoder/proto_constraint_expression.h"
#include "mpact/sim/decoder/proto_format_contexts.h"
#include "src/google/protobuf/descriptor.h"

namespace mpact {
namespace sim {
namespace decoder {
namespace proto_fmt {

using mpact::sim::machine_description::instruction_set::ToPascalCase;

ProtoInstructionEncoding::ProtoInstructionEncoding(
    std::string name, ProtoInstructionGroup *parent)
    : name_(name), instruction_group_(parent) {}

// Copy constructor.
ProtoInstructionEncoding::ProtoInstructionEncoding(
    const ProtoInstructionEncoding &rhs) {
  name_ = rhs.name_;
  instruction_group_ = rhs.instruction_group_;
  setter_code_ = rhs.setter_code_;
  for (const auto &[name, setter_ptr] : rhs.setter_map_) {
    setter_map_.insert({name, new ProtoSetter(*setter_ptr)});
  }
  for (const auto *constraint : rhs.equal_constraints_) {
    equal_constraints_.push_back(new ProtoConstraint(*constraint));
  }
  for (const auto *constraint : rhs.other_constraints_) {
    other_constraints_.push_back(new ProtoConstraint(*constraint));
  }
  for (const auto &[name, constraint_ptr] : rhs.has_constraints_) {
    has_constraints_.insert({name, new ProtoConstraint(*constraint_ptr)});
  }
}

ProtoInstructionEncoding::~ProtoInstructionEncoding() {
  for (const auto *constraint : equal_constraints_) {
    delete constraint->expr;
    delete constraint;
  }
  equal_constraints_.clear();
  for (const auto *constraint : other_constraints_) {
    delete constraint->expr;
    delete constraint;
  }
  other_constraints_.clear();
  for (const auto &[unused, constraint] : has_constraints_) {
    delete constraint;
  }
  has_constraints_.clear();
  for (const auto &[unused, setter] : setter_map_) {
    delete setter;
  }
  setter_map_.clear();
}

absl::Status ProtoInstructionEncoding::AddSetter(
    SetterDefCtx *ctx, const std::string &name,
    const google::protobuf::FieldDescriptor *field_descriptor,
    const std::vector<const google::protobuf::FieldDescriptor *> &one_of_fields,
    IfNotCtx *if_not) {
  if (ctx == nullptr) return absl::InvalidArgumentError("Context is null");

  // If there is a setter already for that name, return an error.
  auto iter = setter_map_.find(name);
  if (iter != setter_map_.end()) {
    return absl::AlreadyExistsError(
        absl::StrCat("Setter '", name, "' already defined."));
  }
  // Setters are added after constraints. For each depends_on, see if the
  // constraint already exists for the encoding. If so, remove it from the
  // one_of_fields vector, as it is guaranteed to be satisfied if the
  // instruction is successfully decoded. If it contradicts an existing
  // constraint, signal an error.
  ProtoConstraint *depends_on = nullptr;
  for (auto const *desc : one_of_fields) {
    auto iter = oneof_field_map_.find(desc->containing_oneof());
    if (iter != oneof_field_map_.end()) {
      // Duplicate of an encoding constraint.
      if (iter->second == desc) continue;
      // Conflict.
      return absl::InternalError(absl::StrCat(
          "One_of constraint on '", desc->name(),
          "' contradicts encoding constraint on '", iter->second->name(), "'"));
    }
    depends_on = AddHasConstraint(desc, depends_on);
  }

  // Add the setter information.
  setter_map_.emplace(
      name, new ProtoSetter{ctx, name, field_descriptor, if_not, depends_on});
  return absl::OkStatus();
}

absl::Status ProtoInstructionEncoding::AddConstraint(
    FieldConstraintCtx *ctx, ConstraintType op,
    const google::protobuf::FieldDescriptor *field_descriptor,
    const std::vector<const google::protobuf::FieldDescriptor *> &one_of_fields,
    const ProtoConstraintExpression *expr) {
  // One_of_fields is a list of fields that have 'kHas' constraints that are
  // prerequisites for the constraint being added. The variable depends_on
  // points to the end of the dependence chain, or nullptr if there are no or
  // duplicate one_of field constraints.
  ProtoConstraint *depends_on = nullptr;
  for (auto const *desc : one_of_fields) {
    auto iter = oneof_field_map_.find(desc->containing_oneof());
    if (iter != oneof_field_map_.end()) {
      if (iter->second == field_descriptor) {
        continue;  // Ignore duplicate.
      } else {
        // This contradicts a previous one_of constraint. Flag an error.
        return absl::InternalError(
            absl::StrCat("One_of constraint on '", desc->name(),
                         "' contradicts previous constraint on '",
                         iter->second->name(), "'"));
      }
    }
    // Add the one_of to the oneof_field_map_.
    oneof_field_map_.emplace(desc->containing_oneof(), desc);
    depends_on = AddHasConstraint(desc, depends_on);
  }
  // In order to generate a reasonably efficient decoder we divide the
  // constraints into two sets, those that can be used as indices into function
  // call tables or used as values in switch statements to differentiate between
  // the most instructions, and those that have to be evaluated in a slower
  // (often serial) manner. Only constraints on fields that don't depend on
  // other one_of fields can be treated in this manner. A dependency on one_of
  // fields can be used to create additional constraints, of which one is at the
  // top level in the proto.

  if (!one_of_fields.empty()) {
    // Add equal constraint on the first one_of_field dependency.
    equal_constraints_.push_back(new ProtoConstraint{
        ctx, one_of_fields[0], ConstraintType::kHas, nullptr, 0, nullptr});
    // Add the remaining one_of depende ncies to the other constraints.
    for (int i = 1; i < one_of_fields.size(); ++i) {
      other_constraints_.push_back(new ProtoConstraint{
          ctx, one_of_fields[i], ConstraintType::kHas, nullptr, 0, nullptr});
      // Add the constraint to the 'other' constraints.
      other_constraints_.push_back(
          new ProtoConstraint{ctx, field_descriptor, op, expr, 0, depends_on});
    }
    return absl::OkStatus();
  }

  // A kHas constraint on a member of a one_of field is equivalent to an kEq
  // constraint on the value of the one_of '_value()' function, so add it to the
  // equal_constraints vector. For kEq constraints, if the type of the field is
  // not an integer type, put it in the other_constraints.

  if ((op == ConstraintType::kEq) &&
      (IsIntType(kCppToVariantTypeMap[field_descriptor->cpp_type()]))) {
    // If it's an equal constraint with an integer type, add it to the 'equal'
    // constraints.
    equal_constraints_.push_back(
        new ProtoConstraint{ctx, field_descriptor, op, expr, 0, nullptr});
  } else if ((op == ConstraintType::kHas) &&
             (field_descriptor->containing_oneof() != nullptr)) {
    // If it's a kHas constraint on an one_of field, first make sure that it
    // does not contradict or duplicate any previous one_of kHas constraints.
    auto *oneof_desc = field_descriptor->containing_oneof();
    auto iter = oneof_field_map_.find(oneof_desc);
    if (iter != oneof_field_map_.end()) {
      // There is already a constraint on this oneof. Either it's the same
      // constraint, which means we can ignore this one, or it is for a
      // different field in which case it is a contradiction. Either way, the
      // constraint does not get added.
      if (iter->second == field_descriptor) {
        // It is a duplicate constraint. just ignore.
        return absl::OkStatus();
      }
      // It is a different field of the one_of, so there is a contradiction.
      // Return an error.
      return absl::InternalError(absl::StrCat(
          "One_of constraint on '", field_descriptor->name(),
          "' contradicts previous constraint on '", iter->second->name(), "'"));
    }
    oneof_field_map_.emplace(oneof_desc, field_descriptor);
    equal_constraints_.push_back(
        new ProtoConstraint{ctx, field_descriptor, op, nullptr, 0, nullptr});
  } else {
    // Otherwise add it to the 'other' constraints.
    other_constraints_.push_back(
        new ProtoConstraint{ctx, field_descriptor, op, expr, 0, nullptr});
  }
  return absl::OkStatus();
}

ProtoConstraint *ProtoInstructionEncoding::AddHasConstraint(
    const google::protobuf::FieldDescriptor *field_descriptor,
    ProtoConstraint *depends_on) {
  if (depends_on != nullptr) {
    if (!has_constraints_.contains(depends_on->field_descriptor->full_name())) {
      return nullptr;
    }
  }
  auto iter = has_constraints_.find(field_descriptor->full_name());
  if (iter != has_constraints_.end()) return iter->second;

  ProtoConstraint *constraint = new ProtoConstraint{
      nullptr, field_descriptor, ConstraintType::kHas, nullptr, 0, depends_on};
  has_constraints_.emplace(field_descriptor->full_name(), constraint);
  return constraint;
}

// This function returns a string with properly indented C++ code for the
// setters for this instruction. The '$' is used instead of the message name.
void ProtoInstructionEncoding::GenerateSetterCode() {
  const int kIndent = 0;
  if (setter_map_.empty()) return;
  absl::StrAppend(&setter_code_, "/* setters for ", name(), " */\n");
  // First need to group setters by dependencies on fields, split into setters
  // with if_not and those without (except for those with no depends_on.).
  // Also need to group constraints by their dependencies. Use multimap that
  // maps from a constraint to those that depend on it.
  absl::btree_multimap<const ProtoConstraint *, const ProtoConstraint *>
      grouped_constraints;
  // Maintain a set of inserted constraints, so that the multimap has no
  // duplicate key-value pairs.
  absl::flat_hash_set<const ProtoConstraint *> inserted_constraints;
  // This set contains the top level constraints that do not depend on any
  // other constraints, and thus are the beginning of the 'dependence chains'.
  absl::flat_hash_set<const ProtoConstraint *> constraint_tops;
  // These multimaps map from a constraint to the set of setters dependent on
  // that constraint.
  absl::btree_multimap<const ProtoConstraint *, const ProtoSetter *>
      grouped_setters;
  absl::btree_multimap<const ProtoConstraint *, const ProtoSetter *>
      grouped_if_not_setters;

  // Lambda used to determine if a constraint is already satisfied by
  // an identical constraint used in the decoding of the instruction.
  auto is_in_eq_constraints = [&](const ProtoConstraint *constraint) {
    auto *field_descriptor = constraint->field_descriptor;
    auto iter = std::find_if(
        equal_constraints_.begin(), equal_constraints_.end(),
        [&field_descriptor](const ProtoConstraint *constraint) {
          return (constraint->op == ConstraintType::kHas) &&
                 (constraint->field_descriptor == field_descriptor);
        });
    return (iter != equal_constraints_.end());
  };

  // First build up the data structures.
  // Iterate over the setters for this instruction.
  for (auto &[name, setter_ptr] : setter_map_) {
    // Get any one_of dependency that the setter depends on.
    auto *depends = setter_ptr->depends_on;
    // If the dependency matches one in the equal constraints for decoding the
    // instruction, it will be true for the setters, and does not have to be
    // tested for again.
    if (depends != nullptr && is_in_eq_constraints(depends)) depends = nullptr;

    // Group the setter in a multimap that maps from a constraint to a
    // dependent constraint. There are two sets, depending on whether the setter
    // has an if_not clause or not. Setters with null dependency are inserted in
    // the regular group regardless of if_not value.
    if (depends != nullptr && setter_ptr->if_not != nullptr) {
      grouped_if_not_setters.emplace(depends, setter_ptr);
    } else {
      grouped_setters.emplace(depends, setter_ptr);
    }
    // If there is no one_of dependency, or the setter has an if_not,
    // go to the next setter.
    if (depends == nullptr) continue;
    if (setter_ptr->if_not != nullptr) continue;

    // Add dependency 'links' to the grouped_constraints map, that map
    // from a constraint to the constraints that depend on it.
    while (depends != nullptr && depends->depends_on != nullptr) {
      // See if 'depends' has been inserted yet, if not, add a map entry from
      // the one_of it depends on to it in the grouped_constraints multi map.
      auto [unused, inserted] = inserted_constraints.insert(depends);
      if (inserted) {
        grouped_constraints.emplace(depends->depends_on, depends);
      }
      // Go to the next dependency in the chain, but make sure to skip any
      // that are satisfied by the equal constraints.
      depends = depends->depends_on;
    }
    // Maintain a set of the the top level constraints (which do not depend
    // on other constraints).
    constraint_tops.insert(depends);
  }

  // Helper lambda functions used in the loop nest below.
  // This generates the assignment.
  auto assign = [&](int indent, const ProtoSetter *setter) {
    absl::StrAppend(&setter_code_, std::string(indent, ' '), "decoder->Set",
                    ToPascalCase(setter->name), "($.");
    auto field_name = setter->ctx->qualified_ident()->getText();
    // Need to convert from a.b.c to a().b().c().
    auto call =
        absl::StrCat(absl::StrReplaceAll(field_name, {{".", "()."}}), "()");
    if (setter->if_not == nullptr) {
      absl::StrAppend(&setter_code_, call, "); // ",
                      setter->field_descriptor->full_name(), "\n");
    } else {
      auto pos = call.find_last_of('.');
      std::string name;
      if (pos != std::string::npos) {
        std::string prefix = call.substr(0, pos + 1);
        name = prefix + "has_" + call.substr(pos + 1);
      } else {
        name = "has_" + call;
      }
      std::string txt = setter->if_not->value() != nullptr
                            ? setter->if_not->value()->getText()
                            : setter->if_not->qualified_ident()->getText();
      absl::StrAppend(&setter_code_, name, " ? $.", call, " : ", txt, ");\n");
    }
  };

  // First process the setters with no oneof dependencies.
  auto [begin, end] = grouped_setters.equal_range(nullptr);
  for (auto iter = begin; iter != end; ++iter) assign(kIndent, iter->second);

  // Helper lambda function to generate the if statement to guard individual
  // setters.
  auto generate_if_statement = [&](int indent,
                                   const ProtoConstraint *constraint) {
    auto *desc = constraint->field_descriptor;
    auto *oneof = desc->containing_oneof();
    absl::StrAppend(&setter_code_, std::string(indent, ' '), "if ($.");
    if (oneof != nullptr) {
      absl::StrAppend(&setter_code_, oneof->name(),
                      "_case() == ", ToPascalCase(oneof->name()), "Case::k",
                      ToPascalCase(desc->name()), ") {\n");
    } else {
      absl::StrAppend(&setter_code_, "has_", desc->name(), ") {\n");
    }
  };

  // Recursive lambda for generating nested if statements around groups of
  // setters with the same constraint.
  std::function<void(int, const ProtoConstraint *)> generate_nested_ifs =
      [&](int indent, const ProtoConstraint *constraint) {
        // Generate if statement for 'constraint'.
        generate_if_statement(indent, constraint);
        indent += 2;
        // Perform all the assigns that depend on constraint.
        {
          auto [begin, end] = grouped_setters.equal_range(constraint);
          for (auto iter = begin; iter != end; ++iter)
            assign(indent, iter->second);
        }
        // Generate any ifs for constraints dependent on the current constraint.
        {
          auto [begin, end] = grouped_constraints.equal_range(constraint);
          for (auto iter = begin; iter != end; ++iter) {
            generate_nested_ifs(indent, iter->second);
          }
        }
        indent -= 2;
        absl::StrAppend(&setter_code_, std::string(indent, ' '), "}\n");
      };

  // Process the setters with no if_not's.
  for (auto *constraint : constraint_tops) {
    generate_nested_ifs(kIndent, constraint);
  }

  // Recursive lambda for generating the conditions of the if statements used
  // by setters with 'if_not' constructs.
  std::function<void(const ProtoConstraint *, std::string &)>
      recursive_if_conditions =
          [&](const ProtoConstraint *constraint, std::string &if_conditions) {
            auto *desc = constraint->field_descriptor;
            auto *depends_on = constraint->depends_on;
            std::string sep = "";
            // Generate the conditions in reverse order of the depends_on list.
            if (depends_on != nullptr) {
              recursive_if_conditions(depends_on, if_conditions);
              if (!if_conditions.empty()) sep = " && ";
            }
            auto *oneof = desc->containing_oneof();
            std::string ident = constraint->ctx->qualified_ident()->getText();
            auto pos = ident.find_last_of('.');
            std::string prefix;
            if (pos != std::string::npos) {
              prefix = ident.substr(0, pos + 1);
            }
            if (!prefix.empty()) {
              // Convert a.b.c to a().b().c().
              prefix = absl::StrReplaceAll(prefix, {{".", "()."}});
            }
            if (oneof != nullptr) {
              absl::StrAppend(&if_conditions, sep, "($.", prefix, oneof->name(),
                              "_case() == k", ToPascalCase(desc->name()), ")");
            } else {
              absl::StrAppend(&if_conditions, sep, "($.", prefix, "has_",
                              desc->name(), ")");
            }
          };

  // Process the setters with dependencies and if_not's.
  for (auto iter = grouped_if_not_setters.begin();
       iter != grouped_if_not_setters.end();
       /*empty increment expression - it's done inside nested loop below*/) {
    std::string if_conditions;
    recursive_if_conditions(iter->first, if_conditions);
    absl::StrAppend(&setter_code_, std::string(kIndent, ' '), "if (",
                    if_conditions, ") {\n");
    auto count = grouped_if_not_setters.count(iter->first);
    for (auto i = 0; i < count; ++i) {
      assign(kIndent + 2, iter->second);
      ++iter;
    }
    absl::StrAppend(&setter_code_, std::string(kIndent, ' '), "}\n");
  }
}

std::string ProtoInstructionEncoding::GetSetterCode(
    absl::string_view message_name, int indent) const {
  return absl::StrReplaceAll(
      setter_code_,
      {{"$", message_name}, {"\n", "\n" + std::string(indent, ' ')}});
}

}  // namespace proto_fmt
}  // namespace decoder
}  // namespace sim
}  // namespace mpact
