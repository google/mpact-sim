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

#ifndef MPACT_SIM_GENERIC_STATE_ITEM_BASE_H_
#define MPACT_SIM_GENERIC_STATE_ITEM_BASE_H_

#include <string>
#include <vector>

#include "absl/strings/string_view.h"
#include "mpact/sim/generic/data_buffer.h"

namespace mpact {
namespace sim {
namespace generic {

class ArchState;

// The StateBase class factors out common features of simulated machine
// state structures, namely the Name, Shape (size in each dimension) and Size
// in bytes of the state instance. The class inherits from
// DataBufferDestination, but does not implement its interface. That is left
// to the classes that implement from StateItemBase. Doing it this way avoids
// a multiple inheritance situation in the final classes.
class StateItemBase : public DataBufferDestination {
 protected:
  // Only constructed from derived classes
  StateItemBase(ArchState* arch_state, absl::string_view name,
                const std::vector<int>& shape, int element_size);
  StateItemBase() = delete;
  StateItemBase(const StateItemBase&) = delete;

 public:
  // Name() returns the state item's name.
  const std::string& name() const { return name_; }

  // Returns the size vector of the state item. A scalar element has size
  // vector {1}, an N element vector item has size vector {N}, and
  // an MxN array item element has size vector {M,N}.
  std::vector<int> shape() const { return shape_; }

  // Returns the size in bytes of the state item.
  int size() const { return size_; }

  // Returns the size in bytes of the unit type.
  int element_size() const { return element_size_; }

  // Returns the architecture state object that this simulated state is
  // associated with.
  ArchState* arch_state() const { return arch_state_; }

 private:
  ArchState* arch_state_;
  std::string name_;
  std::vector<int> shape_;
  int element_size_;
  int size_;
};

}  // namespace generic
}  // namespace sim
}  // namespace mpact

#endif  // MPACT_SIM_GENERIC_STATE_ITEM_BASE_H_
