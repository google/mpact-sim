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

#ifndef MPACT_SIM_GENERIC_CONTROL_REGISTER_H_
#define MPACT_SIM_GENERIC_CONTROL_REGISTER_H_

// Defines a new register base class that registers a callback function that
// is called when the register receives a new value update in the form of a
// data buffer. The callback can be used in several ways according to need. One
// intended use case is to generate a side-effect or action in the simulated
// system. Another may be to constrain the written value to a legal subset or
// bit range, for instance when using a 32 bit element type to store an 18
// bit value. This function is in addition to the "next_update_callback" of
// RegisterBase, in that it is persistent, and allows for changes to the
// stored value before the "next_update_callback" is called.
//
// While the additional functionality of this class over the regular Register
// class is minor, it is done in a separate class to avoid adding any more
// overhead to the most common usecase of normal register writes.
//
// It's worth noting that the value_update_notification that is available for
// the RegisterBase class will only be called if the RegisterBase::SetDataBuffer
// method is called from the update callback function, as that is the only way
// the register object's data pointer is updated.

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "absl/strings/string_view.h"
#include "mpact/sim/generic/data_buffer.h"
#include "mpact/sim/generic/register.h"
#include "mpact/sim/generic/state_item.h"
#include "mpact/sim/generic/state_item_base.h"

namespace mpact {
namespace sim {
namespace generic {

class ArchState;

// Base class for control registers types with the DataBufferDestination
// interface. There is no default constructor, should only be
// constructed/destructed from derived classes.
class ControlRegisterBase : public RegisterBase {
 public:
  // Type alias for the update callback function type (set in the constructor).
  using UpdateCallbackFunction =
      std::function<void(ControlRegisterBase *, DataBuffer *)>;
  ControlRegisterBase() = delete;
  ControlRegisterBase(const ControlRegisterBase &) = delete;
  ~ControlRegisterBase() override = default;

  // Calls the update callback function.
  void SetDataBuffer(DataBuffer *db) override;

 protected:
  ControlRegisterBase(ArchState *arch_state, absl::string_view name,
                      const std::vector<int> &shape, int element_size,
                      UpdateCallbackFunction on_update_callback);

 private:
  UpdateCallbackFunction on_update_callback_;
};

// Scalar control register type with value type ElementType.
template <typename ElementType>
using ControlRegister = StateItem<ControlRegisterBase, ElementType,
                                  RegisterSourceOperand<ElementType>,
                                  RegisterDestinationOperand<ElementType>>;

// Holding off defining any vector valued or matrix valued control registers
// for now until the need arises. Most control registers are scalar.

}  // namespace generic
}  // namespace sim
}  // namespace mpact

#endif  // MPACT_SIM_GENERIC_CONTROL_REGISTER_H_
