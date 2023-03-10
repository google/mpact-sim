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

#ifndef MPACT_SIM_GENERIC_DEVNULL_OPERAND_H_
#define MPACT_SIM_GENERIC_DEVNULL_OPERAND_H_

#include <any>
#include <string>
#include <vector>

#include "absl/strings/string_view.h"
#include "mpact/sim/generic/arch_state.h"
#include "mpact/sim/generic/data_buffer.h"
#include "mpact/sim/generic/operand_interface.h"

// This file provides a destination operand that acts as a regular operand, but
// writing to the operand has no effect. It will provide a data buffer, but
// the data buffer will be discarded when submitted. This is intended to be used
// for semantic functions that may expect more destination operands than what
// the instruction itself specifies.

namespace mpact {
namespace sim {
namespace generic {

template <typename T>
class DevNullOperand : public DestinationOperandInterface {
 public:
  // Constructor requires machine state to access data buffer factory, and
  // shape to allocate the correct size.
  DevNullOperand(ArchState *state, const std::vector<int> &shape,
                 absl::string_view string_value)
      : db_factory_(state->db_factory()),
        shape_(shape),
        size_(sizeof(T)),
        string_value_(string_value) {
    for (int sz : shape_) {
      size_ *= sz;
    }
  }
  DevNullOperand(ArchState *state, const std::vector<int> &shape)
      : DevNullOperand(state, shape, "") {}
  DevNullOperand() = delete;
  // Allocates a data buffer of the correct size, but initializes it with a
  // null destination.
  DataBuffer *AllocateDataBuffer() override {
    DataBuffer *db = db_factory_->Allocate(size_);
    InitializeDataBuffer(db);
    return db;
  }
  // Initializes the data buffer with null attributes.
  void InitializeDataBuffer(DataBuffer *db) override {
    db->set_destination(nullptr);
    db->set_latency(0);
    db->set_delay_line(nullptr);
  }
  // Just call AllocateDataBuffer, as there is no underlying state item.
  DataBuffer *CopyDataBuffer() override { return AllocateDataBuffer(); }
  // Zero latency always.
  int latency() const override { return 0; }
  // No object to return.
  std::any GetObject() const override { return std::any(); }
  // Accessor.
  std::vector<int> shape() const override { return shape_; }

  std::string AsString() const override { return string_value_; }

 private:
  DataBufferFactory *db_factory_;
  std::vector<int> shape_;
  int size_;
  std::string string_value_;
};

}  // namespace generic
}  // namespace sim
}  // namespace mpact
#endif  // MPACT_SIM_GENERIC_DEVNULL_OPERAND_H_
