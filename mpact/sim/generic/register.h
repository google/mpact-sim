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

#ifndef MPACT_SIM_GENERIC_REGISTER_H_
#define MPACT_SIM_GENERIC_REGISTER_H_

// Contains the basic generic definitions for classes used to model
// register state in simulated architectures, as in the kind of registers
// used in instruction visible register files. Registers can be scalar,
// one dimensional vector, or two dimensional array with a base value type.

#include <any>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "absl/strings/string_view.h"
#include "mpact/sim/generic/arch_state.h"
#include "mpact/sim/generic/data_buffer.h"
#include "mpact/sim/generic/operand_interface.h"
#include "mpact/sim/generic/signed_type.h"
#include "mpact/sim/generic/state_item.h"
#include "mpact/sim/generic/state_item_base.h"

namespace mpact {
namespace sim {
namespace generic {

class ArchState;
class RegisterBase;
class SimpleResource;

// A register source operand with value type T used to access (read/write)
// register values from instruction semantic functions.
template <typename T>
class RegisterSourceOperand : public SourceOperandInterface {
 public:
  // Constructor. Note, default constructor deleted.
  RegisterSourceOperand(RegisterBase *reg, const std::string op_name);
  explicit RegisterSourceOperand(RegisterBase *reg);

  RegisterSourceOperand() = delete;

  // Accessor methods call into the register_ data_buffer to obtain the values.
  // The index is in one dimension. For scalar registers it should always be
  // zero, for vector registers it is the element index. For higher order
  // register shapes it should be the linearized row-major order index.
  // In order to properly cast signed values, an internal helper template
  // is used to get the signed type equivalent of any unsigned register unit
  // value type.
  bool AsBool(int i) final;
  int8_t AsInt8(int i) final;
  uint8_t AsUint8(int i) final;
  int16_t AsInt16(int i) final;
  uint16_t AsUint16(int i) final;
  int32_t AsInt32(int i) final;
  uint32_t AsUint32(int i) final;
  int64_t AsInt64(int i) final;
  uint64_t AsUint64(int i) final;
  // Returns the RegisterBase object wrapped in absl::any.
  std::any GetObject() const override { return std::any(register_); }
  // Non-inherited method to get the register object.
  RegisterBase *GetRegister() const { return register_; }
  // Returns the shape of the register.
  std::vector<int> shape() const override;

  std::string AsString() const override { return op_name_; }

 private:
  RegisterBase *register_;
  std::string op_name_;
};

// Register destination operand type with element value type T. It is agnostic
// of the actual structure of the underlying register (scalar, vector, matrix).
template <typename T>
class RegisterDestinationOperand : public DestinationOperandInterface {
 public:
  // Constructor and Destructor
  RegisterDestinationOperand(RegisterBase *reg, int latency);
  RegisterDestinationOperand(RegisterBase *reg, int latency,
                             std::string op_name);
  RegisterDestinationOperand() = delete;

  // Initializes the DataBuffer instance so that when Submit is called, it can
  // be entered into the correct delay line, with the correct latency, targeting
  // the correct register.
  void InitializeDataBuffer(DataBuffer *db) override;

  // Allocates and returns an initialized DataBuffer instance that contains a
  // copy of the current value of the register. This is useful when only part
  // of the destination register will be modified.
  DataBuffer *CopyDataBuffer() override;

  // Allocates and returns an initialized DataBuffer instance.
  DataBuffer *AllocateDataBuffer() final;

  // Returns the latency associated with writes to this register operand.
  int latency() const override { return latency_; }

  // Returns the RegisterBase object wrapped in absl::any.
  std::any GetObject() const override { return std::any(register_); }
  // Non-inherited method to get the register object.
  RegisterBase *GetRegister() const { return register_; }

  // Returns the shape of the underlying register (the number of elements in
  // each dimension). For instance {0} indicates a scalar quantity, whereas
  // {128} indicates an 128 element vector quantity.
  std::vector<int> shape() const override;

  std::string AsString() const override { return op_name_; }

 private:
  RegisterBase *register_;
  DataBufferFactory *db_factory_;
  int latency_;
  DataBufferDelayLine *delay_line_;
  std::string op_name_;
};

// Base class for register types with the DataBufferDestination interface.
// No default constructor - should only be constructed/destructed from the
// derived classes.
class RegisterBase : public StateItemBase {
 public:
  // Type alias for the notification callback function type.
  using UpdateCallbackFunction = std::function<void()>;
  // Constructors are only called from derived classes and are in the
  // protected section below.
  ~RegisterBase() override;
  RegisterBase() = delete;
  RegisterBase(const RegisterBase &) = delete;
  RegisterBase &operator=(const RegisterBase &) = delete;

  // DecRef's the current data buffer and replaces it with a new one.
  void SetDataBuffer(DataBuffer *db) override;
  // Returns a pointer to the DataBuffer that contains the current value of
  // the register.
  DataBuffer *data_buffer() const { return data_buffer_; }

 protected:
  RegisterBase(ArchState *state, absl::string_view name,
               const std::vector<int> &shape, int unit_size);

 private:
  DataBuffer *data_buffer_;
  std::vector<UpdateCallbackFunction> next_update_callbacks_;
};

// A register class that frees a SimpleResource instance when written to. This
// is intended to be used in modeling dynamic stalls/hold issue due to data
// dependencies on long latency operations with a protected pipeline.
class ReservedRegisterBase : public RegisterBase {
 public:
  ReservedRegisterBase() = delete;
  ReservedRegisterBase(const ReservedRegisterBase &) = delete;
  ReservedRegisterBase &operator=(const ReservedRegisterBase &) = delete;

