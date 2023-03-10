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

#include "mpact/sim/generic/control_register.h"

#include <vector>

namespace mpact {
namespace sim {
namespace generic {

ControlRegisterBase::ControlRegisterBase(
    ArchState *arch_state, absl::string_view name,
    const std::vector<int> &shape, int element_size,
    UpdateCallbackFunction on_update_callback)
    : RegisterBase(arch_state, name, shape, element_size),
      on_update_callback_(on_update_callback) {}

// This method does not update anything in the object, instead it calls the
// update callback function. In order to update the register value, the
// callback function should use the ControlRegisterBase pointer to call
// the RegisterBase SetDataBuffer. E.g., the following update callback function
// is equivalent to just using the RegisterBase class itself.
//
// void MyUpdateFunction(ControlRegisterBase *register, DataBuffer *db) {
//   register->RegisterBase::SetDataBuffer(db);
// }
//
void ControlRegisterBase::SetDataBuffer(DataBuffer *db) {
  on_update_callback_(this, db);
}

}  // namespace generic
}  // namespace sim
}  // namespace mpact
