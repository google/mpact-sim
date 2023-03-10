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

#ifndef MPACT_SIM_GENERIC_STATE_ITEM_H_
#define MPACT_SIM_GENERIC_STATE_ITEM_H_

#include <string>
#include <type_traits>

#include "absl/strings/string_view.h"
#include "mpact/sim/generic/operand_interface.h"

namespace mpact {
namespace sim {
namespace generic {

// Forward declarations
// ArchState is a class that holds the architectural (processor) state.
// This class is not used in the StateItem class, other than passing on a
// pointer to the BaseType. Even in the BaseClass it is only used as a
// property with a getter function.
class ArchState;

// This file contains a templated helper class used to create types for
// registers and other machine state. As such, this class doesn't have its
// own test class, but is effectively tested where those resulting types
// are defined, e.g., test/register_test.cc for register.{h,cc} where the
// register types are defined.

// Variadically templated base class for machine state classes. The
// templatization is done to enable creation of scalar, vector and array
// value typed instances of a base class such as a register or fifo.
// The base template cannot be instantiated since the constructor is
// deleted, only the partially specialized versions of the template can be
// instantiated.
template <typename BaseType, typename ElementType, typename SourceOperandType,
          typename DestinationOperandType, int... Ns>
class StateItem : public BaseType {
 public:
  StateItem() = delete;
};

// The following partial specializations of the State class are used to
// model scalar, vector and array structured values. Each has a templated
// constructor that takes a name and an optional pack of additional
// parameters. The constructor calls the BaseType class constructor
// with the name, order of the state based on scalar value, vector or array
// size, the size of the unit type and the pack of additional parameters.

// Partial specialization for a scalar state class of ElementType.
template <typename BaseType, typename ElementType, typename SourceOperandType,
          typename DestinationOperandType>
class StateItem<BaseType, ElementType, SourceOperandType,
                DestinationOperandType> : public BaseType {
 public:
  using ValueType = ElementType;
  template <typename... Ps>
  explicit StateItem(ArchState *arch_state, absl::string_view name, Ps... pargs)
      : BaseType(arch_state, name, {1}, sizeof(ElementType), pargs...) {}
  virtual SourceOperandInterface *CreateSourceOperand() {
    return new SourceOperandType(this);
  }
  virtual DestinationOperandInterface *CreateDestinationOperand(int latency) {
    return new DestinationOperandType(this, latency);
  }
  virtual SourceOperandInterface *CreateSourceOperand(std::string op_name) {
    return new SourceOperandType(this, op_name);
  }
  virtual DestinationOperandInterface *CreateDestinationOperand(
      int latency, std::string op_name) {
    return new DestinationOperandType(this, latency, op_name);
  }
};

// Partial specialization for a longer "scalar" register using an array of
// element type. The element type should be uint8_t (so it is a byte array).
template <typename BaseType, typename ElementType, typename SourceOperandType,
          typename DestinationOperandType>
class StateItem<BaseType, ElementType *, SourceOperandType,
                DestinationOperandType> : public BaseType {
 public:
  using ValueType = ElementType;
  template <typename... Ps>
  explicit StateItem(ArchState *arch_state, absl::string_view name, int width,
                     Ps... pargs)
      : BaseType(arch_state, name, {width}, sizeof(ElementType), pargs...) {
    static_assert(std::is_same<ElementType, uint8_t>::value,
                  "Element type must be 'uint8_t'");
  }
  virtual SourceOperandInterface *CreateSourceOperand() {
    return new SourceOperandType(this);
  }
  virtual SourceOperandInterface *CreateSourceOperand(std::string op_name) {
    return new SourceOperandType(this, op_name);
  }
  virtual DestinationOperandInterface *CreateDestinationOperand(
      int latency, std::string op_name) {
    return new DestinationOperandType(this, latency, op_name);
  }
  virtual DestinationOperandInterface *CreateDestinationOperand(int latency) {
    return new DestinationOperandType(this, latency);
  }
};

// Specialization used when there is no Destination operand type (i.e., it is
// void).
template <typename BaseType, typename ElementType, typename SourceOperandType>
class StateItem<BaseType, ElementType, SourceOperandType, void>
    : public BaseType {
 public:
  using ValueType = ElementType;
  template <typename... Ps>
  explicit StateItem(ArchState *arch_state, absl::string_view name, Ps... pargs)
      : BaseType(arch_state, name, {1}, sizeof(ElementType), pargs...) {}
  virtual SourceOperandInterface *CreateSourceOperand() {
    return new SourceOperandType(this);
  }
  virtual SourceOperandInterface *CreateSourceOperand(std::string op_name) {
    return new SourceOperandType(this, op_name);
  }
};

// Partial specialization for a N length vector state class of ElementType.
template <typename BaseType, typename ElementType, typename SourceOperandType,
          typename DestinationOperandType, int N>
class StateItem<BaseType, ElementType, SourceOperandType,
                DestinationOperandType, N> : public BaseType {
 public:
  using ValueType = ElementType;
  static constexpr int kNumDimensions = 1;
  static constexpr int kShape[] = {N};
  template <typename... Ps>
  explicit StateItem(ArchState *arch_state, absl::string_view name, Ps... pargs)
      : BaseType(arch_state, name, {N}, sizeof(ElementType), pargs...) {}
  // Source/destination operand creation interface.
  virtual SourceOperandInterface *CreateSourceOperand() {
    return new SourceOperandType(this);
  }
  virtual DestinationOperandInterface *CreateDestinationOperand(int latency) {
    return new DestinationOperandType(this, latency);
  }
  virtual SourceOperandInterface *CreateSourceOperand(std::string op_name) {
    return new SourceOperandType(this, op_name);
  }
  virtual DestinationOperandInterface *CreateDestinationOperand(
      int latency, std::string op_name) {
    return new DestinationOperandType(this, latency, op_name);
  }
};

// Partial specialization for a MxN size matrix state class of ElementType.
template <typename BaseType, typename ElementType, typename SourceOperandType,
          typename DestinationOperandType, int M, int N>
class StateItem<BaseType, ElementType, SourceOperandType,
                DestinationOperandType, M, N> : public BaseType {
 public:
  using ValueType = ElementType;
  static constexpr int kNumDimensions = 2;
  static constexpr int kShape[] = {M, N};
  template <typename... Ps>
  explicit StateItem(ArchState *arch_state, absl::string_view name, Ps... pargs)
      : BaseType(arch_state, name, {M, N}, sizeof(ElementType), pargs...) {}
  virtual SourceOperandInterface *CreateSourceOperand() {
    return new SourceOperandType(this);
  }
  virtual DestinationOperandInterface *CreateDestinationOperand(int latency) {
    return new DestinationOperandType(this, latency);
  }
  virtual SourceOperandInterface *CreateSourceOperand(std::string op_name) {
    return new SourceOperandType(this, op_name);
  }
  virtual DestinationOperandInterface *CreateDestinationOperand(
      int latency, std::string op_name) {
    return new DestinationOperandType(this, latency, op_name);
  }
};

}  // namespace generic
}  // namespace sim
}  // namespace mpact

#endif  // MPACT_SIM_GENERIC_STATE_ITEM_H_
