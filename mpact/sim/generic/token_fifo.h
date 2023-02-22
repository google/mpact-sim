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

#ifndef MPACT_SIM_GENERIC_TOKEN_FIFO_H_
#define MPACT_SIM_GENERIC_TOKEN_FIFO_H_

#include <vector>

#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "mpact/sim/generic/arch_state.h"
#include "mpact/sim/generic/data_buffer.h"
#include "mpact/sim/generic/fifo.h"
#include "mpact/sim/generic/program_error.h"
#include "mpact/sim/generic/state_item.h"
#include "mpact/sim/generic/state_item_base.h"

// Contains the definition for classes required to model a group of fifos that
// operate independently except that their total capacity is based on a shared
// resource (RAM), so that the total size of all the fifos sharing the resource
// cannot exceed the size of the resource.

namespace mpact {
namespace sim {
namespace generic {

// The fifo uses a token based approach. Each token allows for one element to
// be pushed into the fifo. The token is released when the element is pop'ed
// off the fifo and returned to the token store.

class FifoTokenStore {
 public:
  explicit FifoTokenStore(unsigned size) : capacity_(size), available_(size) {}

  absl::Status Acquire();
  absl::Status Release();
  unsigned available() const { return available_; }
  unsigned capacity() const { return capacity_; }

 private:
  unsigned capacity_;
  unsigned available_;
};

class TokenFifoBase : public FifoBase {
 protected:
  TokenFifoBase(ArchState *arch_state, absl::string_view name,
                const std::vector<int> &shape, int element_size, unsigned capacity,
                FifoTokenStore *tokens);
  TokenFifoBase() = delete;
  TokenFifoBase(const TokenFifoBase &) = delete;

 public:
  bool IsOverSubscribed() const override;
  bool IsFull() const override;
  bool Push(DataBuffer *db) override;
  void Pop() override;

 private:
  FifoTokenStore *token_store_;
};

template <typename ElementType>
using TokenFifo =
    StateItem<TokenFifoBase, ElementType, FifoSourceOperand<ElementType>,
              FifoDestinationOperand<ElementType>>;

}  // namespace generic
}  // namespace sim
}  // namespace mpact

#endif  // MPACT_SIM_GENERIC_TOKEN_FIFO_H_
