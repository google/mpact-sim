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

#ifndef MPACT_SIM_GENERIC_IMMEDIATE_OPERAND_H_
#define MPACT_SIM_GENERIC_IMMEDIATE_OPERAND_H_

#include <any>
#include <cstdint>
#include <string>
#include <vector>

#include "absl/base/macros.h"
#include "mpact/sim/generic/operand_interface.h"

namespace mpact {
namespace sim {
namespace generic {

// Immediate source operand with value type T. While the value is a scalar,
// it can be used for a vector or matrix immediate as well, since the index in
// the accessor methods is ignored.
template <typename T>
class ImmediateOperand : public SourceOperandInterface {
 public:
  explicit ImmediateOperand(T val)
      : value_(val), shape_({1}), as_string_(absl::StrCat(val)) {}
  ImmediateOperand(T val, std::string as_string)
      : value_(val), shape_({1}), as_string_(as_string) {}
  ImmediateOperand(T val, const std::vector<int>& shape)
      : value_(val), shape_(shape), as_string_(absl::StrCat(val)) {}
  ImmediateOperand(T val, const std::vector<int>& shape, std::string as_string)
      : value_(val), shape_(shape), as_string_(as_string) {}

  // Methods for accessing the immediate value. Always returns the same
  // value regardless of the index parameter.
  bool AsBool(int) override { return static_cast<bool>(value_); }
  int8_t AsInt8(int) override { return static_cast<int8_t>(value_); }
  uint8_t AsUint8(int) override { return static_cast<uint8_t>(value_); }
  int16_t AsInt16(int) override { return static_cast<int16_t>(value_); }
  uint16_t AsUint16(int) override { return static_cast<uint16_t>(value_); }
  int32_t AsInt32(int) override { return static_cast<int32_t>(value_); }
  uint32_t AsUint32(int) override { return static_cast<uint32_t>(value_); }
  int64_t AsInt64(int) override { return static_cast<int64_t>(value_); }
  uint64_t AsUint64(int) override { return static_cast<uint64_t>(value_); }

  // Returns empty absl::any, as the immediate operand does not have an
  // underlying object that models any processor state.
  std::any GetObject() const override { return std::any(); }

  // Returns the shape of the operand (the number of elements in each
  // dimension). For instance {1} indicates a scalar quantity, whereas {128}
  // indicates an 128 element vector quantity. A 2D 64 by 32 array would have
  // shape {64, 32}.
  // ** Note: in principle a scalar shaped object should be encoded as an
  // empty vector {}, i.e., point with zero dimensions. However, some code was
  // simplified by not allowing an empty vector. Therefore, a 1 dimensional
  // vector shape with dimension size 1 used for a scalar shape.
  std::vector<int> shape() const override { return shape_; }

  std::string AsString() const override { return as_string_; }

 private:
  T value_;
  std::vector<int> shape_;
  std::string as_string_;
};

// Vector Immediate source operand with value type T. This allows each vector
// element to have a different value. It is initialized with a std::vector<T>.
template <typename T>
class VectorImmediateOperand : public SourceOperandInterface {
 public:
  explicit VectorImmediateOperand(std::vector<T> val) : value_(val) {
    shape_.push_back(value_.size());
  }
  VectorImmediateOperand(std::vector<T> val, const std::vector<int>& shape)
      : value_(val), shape_(shape) {
    ABSL_HARDENING_ASSERT((shape.size() == 1) && (shape[0] == val.size()));
  }

  // Methods for accessing the immediate value.
  bool AsBool(int i) override { return static_cast<bool>(value_[i]); }
  int8_t AsInt8(int i) override { return static_cast<int8_t>(value_[i]); }
  uint8_t AsUint8(int i) override { return static_cast<uint8_t>(value_[i]); }
  int16_t AsInt16(int i) override { return static_cast<int16_t>(value_[i]); }
  uint16_t AsUint16(int i) override { return static_cast<uint16_t>(value_[i]); }
  int32_t AsInt32(int i) override { return static_cast<int32_t>(value_[i]); }
  uint32_t AsUint32(int i) override { return static_cast<uint32_t>(value_[i]); }
  int64_t AsInt64(int i) override { return static_cast<int64_t>(value_[i]); }
  uint64_t AsUint64(int i) override { return static_cast<uint64_t>(value_[i]); }

  // Returns empty absl::any, as the immediate operand does not have an
  // underlying object that models any processor state.
  std::any GetObject() const override { return std::any(); }

  // Return the shape of the operand (the number of elements in each dimension).
  // For instance {1} indicates a scalar quantity, whereas {128} indicates an
  // 128 element vector quantity.
  std::vector<int> shape() const override { return shape_; }

  std::string AsString() const override {
    return absl::StrCat("[", value_[0], "...", value_.back(), "]");
  }

 private:
  std::vector<T> value_;
  std::vector<int> shape_;
};

}  // namespace generic
}  // namespace sim
}  // namespace mpact

#endif  // MPACT_SIM_GENERIC_IMMEDIATE_OPERAND_H_
