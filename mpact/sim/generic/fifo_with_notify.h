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

#ifndef MPACT_SIM_GENERIC_FIFO_WITH_NOTIFY_H_
#define MPACT_SIM_GENERIC_FIFO_WITH_NOTIFY_H_

#include <vector>

#include "absl/functional/any_invocable.h"
#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "mpact/sim/generic/arch_state.h"
#include "mpact/sim/generic/data_buffer.h"
#include "mpact/sim/generic/fifo.h"
#include "mpact/sim/generic/program_error.h"
#include "mpact/sim/generic/state_item.h"
#include "mpact/sim/generic/state_item_base.h"

namespace mpact {
namespace sim {
namespace generic {

// This class inherits from FifoBase and adds ability to register callbacks when
// the fifo transitions from empty to non-empty, or from non-empty to empty.
class FifoWithNotifyBase : public FifoBase {
 protected:
  // Constructed from derived class (StateItem).
  FifoWithNotifyBase(ArchState *arch_state, absl::string_view name,
                     const std::vector<int> &shape, int element_size,
                     int default_capacity)
      : FifoBase(arch_state, name, shape, element_size, default_capacity) {}
  FifoWithNotifyBase() = delete;
  FifoWithNotifyBase(const FifoWithNotifyBase &) = delete;

 public:
  using OnEventCallback = absl::AnyInvocable<void(FifoWithNotifyBase *)>;

  // SetDataBuffer and Pop are overridden to detect when the fifo transitions
  // from/to empty. The reason Push is not overrided is that Push moves the
  // data buffer into a delay line with a possible non-zero latency. That means
  // that the write doesn't actually occur into until SetDataBuffer is called.
  bool Push(DataBuffer *) override;
  void Pop() override;

  // Set the transition callbacks functions. Pass in nullptr to clear an already
  // set callback.
  void SetOnNotEmpty(OnEventCallback callback);
  void SetOnEmpty(OnEventCallback callback);

 private:
  OnEventCallback on_not_empty_;
  OnEventCallback on_empty_;
};

template <typename ElementType>
using FifoWithNotify =
    StateItem<FifoWithNotifyBase, ElementType, FifoSourceOperand<ElementType>,
              FifoDestinationOperand<ElementType>>;

}  // namespace generic
}  // namespace sim
}  // namespace mpact

#endif  // MPACT_SIM_GENERIC_FIFO_WITH_NOTIFY_H_
