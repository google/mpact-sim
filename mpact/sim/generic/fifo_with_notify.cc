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

#include "mpact/sim/generic/fifo_with_notify.h"

#include <utility>

namespace mpact {
namespace sim {
namespace generic {

// Call base class Push. Only call the on_not_empty callback if the fifo is
// empty prior to pushing.
bool FifoWithNotifyBase::Push(DataBuffer *db) {
  if (!IsEmpty()) return FifoBase::Push(db);

  auto res = FifoBase::Push(db);
  if (on_not_empty_) on_not_empty_(this);
  return res;
}

// Call base class Pop. Only call the on_empty callback if the fifo is empty
// after the pop.
void FifoWithNotifyBase::Pop() {
  FifoBase::Pop();
  if (IsEmpty() && on_empty_) on_empty_(this);
}

void FifoWithNotifyBase::SetOnNotEmpty(OnEventCallback callback) {
  on_not_empty_ = std::move(callback);
}

void FifoWithNotifyBase::SetOnEmpty(OnEventCallback callback) {
  on_empty_ = std::move(callback);
}

}  // namespace generic
}  // namespace sim
}  // namespace mpact
