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

#include "mpact/sim/util/asm/test/riscv64x_bin_encoder_interface.h"

#include <cstdint>
#include <tuple>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "mpact/sim/generic/type_helpers.h"
#include "mpact/sim/util/asm/resolver_interface.h"
#include "mpact/sim/util/asm/test/riscv64x_bin_encoder.h"
#include "mpact/sim/util/asm/test/riscv64x_enums.h"
#include "mpact/sim/util/asm/test/riscv_bin_setters.h"

namespace mpact {
namespace sim {
namespace riscv {
namespace isa64 {

using ::mpact::sim::generic::operator*;  // NOLINT(misc-unused-using-decls)
using ::mpact::sim::util::assembler::ResolverInterface;

RiscV64XBinEncoderInterface::RiscV64XBinEncoderInterface() {
  AddRiscvSourceOpBinSetters<SourceOpEnum, OpMap, encoding64::Encoder>(
      source_op_map_);
  AddRiscvDestOpBinSetters<DestOpEnum, OpMap, encoding64::Encoder>(
      dest_op_map_);
}

absl::StatusOr<std::tuple<uint64_t, int>>
RiscV64XBinEncoderInterface::GetOpcodeEncoding(SlotEnum slot, int entry,
                                               OpcodeEnum opcode,
                                               ResolverInterface *resolver) {
  return encoding64::kOpcodeEncodings->at(opcode);
}

absl::StatusOr<uint64_t> RiscV64XBinEncoderInterface::GetSrcOpEncoding(
    uint64_t address, absl::string_view text, SlotEnum slot, int entry,
    OpcodeEnum opcode, SourceOpEnum source_op, int source_num,
    ResolverInterface *resolver) {
  auto iter = source_op_map_.find(*source_op);
  if (iter == source_op_map_.end()) {
    return absl::NotFoundError(absl::StrCat(
        "Source operand not found for op enum value ", *source_op));
  }
  return iter->second(address, text, resolver);
}

absl::StatusOr<uint64_t> RiscV64XBinEncoderInterface::GetDestOpEncoding(
    uint64_t address, absl::string_view text, SlotEnum slot, int entry,
    OpcodeEnum opcode, DestOpEnum dest_op, int dest_num,
    ResolverInterface *resolver) {
  auto iter = dest_op_map_.find(*dest_op);
  if (iter == dest_op_map_.end()) {
    return absl::NotFoundError(
        absl::StrCat("Dest operand not found for op enum value ", *dest_op));
  }
  return iter->second(address, text, resolver);
}

absl::StatusOr<uint64_t> RiscV64XBinEncoderInterface::GetListDestOpEncoding(
    uint64_t address, absl::string_view text, SlotEnum slot, int entry,
    OpcodeEnum opcode, ListDestOpEnum dest_op, int dest_num,
    ResolverInterface *resolver) {
  auto iter = list_dest_op_map_.find(*dest_op);
  if (iter == list_dest_op_map_.end()) {
    return absl::NotFoundError(absl::StrCat(
        "List dest operand not found for op enum value ", *dest_op));
  }
  return iter->second(address, text, resolver);
}

absl::StatusOr<uint64_t> RiscV64XBinEncoderInterface::GetListSrcOpEncoding(
    uint64_t address, absl::string_view text, SlotEnum slot, int entry,
    OpcodeEnum opcode, ListSourceOpEnum source_op, int source_num,
    ResolverInterface *resolver) {
  auto iter = list_source_op_map_.find(*source_op);
  if (iter == list_source_op_map_.end()) {
    return absl::NotFoundError(absl::StrCat(
        "List source operand not found for op enum value ", *source_op));
  }
  return iter->second(address, text, resolver);
}

absl::StatusOr<uint64_t> RiscV64XBinEncoderInterface::GetPredOpEncoding(
    uint64_t address, absl::string_view text, SlotEnum slot, int entry,
    OpcodeEnum opcode, PredOpEnum pred_op, ResolverInterface *resolver) {
  auto iter = pred_op_map_.find(*pred_op);
  if (iter == pred_op_map_.end()) {
    return absl::NotFoundError(absl::StrCat(
        "Predicate operand not found for op enum value ", *pred_op));
  }
  return iter->second(address, text, resolver);
}

}  // namespace isa64
}  // namespace riscv
}  // namespace sim
}  // namespace mpact
