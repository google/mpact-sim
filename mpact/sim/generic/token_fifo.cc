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

#include "mpact/sim/generic/token_fifo.h"

#include <vector>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "mpact/sim/generic/arch_state.h"
#include "mpact/sim/generic/data_buffer.h"

namespace mpact {
namespace sim {
namespace generic {

// Token store method definitions.
absl::Status FifoTokenStore::Acquire() {
  if (available_ > 0) {
    available_--;
    return absl::OkStatus();
  }
  return absl::UnavailableError("No token available");
}

absl::Status FifoTokenStore::Release() {
  if (available_ == capacity_) {
    return absl::InternalError("More tokens released than capacity");
  }
  available_++;
  return absl::OkStatus();
}

// Token fifo base method definitions.
TokenFifoBase::TokenFifoBase(ArchState* arch_state, absl::string_view name,
                             const std::vector<int>& shape, int element_size,
                             unsigned capacity, FifoTokenStore* tokens)
    : FifoBase(arch_state, name, shape, element_size, capacity),
      token_store_(tokens) {}

// The fifo is full if it is at capacity or if there are no token available.
bool TokenFifoBase::IsFull() const {
  return (token_store_->available() == 0) ||
         (Reserved() >= token_store_->available());
}

bool TokenFifoBase::IsOverSubscribed() const {
  return Reserved() > token_store_->available();
}

bool TokenFifoBase::Push(DataBuffer* db) {
  if (token_store_->available() == 0) {
    if (nullptr != overflow_program_error()) {
      overflow_program_error()->Raise("Overflow in fifo " + name());
    }
    return false;
  }
  FifoBase::Push(db);
  // Acquire a token. There is an error if the fifo is not full but a token
  // cannot be acquired.
  if (!token_store_->Acquire().ok()) {
    if (overflow_program_error() != nullptr) {
      overflow_program_error()->Raise(
          absl::StrCat("No token available for ", name()));
    }
    return false;
  }
  return true;
}

void TokenFifoBase::Pop() {
  if (IsEmpty()) {
    if (underflow_program_error() != nullptr) {
      underflow_program_error()->Raise("Underflow in " + name());
    }
    return;
  }
  FifoBase::Pop();
  auto status = token_store_->Release();
  if (!status.ok()) {
    if (nullptr != underflow_program_error()) {
      underflow_program_error()->Raise("Error when releasing token in " +
                                       name());
    }
  }
}

}  // namespace generic
}  // namespace sim
}  // namespace mpact
