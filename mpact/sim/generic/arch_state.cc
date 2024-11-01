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

#include "mpact/sim/generic/arch_state.h"

#include <string>
#include <utility>

#include "absl/container/flat_hash_set.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "mpact/sim/generic/component.h"
#include "mpact/sim/generic/data_buffer.h"
#include "mpact/sim/generic/fifo.h"
#include "mpact/sim/generic/function_delay_line.h"
#include "mpact/sim/generic/operand_interface.h"
#include "mpact/sim/generic/program_error.h"
#include "mpact/sim/generic/register.h"

namespace mpact {
namespace sim {
namespace generic {

ArchState::ArchState(Component* parent, absl::string_view id,
                     SourceOperandInterface* pc_operand)
    : Component(std::string(id), parent), pc_operand_(pc_operand) {
  // Create DataBufferFactory to be used for this ArchState instance.
  db_factory_ = new DataBufferFactory();

  // Create the two default delay lines for this ArchState instance.
  data_buffer_delay_line_ = CreateAndAddDelayLine<DataBufferDelayLine>();
  function_delay_line_ = CreateAndAddDelayLine<FunctionDelayLine>();

  // Create the program error controller.
  program_error_controller_ =
      new ProgramErrorController(absl::StrCat(id, "Errors"));
}

void ArchState::AddRegister(RegisterBase* reg) {
  AddRegister(reg->name(), reg);
}

void ArchState::AddRegister(absl::string_view name, RegisterBase* reg) {
  registers_.insert(std::make_pair(std::string(name), reg));
}

void ArchState::RemoveRegister(absl::string_view name) {
  registers_.erase(std::string(name));
}

void ArchState::AddFifo(FifoBase* fifo) { AddFifo(fifo->name(), fifo); }

void ArchState::AddFifo(absl::string_view name, FifoBase* fifo) {
  fifos_.insert(std::make_pair(std::string(name), fifo));
}

void ArchState::RemoveFifo(absl::string_view name) {
  fifos_.erase(std::string(name));
}

ArchState::~ArchState() {
  for (auto dl : delay_lines_) {
    delete dl;
  }
  delay_lines_.clear();

  // Since registers and fifos may have been inserted with aliases, we need to
  // keep track of the pointers that are deleted, so that we don't delete a
  // pointer more than once.
  absl::flat_hash_set<void*> pointer_set;

  for (const auto& reg : registers_) {
    if (pointer_set.contains(reg.second)) continue;
    pointer_set.insert(reg.second);
    delete reg.second;
  }
  registers_.clear();
  pointer_set.clear();

  for (const auto& fifo : fifos_) {
    if (pointer_set.contains(fifo.second)) continue;
    pointer_set.insert(fifo.second);
    delete fifo.second;
  }
  fifos_.clear();
  pointer_set.clear();
  delete db_factory_;

  delete program_error_controller_;
}

}  // namespace generic
}  // namespace sim
}  // namespace mpact
