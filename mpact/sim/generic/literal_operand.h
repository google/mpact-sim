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

#ifndef MPACT_SIM_GENERIC_LITERAL_OPERAND_H_
#define MPACT_SIM_GENERIC_LITERAL_OPERAND_H_

#include <any>
#include <cstdint>
#include <string>
#include <vector>

#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "mpact/sim/generic/operand_interface.h"

// Literal operands differ from the immediate operands in that it not only
// represents a constant value in the simulated instruction such as an address
// offset or immediate arithmetic operand, but actually represents an
// "immediate" value that is a compile-time constant. This may seem like a
// small difference, but if an instruction immediate value is 32-bits long, it
// is beyond impractical to switch between 2^32 possible literal operands to
// select from, since each such operand has to be instantiated separately
// in the code, as they are compile time constants. However, for a constant
// true/false predicate this is perfectly reasonable. Even for small sets of
// pre-defined instruction immediate values this can be useful. Using literal
// operands can make a difference in performance when the operand is frequently
// accessed, such as an "always" (true) or "never" (false) predicate for
// simulated instructions.
namespace mpact {
namespace sim {
namespace generic {

// Boolean literal predicate operand.
template <bool literal>
class BoolLiteralPredicateOperand : public PredicateOperandInterface {
 public:
  BoolLiteralPredicateOperand() = default;
  bool Value() override { return literal; }
  std::string AsString() const override { return ""; }
};

// Boolean literal operand.
template <bool literal>
class BoolLiteralOperand : public SourceOperandInterface {
 public:
  BoolLiteralOperand() : as_string_(absl::StrCat(literal)) {}
  BoolLiteralOperand(absl::string_view as_string) : as_string_(as_string) {}
  BoolLiteralOperand(const std::vector<int> &shape, absl::string_view as_string)
      : shape_(shape), as_string_(as_string) {}
  explicit BoolLiteralOperand(const std::vector<int> &shape)
      : BoolLiteralOperand(shape, absl::StrCat(literal)) {}

  // Methods for accessing the literal value. Always returns the same
  // value regardless of the index parameter.
  bool AsBool(int) override { return literal; }
  int8_t AsInt8(int) override { return static_cast<int8_t>(literal); }
  uint8_t AsUint8(int) override { return static_cast<uint8_t>(literal); }
  int16_t AsInt16(int) override { return static_cast<int16_t>(literal); }
  uint16_t AsUint16(int) override { return static_cast<uint16_t>(literal); }
  int32_t AsInt32(int) override { return static_cast<int32_t>(literal); }
  uint32_t AsUint32(int) override { return static_cast<uint32_t>(literal); }
  int64_t AsInt64(int) override { return static_cast<int64_t>(literal); }
  uint64_t AsUint64(int) override { return static_cast<uint64_t>(literal); }

  // Returns empty absl::any, as the literal operand does not have an
  // underlying object that models any processor state.
  std::any GetObject() const override { return std::any(); }

  // Return the shape of the operand (the number of elements in each dimension).
  // For instance {0} indicates a scalar quantity, whereas {128} indicates an
  // 128 element vector quantity.
  std::vector<int> shape() const override { return shape_; }

  std::string AsString() const override { return as_string_; }

 private:
  std::string as_string_;
  std::vector<int> shape_;
};

// Integer valued literal operand.
template <int literal>
class IntLiteralOperand : public SourceOperandInterface {
 public:
  IntLiteralOperand() : as_string_(absl::StrCat(literal)) {};
  IntLiteralOperand(absl::string_view as_string) : as_string_(as_string) {}
  IntLiteralOperand(const std::vector<int> &shape, absl::string_view as_string)
      : shape_(shape), as_string_(as_string) {}
  explicit IntLiteralOperand(const std::vector<int> &shape)
      : IntLiteralOperand(shape, absl::StrCat(literal)) {}

  // Methods for accessing the immediate value. Always returns the same
  // value regardless of the index parameter.
  bool AsBool(int) override { return static_cast<bool>(literal); }
  int8_t AsInt8(int) override { return static_cast<int8_t>(literal); }
  uint8_t AsUint8(int) override { return static_cast<uint8_t>(literal); }
  int16_t AsInt16(int) override { return static_cast<int16_t>(literal); }
  uint16_t AsUint16(int) override { return static_cast<uint16_t>(literal); }
  int32_t AsInt32(int) override { return static_cast<int32_t>(literal); }
  uint32_t AsUint32(int) override { return static_cast<uint32_t>(literal); }
  int64_t AsInt64(int) override { return static_cast<int64_t>(literal); }
  uint64_t AsUint64(int) override { return static_cast<uint64_t>(literal); }

  // Returns empty absl::any, as the literal operand does not have an
  // underlying object that models any processor state.
  std::any GetObject() const override { return std::any(); }

  // Return the shape of the operand (the number of elements in each dimension).
  // For instance {0} indicates a scalar quantity, whereas {128} indicates an
  // 128 element vector quantity.
  std::vector<int> shape() const override { return shape_; }

  std::string AsString() const override { return as_string_; }

 private:
  std::vector<int> shape_;
  std::string as_string_;
};

}  // namespace generic
}  // namespace sim
}  // namespace mpact

#endif  // MPACT_SIM_GENERIC_LITERAL_OPERAND_H_
