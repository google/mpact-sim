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

#include "mpact/sim/generic/state_item_base.h"

#include <vector>

#include "absl/strings/string_view.h"

namespace mpact {
namespace sim {
namespace generic {

StateItemBase::StateItemBase(class ArchState* arch_state,
                             absl::string_view name,
                             const std::vector<int>& shape, int element_size)
    : arch_state_(arch_state),
      name_(name),
      shape_(shape),
      element_size_(element_size),
      size_(element_size) {
  // Compute the byte size of the state that will be stored based on the
  // order information.
  for (int sz : shape_) {
    size_ *= sz;
  }
}
}  // namespace generic
}  // namespace sim
}  // namespace mpact
