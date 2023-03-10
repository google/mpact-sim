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

#ifndef MPACT_SIM_DECODER_TEST_TESTFILES_RISCV32I_H_
#define MPACT_SIM_DECODER_TEST_TESTFILES_RISCV32I_H_

#include "mpact/sim/generic/instruction.h"

namespace mpact {
namespace sim {
namespace riscv {
namespace rv32 {

using ::mpact::sim::generic::Instruction;

void SomeFunction(Instruction *inst);
void SomeOtherFunction(Instruction *inst);

}  // namespace rv32
}  // namespace riscv
}  // namespace sim
}  // namespace mpact

#endif  // MPACT_SIM_DECODER_TEST_TESTFILES_RISCV32I_H_
