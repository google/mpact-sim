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

#ifndef MPACT_SIM_DECODER_OPCODE_H_
#define MPACT_SIM_DECODER_OPCODE_H_

#include <functional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "absl/container/btree_set.h"
#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "mpact/sim/decoder/format_name.h"
#include "mpact/sim/decoder/resource.h"
#include "mpact/sim/decoder/template_expression.h"

namespace mpact {
namespace sim {
namespace machine_description {
namespace instruction_set {

// An instance of the Opcode class represents an individual instruction opcode
// in the instruction set. The Opcode name has to be unique within the
// instruction set. In addition to having a name, the opcode also has an
// optional predicate operand name, a (possibly empty) list of source operand
// names, and a (possibly empty) list of destination operand names.
//
// Opcodes are created using the OpcodeFactory factory class. Each opcode is
// assigned a unique (within the OpcodeFactory) value that is used to define
// the value of the corresponding class enum entry in the generated code. This
// value is unrelated to any value of the "opcode" field in the instruction
// encoding.
class DestinationOperand {
 public:
  // Operand latency is defined by the expression.
  DestinationOperand(std::string name, bool is_array,
                     TemplateExpression *expression)
      : name_(std::move(name)),
        pascal_case_name_(ToPascalCase(name_)),
        expression_(expression),
        is_array_(is_array) {}
  // Operand latency is a constant.
  DestinationOperand(std::string name, bool is_array, int latency)
      : name_(std::move(name)),
        pascal_case_name_(ToPascalCase(name_)),
        expression_(new TemplateConstant(latency)),
        is_array_(is_array) {}
  // This constructor is used when the destination operand latency is specified
  // as '*' - meaning that it will be computed at the time of decode.
  explicit DestinationOperand(std::string name, bool is_array)
      : name_(std::move(name)),
        pascal_case_name_(ToPascalCase(name_)),
        expression_(nullptr),
        is_array_(is_array) {}
  ~DestinationOperand() { delete expression_; }

  const std::string &name() const { return name_; }
  const std::string &pascal_case_name() const { return pascal_case_name_; }
  TemplateExpression *expression() const { return expression_; }
  bool is_array() const { return is_array_; }
  bool HasLatency() const { return expression_ != nullptr; }
  absl::StatusOr<int> GetLatency() const {
    if (expression_ == nullptr) return -1;
    auto res = expression_->GetValue();
    if (!res.ok()) {
      return absl::InternalError(absl::StrCat(
          "Template expression evaluation error", res.status().message()));
    }
    auto variant_value = res.value();
    auto *value_ptr = std::get_if<int>(&variant_value);
    if (value_ptr == nullptr) {
      return absl::InternalError("Template expression type error");
    }
    return *value_ptr;
  }

 private:
  std::string name_;
  std::string pascal_case_name_;
  TemplateExpression *expression_;
  bool is_array_ = false;
};

struct SourceOperand {
  std::string name;
  bool is_array;
  SourceOperand(std::string name_, bool is_array_)
      : name(std::move(name_)), is_array(is_array_) {}
};

// This struct is used to specify the location of an operand within an
// instruction. It specifies which instruction (or child instruction) number. In
// this case, 0 is the top level instruction, 1 is the first child instruction
// etc. The type is 'p' for predicate operand, 's' for source operand, and 'd'
// for destination operand. The instance number specifies the entry index in the
// source or destination operand vector.
struct OperandLocator {
  int op_spec_number;
  char type;
  int instance;
  OperandLocator(int op_spec_number_, char type_, int instance_)
      : op_spec_number(op_spec_number_), type(type_), instance(instance_) {}
};

struct FormatInfo {
  FormatInfo() = default;
  // Use default copy constructor.
  FormatInfo(const FormatInfo &) = default;
  std::string op_name;
  bool is_formatted = true;
  std::string number_format;
  bool use_address = false;
  std::string operation;
  bool do_left_shift = false;
  int shift_amount = 0;
};

struct DisasmFormat {
  DisasmFormat() = default;
  // Copy constructor.
  DisasmFormat(const DisasmFormat &df) {
    width = df.width;
    for (auto const &frag : df.format_fragment_vec) {
      format_fragment_vec.push_back(frag);
    }
    for (auto const *info : df.format_info_vec) {
      format_info_vec.push_back(new FormatInfo(*info));
    }
  }
  // Destructor.
  ~DisasmFormat() {
    for (auto *info : format_info_vec) {
      delete info;
    }
    format_info_vec.clear();
  }
  int width = 0;
  std::vector<std::string> format_fragment_vec;
  std::vector<FormatInfo *> format_info_vec;
};

struct ResourceReference {
  Resource *resource;
  bool is_array;
  DestinationOperand *dest_op;
  TemplateExpression *begin_expression;
  TemplateExpression *end_expression;
  ResourceReference(Resource *resource_, bool is_array_,
                    DestinationOperand *dest_op_,
                    TemplateExpression *begin_expr_,
                    TemplateExpression *end_expr_)
      : resource(resource_),
        is_array(is_array_),
        dest_op(dest_op_),
        begin_expression(begin_expr_),
        end_expression(end_expr_) {}
  ResourceReference(const ResourceReference &rhs) {
    resource = rhs.resource;
    is_array = rhs.is_array;
    dest_op = rhs.dest_op;
    begin_expression = rhs.begin_expression->DeepCopy();
    end_expression = rhs.end_expression->DeepCopy();
  }
  ~ResourceReference() {
    resource = nullptr;
    dest_op = nullptr;
    delete begin_expression;
    begin_expression = nullptr;
    delete end_expression;
    end_expression = nullptr;
  }
};

using OpLocatorMap = absl::flat_hash_map<std::string, OperandLocator>;

class Opcode {
  friend class OpcodeFactory;

