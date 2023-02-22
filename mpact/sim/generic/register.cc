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

#include "mpact/sim/generic/register.h"

#include <cstdint>
#include <cstring>
#include <vector>

#include "absl/base/macros.h"
#include "absl/strings/string_view.h"
#include "mpact/sim/generic/ref_count.h"
#include "mpact/sim/generic/simple_resource.h"

namespace mpact {
namespace sim {
namespace generic {

RegisterBase::RegisterBase(ArchState *state, absl::string_view name,
                           const std::vector<int> &shape, int unit_size)
    : StateItemBase(state, name, shape, unit_size), data_buffer_(nullptr) {
  // Initialize register with a data buffer set to 0.
  if (state != nullptr) {
    auto *db = state->db_factory()->Allocate<uint8_t>(size());
    std::memset(db->raw_ptr(), 0, size());
    SetDataBuffer(db);
    db->DecRef();
  }
}

RegisterBase::~RegisterBase() {
  if (nullptr != data_buffer_) {
    data_buffer_->DecRef();
    data_buffer_ = nullptr;
  }
}

void RegisterBase::SetDataBuffer(DataBuffer *db) {
  // For now, ignore if the sizes don't match.
  // ABSL_HARDENING_ASSERT(db->size<uint8_t>() == size());
  if (nullptr != data_buffer_) {
    data_buffer_->DecRef();
  }
  db->IncRef();
  data_buffer_ = db;
}

ReservedRegisterBase::ReservedRegisterBase(ArchState *state,
                                           absl::string_view name,
                                           const std::vector<int> &shape,
                                           int unit_size,
                                           SimpleResource *resource)
    : RegisterBase(state, name, shape, unit_size), resource_(resource) {}

void ReservedRegisterBase::SetDataBuffer(DataBuffer *db) {
  // Use the base class to update the data buffer.
  RegisterBase::SetDataBuffer(db);
  // Release the resource if set.
  if (resource_ != nullptr) resource_->Release();
}

}  // namespace generic
}  // namespace sim
}  // namespace mpact
