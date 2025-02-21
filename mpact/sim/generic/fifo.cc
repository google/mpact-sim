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

#include "mpact/sim/generic/fifo.h"

#include <string>
#include <vector>

#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "mpact/sim/generic/arch_state.h"
#include "mpact/sim/generic/component.h"
#include "mpact/sim/generic/data_buffer.h"
#include "mpact/sim/generic/state_item_base.h"

namespace mpact {
namespace sim {
namespace generic {

FifoBase::FifoBase(class ArchState *arch_state, absl::string_view name,
                   const std::vector<int> &shape, int element_size,
                   int default_capacity)
    : StateItemBase(arch_state, name, shape, element_size),
      Component(std::string(name), arch_state),
      depth_("depth", default_capacity),
      overflow_program_error_(nullptr),
      underflow_program_error_(nullptr),
      name_(name),
      capacity_(default_capacity),
      reserved_(0) {
  // empty
}

FifoBase::~FifoBase() {
  while (Available() > 0) {
    Pop();
  }
}

bool FifoBase::IsFull() const { return fifo_.size() + reserved_ >= capacity_; }

bool FifoBase::IsEmpty() const { return fifo_.empty() && (reserved_ == 0); }

bool FifoBase::IsOverSubscribed() const {
  return fifo_.size() + reserved_ > capacity_;
}

void FifoBase::Reserve(int count) { reserved_ += count; }

bool FifoBase::Push(DataBuffer *db) {
  // If any slots are reserved, decrement first before checking for full.
  if (reserved_ > 0) {
    reserved_--;
  }
  if (IsFull()) {
    if (nullptr != overflow_program_error_) {
      overflow_program_error_->Raise("Overflow in fifo " + name_);
    }
    return false;
  } else {
    db->IncRef();
    fifo_.push_back(db);
    return true;
  }
}

void FifoBase::Pop() {
  if (fifo_.empty()) {
    if (nullptr != underflow_program_error_) {
      underflow_program_error_->Raise("Underflow in fifo " + name_);
    }
    return;
  }
  fifo_.front()->DecRef();
  fifo_.pop_front();
}

DataBuffer *FifoBase::Front() const {
  if (fifo_.empty()) {
    if (nullptr != underflow_program_error_) {
      underflow_program_error_->Raise("Underflow in fifo " + name_);
    }
    return nullptr;
  } else {
    return fifo_.front();
  }
}

unsigned FifoBase::Available() const { return fifo_.size(); }

void FifoBase::SetDataBuffer(DataBuffer *db) { Push(db); }

absl::Status FifoBase::ImportSelf(
    const mpact::sim::proto::ComponentData &component_data) {
  auto status = Component::ImportSelf(component_data);
  if (!status.ok()) return status;
  capacity_ = depth_.GetValue();
  return absl::OkStatus();
}

}  // namespace generic
}  // namespace sim
}  // namespace mpact