 public:
  // Constructor is private (defined below), only used by OpcodeFactory.
  virtual ~Opcode();

  // Each opcode specifies an optional predicate operand name, an optional list
  // of source operand names, and an optional list of destination operand names.
  // These methods append the source and destination operand names to the
  // opcode. These names are used to create interface methods that are called
  // to get the Predicate, Source and Destination operand interfaces (defined
  // in .../sim/generic/operand_interfaces.h. The implementation of these
  // methods will be left to the user of this generator tool.
  void AppendSourceOp(absl::string_view op_name, bool is_array);
  void AppendDestOp(absl::string_view op_name, bool is_array,
                    TemplateExpression *expression);
  void AppendDestOp(absl::string_view op_name, bool is_array);
  DestinationOperand *GetDestOp(absl::string_view op_name);
  // Append child opcode specification.
  void AppendChild(Opcode *op) { child_ = op; }
  // Checks destination latencies with the given function. Returns true if all
  // comply.
  bool ValidateDestLatencies(const std::function<bool(int)> &validator) const;
  int instruction_size() const { return instruction_size_; }
  void set_instruction_size(int val) { instruction_size_ = val; }
  Opcode *child() const { return child_; }
  Opcode *parent() const { return parent_; }
  const std::string &name() const { return name_; }
  const std::string &pascal_name() const { return pascal_name_; }
  // Each opcode is assigned a unique value that is used in the slot class
  // enum definition.
  int value() const { return value_; }
  // Predicate, source and destination operand names.
  const std::string &predicate_op_name() const { return predicate_op_name_; }
  void set_predicate_op_name(absl::string_view op_name) {
    predicate_op_name_ = op_name;
  }
  const std::vector<SourceOperand> &source_op_vec() const {
    return source_op_vec_;
  }
  const std::vector<DestinationOperand *> &dest_op_vec() const {
    return dest_op_vec_;
  }

  OpLocatorMap &op_locator_map() { return op_locator_map_; }  // not const.
  const OpLocatorMap &op_locator_map() const { return op_locator_map_; }

 private:
  Opcode(absl::string_view name, int value);
  int instruction_size_;
  Opcode *child_ = nullptr;
  Opcode *parent_ = nullptr;
  std::string predicate_op_name_;
  std::vector<SourceOperand> source_op_vec_;
  std::vector<DestinationOperand *> dest_op_vec_;
  absl::flat_hash_map<std::string, DestinationOperand *> dest_op_map_;
  std::string name_;
  std::string pascal_name_;
  std::string semfunc_code_string_;
  int value_;
  OpLocatorMap op_locator_map_;
};

class OpcodeFactory {
 public:
  OpcodeFactory() = default;
  ~OpcodeFactory() = default;

  // If the opcode doesn't yet exist, create a new opcode and return the
  // pointer, otherwise return an error code.
  absl::StatusOr<Opcode *> CreateOpcode(absl::string_view name);
  Opcode *CreateDefaultOpcode();
  Opcode *CreateChildOpcode(Opcode *opcode) const;
  // Duplicate the opcode, but evaluate the destination latency expressions
  // with the template argument expression vector.
  absl::StatusOr<Opcode *> CreateDerivedOpcode(const Opcode *opcode,
                                               TemplateInstantiationArgs *args);
  const std::vector<Opcode *> &opcode_vec() const { return opcode_vec_; }

 private:
  absl::btree_set<std::string> opcode_names_;
  std::vector<Opcode *> opcode_vec_;
  int opcode_value_ = 1;
};

}  // namespace instruction_set
}  // namespace machine_description
}  // namespace sim
}  // namespace mpact

#endif  // MPACT_SIM_DECODER_OPCODE_H_
