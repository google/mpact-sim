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

#ifndef MPACT_SIM_GENERIC_OPERAND_INTERFACE_H_
#define MPACT_SIM_GENERIC_OPERAND_INTERFACE_H_

// This file contains the source and destination operand interface definitions.
// The operand interfaces are used in the semantic functions of instructions to
// read from and write to the instruction operands, regardless of the underlying
// type (immediate, register, fifo, etc.).

#include <any>
#include <cstdint>
#include <string>
#include <vector>

#include "absl/types/any.h"
#include "mpact/sim/generic/data_buffer.h"

namespace mpact {
namespace sim {
namespace generic {

// The predicte operand interface is intended primarily as the interface to
// read the value of instruction predicates. It is separated from source
// predicates to avoid mixing it in with the source operands needed for modeling
// the instruction semantics.
class PredicateOperandInterface {
 public:
  virtual bool Value() = 0;
  // Return a string representation of the operand suitable for display in
  // disassembly.
  virtual std::string AsString() const = 0;
  virtual ~PredicateOperandInterface() = default;
};

// The source operand interface provides an interface to access input values
// to instructions in a way that is agnostic about the underlying implementation
// of those values (eg., register, fifo, immediate, predicate, etc).
class SourceOperandInterface {
 public:
  // Methods for accessing the nth value element.
  virtual bool AsBool(int index) = 0;
  virtual int8_t AsInt8(int index) = 0;
  virtual uint8_t AsUint8(int index) = 0;
  virtual int16_t AsInt16(int index) = 0;
  virtual uint16_t AsUint16(int) = 0;
  virtual int32_t AsInt32(int index) = 0;
  virtual uint32_t AsUint32(int index) = 0;
  virtual int64_t AsInt64(int index) = 0;
  virtual uint64_t AsUint64(int index) = 0;

  // Return a pointer to the object instance that implements the state in
  // question (or nullptr) if no such object "makes sense". This is used if
  // the object requires additional manipulation - such as a fifo that needs
  // to be popped. If no such manipulation is required, nullptr should be
  // returned.
  virtual std::any GetObject() const = 0;

  // Return the shape of the operand (the number of elements in each dimension).
  // For instance {1} indicates a scalar quantity, whereas {128} indicates an
  // 128 element vector quantity.
  virtual std::vector<int> shape() const = 0;

  // Return a string representation of the operand suitable for display in
  // disassembly.
  virtual std::string AsString() const = 0;

  virtual ~SourceOperandInterface() = default;
};

// The destination operand interface is used by instruction semantic functions
// to get a writable DataBuffer associated with a piece of simulated state to
// which the new value can be written, and then used to update the value of
// the piece of state with a given latency.
class DestinationOperandInterface {
 public:
  virtual ~DestinationOperandInterface() = default;
  // Allocates a data buffer with ownership, latency and delay line set up.
  virtual DataBuffer *AllocateDataBuffer() = 0;
  // Takes an existing data buffer, and initializes it for the destination
  // as if AllocateDataBuffer had been called.
  virtual void InitializeDataBuffer(DataBuffer *db) = 0;
  // Allocates and initializes data buffer as if AllocateDataBuffer had been
  // called, but also copies in the value from the current value of the
  // destination.
  virtual DataBuffer *CopyDataBuffer() = 0;
  // Returns the latency associated with the destination operand.
  virtual int latency() const = 0;
  // Return a pointer to the object instance that implements the state in
  // question (or nullptr if no such object "makes sense").
  virtual std::any GetObject() const = 0;
  // Returns the order of the destination operand (size in each dimension).
  virtual std::vector<int> shape() const = 0;
  // Return a string representation of the operand suitable for display in
  // disassembly.
  virtual std::string AsString() const = 0;
};

}  // namespace generic
}  // namespace sim
}  // namespace mpact

#endif  // MPACT_SIM_GENERIC_OPERAND_INTERFACE_H_
