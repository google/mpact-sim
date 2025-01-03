// Copyright 2025 Google LLC
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

#ifndef MPACT_SIM_UTIL_ASM_TEST_RISCV64X_ASSEMBLER_H_
#define MPACT_SIM_UTIL_ASM_TEST_RISCV64X_ASSEMBLER_H_

#include <cstdint>
#include <tuple>

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "mpact/sim/util/asm/resolver_interface.h"
#include "mpact/sim/util/asm/test/riscv64x_bin_encoder_interface.h"
#include "mpact/sim/util/asm/test/riscv64x_encoder.h"
#include "mpact/sim/util/asm/test/riscv64x_enums.h"

namespace mpact {
namespace sim {
namespace riscv {

using ::mpact::sim::util::assembler::ResolverInterface;

class RiscV64XAssembler {
 public:
  using SlotEnum = isa64::SlotEnum;
  using OpcodeEnum = isa64::OpcodeEnum;

  RiscV64XAssembler();
  virtual ~RiscV64XAssembler();

  absl::StatusOr<std::tuple<uint64_t, int>> Assemble(
      uint64_t address, absl::string_view text, ResolverInterface *resolver);

 private:
  isa64::RiscV64XBinEncoderInterface *bin_encoder_interface_ = nullptr;
  isa64::Riscv64xSlotMatcher *matcher_ = nullptr;
};

}  // namespace riscv
}  // namespace sim
}  // namespace mpact

#endif  // MPACT_SIM_UTIL_ASM_TEST_RISCV64X_ASSEMBLER_H_
