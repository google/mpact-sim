// Copyright 2025 Google LLC
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

#ifndef MPACT_SIM_GENERIC_WRAPPER_OPERAND_H_
#define MPACT_SIM_GENERIC_WRAPPER_OPERAND_H_

#include <any>
#include <cstdint>
#include <string>
#include <vector>

#include "mpact/sim/generic/data_buffer.h"
#include "mpact/sim/generic/operand_interface.h"

namespace mpact {
namespace sim {
namespace generic {

// Source operand interface for used to wrap a pointer to a value of type T.
// This class is used to provide the means to access the underlying object
// itself using the GetObject() method. None of the other methods are intended
// to be used.
template <typename T>
class WrapperSourceOperand : public SourceOperandInterface {
 public:
  WrapperSourceOperand(T* value, const std::vector<int>& shape)
      : value_(value), shape_(shape) {}
  WrapperSourceOperand() = delete;
  WrapperSourceOperand(const WrapperSourceOperand&) = delete;
  WrapperSourceOperand& operator=(const WrapperSourceOperand&) = delete;
  bool AsBool(int index) override { return false; }
  int8_t AsInt8(int index) override { return 0; }
  uint8_t AsUint8(int index) override { return 0; }
  int16_t AsInt16(int index) override { return 0; }
  uint16_t AsUint16(int) override { return 0; }
  int32_t AsInt32(int index) override { return 0; }
  uint32_t AsUint32(int index) override { return 0; }
  int64_t AsInt64(int index) override { return 0; }
  uint64_t AsUint64(int index) override { return 0; }

  // Return a pointer to the MR object.
  std::any GetObject() const override { return std::any(value_); }
  // Return the shape of the MR.
  std::vector<int> shape() const override { return shape_; }
  // Return the name of the MR.
  std::string AsString() const override { return value_->AsString(); }

 private:
  T* value_;
  std::vector<int> shape_;
};

// Destination operand interface for object of type T. This class is used to
// provide the means to access the underlying object itself using the
// GetObject() method. None of the other methods are intended to be used.
template <typename T>
class WrapperDestinationOperand : public DestinationOperandInterface {
 public:
  WrapperDestinationOperand(T* value, const std::vector<int>& shape)
      : value_(value), shape_(shape) {}
  WrapperDestinationOperand() = delete;
  WrapperDestinationOperand(const WrapperDestinationOperand&) = delete;
  WrapperDestinationOperand& operator=(const WrapperDestinationOperand&) =
      delete;
  ~WrapperDestinationOperand() override = default;

  DataBuffer* AllocateDataBuffer() override { return nullptr; }
  void InitializeDataBuffer(DataBuffer* db) override { /* Do nothing. */ }
  DataBuffer* CopyDataBuffer() override { return nullptr; }
  int latency() const override { return 0; }
  // Return a pointer to the MR object.
  std::any GetObject() const override { return std::any(value_); }
  // Return the shape of the MR.
  std::vector<int> shape() const override { return shape_; }
  // Return the name of the MR.
  std::string AsString() const override { return value_->AsString(); }

 private:
  T* value_;
  std::vector<int> shape_;
};

}  // namespace generic
}  // namespace sim
}  // namespace mpact

#endif  // MPACT_SIM_GENERIC_WRAPPER_OPERAND_H_