  // Override the SetDataBuffer to release the SimpleResource instance when
  // called.
  void SetDataBuffer(DataBuffer *db) override;

  // Accessor.
  SimpleResource *resource() const { return resource_; }

 protected:
  ReservedRegisterBase(ArchState *state, absl::string_view name,
                       const std::vector<int> &shape, int unit_size,
                       SimpleResource *resource);

 private:
  // SimpleResource instance associated with the register.
  SimpleResource *resource_;
};

// Scalar register type with value type ElementType.
template <typename ElementType>
using Register =
    StateItem<RegisterBase, ElementType, RegisterSourceOperand<ElementType>,
              RegisterDestinationOperand<ElementType>>;

// N long vector register type with element value type ElementType.
template <typename ElementType, int N>
using VectorRegister =
    StateItem<RegisterBase, ElementType, RegisterSourceOperand<ElementType>,
              RegisterDestinationOperand<ElementType>, N>;

// MxN matrix register type with element value type ElementType.
template <typename ElementType, int M, int N>
using MatrixRegister =
    StateItem<RegisterBase, ElementType, RegisterSourceOperand<ElementType>,
              RegisterDestinationOperand<ElementType>, M, N>;

// Scalar register type with value type ElementType.
template <typename ElementType>
using ReservedRegister = StateItem<ReservedRegisterBase, ElementType,
                                   RegisterSourceOperand<ElementType>,
                                   RegisterDestinationOperand<ElementType>>;

// N long vector register type with element value type ElementType.
template <typename ElementType, int N>
using ReservedVectorRegister =
    StateItem<ReservedRegisterBase, ElementType,
              RegisterSourceOperand<ElementType>,
              RegisterDestinationOperand<ElementType>, N>;

// MxN matrix register type with element value type ElementType.
template <typename ElementType, int M, int N>
using ReservedMatrixRegister =
    StateItem<ReservedRegisterBase, ElementType,
              RegisterSourceOperand<ElementType>,
              RegisterDestinationOperand<ElementType>, M, N>;

template <typename T>
RegisterSourceOperand<T>::RegisterSourceOperand(RegisterBase *reg,
                                                const std::string op_name)
    : register_(reg), op_name_(op_name) {}

template <typename T>
RegisterSourceOperand<T>::RegisterSourceOperand(RegisterBase *reg)
    : RegisterSourceOperand(reg, reg->name()) {}

template <typename T>
bool RegisterSourceOperand<T>::AsBool(int i) {
  return static_cast<bool>(register_->data_buffer()->Get<T>(i));
}
template <typename T>
int8_t RegisterSourceOperand<T>::AsInt8(int i) {
  return static_cast<int8_t>(
      register_->data_buffer()->Get<typename internal::SignedType<T>::type>(i));
}
template <typename T>
uint8_t RegisterSourceOperand<T>::AsUint8(int i) {
  return static_cast<uint8_t>(register_->data_buffer()->Get<T>(i));
}
template <typename T>
int16_t RegisterSourceOperand<T>::AsInt16(int i) {
  return static_cast<int16_t>(
      register_->data_buffer()->Get<typename internal::SignedType<T>::type>(i));
}
template <typename T>
uint16_t RegisterSourceOperand<T>::AsUint16(int i) {
  return static_cast<uint16_t>(register_->data_buffer()->Get<T>(i));
}
template <typename T>
int32_t RegisterSourceOperand<T>::AsInt32(int i) {
  return static_cast<int32_t>(
      register_->data_buffer()->Get<typename internal::SignedType<T>::type>(i));
}
template <typename T>
uint32_t RegisterSourceOperand<T>::AsUint32(int i) {
  return static_cast<uint32_t>(register_->data_buffer()->Get<T>(i));
}
template <typename T>
int64_t RegisterSourceOperand<T>::AsInt64(int i) {
  return static_cast<int64_t>(
      register_->data_buffer()->Get<typename internal::SignedType<T>::type>(i));
}
template <typename T>
uint64_t RegisterSourceOperand<T>::AsUint64(int i) {
  return static_cast<uint64_t>(register_->data_buffer()->Get<T>(i));
}

template <typename T>
std::vector<int> RegisterSourceOperand<T>::shape() const {
  return register_->shape();
}

template <typename T>
RegisterDestinationOperand<T>::RegisterDestinationOperand(RegisterBase *reg,
                                                          int latency,
                                                          std::string op_name)
    : register_(reg),
      db_factory_(reg->arch_state()->db_factory()),
      latency_(latency),
      delay_line_(reg->arch_state()->data_buffer_delay_line()),
      op_name_(op_name) {}

template <typename T>
RegisterDestinationOperand<T>::RegisterDestinationOperand(RegisterBase *reg,
                                                          int latency)
    : RegisterDestinationOperand(reg, latency, reg->name()) {}

template <typename T>
void RegisterDestinationOperand<T>::InitializeDataBuffer(DataBuffer *db) {
  db->set_destination(register_);
  db->set_latency(latency_);
  db->set_delay_line(delay_line_);
}

template <typename T>
DataBuffer *RegisterDestinationOperand<T>::CopyDataBuffer() {
  DataBuffer *db = db_factory_->MakeCopyOf(register_->data_buffer());
  InitializeDataBuffer(db);
  return db;
}

template <typename T>
DataBuffer *RegisterDestinationOperand<T>::AllocateDataBuffer() {
  DataBuffer *db = db_factory_->Allocate(register_->size());
  InitializeDataBuffer(db);
  return db;
}

template <typename T>
std::vector<int> RegisterDestinationOperand<T>::shape() const {
  return register_->shape();
}
}  // namespace generic
}  // namespace sim
}  // namespace mpact

#endif  // MPACT_SIM_GENERIC_REGISTER_H_
