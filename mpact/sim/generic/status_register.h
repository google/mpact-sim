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

#ifndef MPACT_SIM_GENERIC_STATUS_REGISTER_H_
#define MPACT_SIM_GENERIC_STATUS_REGISTER_H_

#include <any>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "absl/functional/any_invocable.h"
#include "mpact/sim/generic/data_buffer.h"
#include "mpact/sim/generic/operand_interface.h"
#include "mpact/sim/generic/state_item.h"
#include "mpact/sim/generic/state_item_base.h"

// This file declares a class that implements a generic read-only status
// register.

namespace mpact {
namespace sim {
namespace generic {

// The Status register class implements a read-only view of conditions that
// are evaluated dynamically when the register is read. Each bit is computed
// by calling a bool() function associated with each bit position in the value
// type. If the function returns true, the bit is set to 1, otherwise it is a 0.
// By default the functions are initialized to return false (i.e., set the bit
// to 0). The default functions can be overridden by calling the
// SetEvaluateFunction for individual bit positions with the appropriate
// evaluation functions (including lambdas).

template <typename T>
class StatusRegisterBase : public StateItemBase {
  static_assert(std::is_integral<T>::value, "Type must be integral");

 public:
  using Evaluate = absl::AnyInvocable<bool()>;
  // The constructor is in the protected section below. Others are deleted.
  StatusRegisterBase() = delete;
  StatusRegisterBase(const StatusRegisterBase &) = delete;
  StatusRegisterBase &operator=(const StatusRegisterBase &) = delete;

  T Read();
  T Read(T mask);

  void SetDataBuffer(DataBuffer *db) override {
    // Read only state item, so just decrement the ref count and ignore.
    db->DecRef();
  }

  void SetEvaluateFunction(int position, Evaluate eval) {
    evaluate_[position] = std::move(eval);
  }

 protected:
  // The constructor is called from StateItem<>
  StatusRegisterBase(ArchState *state, absl::string_view name,
                     const std::vector<int> &shape, int unit_size);

 private:
  std::vector<Evaluate> evaluate_;
  int size_;
};

template <typename T>
StatusRegisterBase<T>::StatusRegisterBase(ArchState *state,
                                          absl::string_view name,
                                          const std::vector<int> &shape,
                                          int unit_size)
    : StateItemBase(state, name, shape, unit_size), size_(sizeof(T) * 8) {
  // Initialize the evaluate functions to lambdas that return false.
  for (int i = 0; i < size_; i++) {
    evaluate_.push_back([]() -> bool { return false; });
  }
}

// Call the evaluation function for each bit position, starting at the high
// (msb) position.
template <typename T>
T StatusRegisterBase<T>::Read() {
  T value = 0;
  for (int i = size_ - 1; i >= 0; i--) {
    value <<= 1;
    value |= (evaluate_[i]() ? 1 : 0);
  }
  return value;
}

// Call the evaluation function only for the bits that are set in the mask,
// starting at the (msb) position.
template <typename T>
T StatusRegisterBase<T>::Read(T mask) {
  T value = 0;
  for (int i = size_ - 1; i >= 0; i--) {
    value <<= 1;
    if (mask & (static_cast<T>(1) << i)) {
      value |= (evaluate_[i]() ? 1 : 0);
    }
  }
  return value;
}

// The source operand type for StatusRegisters.
template <typename T>
class StatusRegisterSourceOperand : public SourceOperandInterface {
 public:
  // Construtor. Note, default constructor deleted.
  StatusRegisterSourceOperand(StatusRegisterBase<T> *status_reg,
                              std::string op_name)
      : status_register_(status_reg), op_name_(op_name) {}
  explicit StatusRegisterSourceOperand(StatusRegisterBase<T> *status_reg)
      : StatusRegisterSourceOperand(status_reg, status_reg->name()) {}
  StatusRegisterSourceOperand() = delete;

  // Return the value cast to the given type.
  bool AsBool(int i) override {
    return static_cast<bool>(status_register_->Read());
  }
  int8_t AsInt8(int i) override {
    return static_cast<int8_t>(status_register_->Read());
  }
  uint8_t AsUint8(int i) override {
    return static_cast<uint8_t>(status_register_->Read());
  }
  int16_t AsInt16(int i) override {
    return static_cast<int16_t>(status_register_->Read());
  }
  uint16_t AsUint16(int i) override {
    return static_cast<uint16_t>(status_register_->Read());
  }
  int32_t AsInt32(int i) override {
    return static_cast<int32_t>(status_register_->Read());
  }
  uint32_t AsUint32(int i) override {
    return static_cast<uint32_t>(status_register_->Read());
  }
  int64_t AsInt64(int i) override {
    return static_cast<int64_t>(status_register_->Read());
  }
  uint64_t AsUint64(int i) override {
    return static_cast<uint64_t>(status_register_->Read());
  }

  // Return the register object.
  std::any GetObject() const override { return std::any(status_register_); }

  // Returns the shape of the register.
  std::vector<int> shape() const override { return status_register_->shape(); }

  std::string AsString() const override { return op_name_; }

 private:
  StatusRegisterBase<T> *status_register_;
  std::string op_name_;
};

// Adding a deduction guide, so that the StatusRegisterSourceOperand can be
// used without template arguments, deducing the type from the constructor
// argument instead.
template <typename T>
StatusRegisterSourceOperand(StatusRegisterBase<T> *)
    -> StatusRegisterSourceOperand<T>;

template <typename ElementType>
using StatusRegister =
    StateItem<StatusRegisterBase<ElementType>, ElementType,
              StatusRegisterSourceOperand<ElementType>, void>;

}  // namespace generic
}  // namespace sim
}  // namespace mpact

#endif  // MPACT_SIM_GENERIC_STATUS_REGISTER_H_
