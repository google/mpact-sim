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

#include "mpact/sim/util/asm/test/riscv64x_assembler.h"

#include <cstdint>
#include <tuple>

#include "absl/log/check.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "mpact/sim/util/asm/resolver_interface.h"
#include "mpact/sim/util/asm/test/riscv64x_bin_encoder_interface.h"
#include "mpact/sim/util/asm/test/riscv64x_encoder.h"

namespace mpact {
namespace sim {
namespace riscv {

using ::mpact::sim::util::assembler::ResolverInterface;

RiscV64XAssembler::RiscV64XAssembler() {
  bin_encoder_interface_ = new isa64::RiscV64XBinEncoderInterface();
  matcher_ = new isa64::Riscv64xSlotMatcher(bin_encoder_interface_);
  CHECK_OK(matcher_->Initialize());
}

RiscV64XAssembler::~RiscV64XAssembler() {
  delete bin_encoder_interface_;
  delete matcher_;
}

absl::StatusOr<std::tuple<uint64_t, int>> RiscV64XAssembler::Assemble(
    uint64_t address, absl::string_view text, ResolverInterface *resolver) {
  return matcher_->Encode(address, text, 0, resolver);
}

}  // namespace riscv
}  // namespace sim
}  // namespace mpact
