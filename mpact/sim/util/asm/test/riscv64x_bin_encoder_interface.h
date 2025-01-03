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

#ifndef MPACT_SIM_UTIL_ASM_TEST_RISCV64X_BIN_ENCODER_INTERFACE_H_
#define MPACT_SIM_UTIL_ASM_TEST_RISCV64X_BIN_ENCODER_INTERFACE_H_

#include <cstdint>
#include <functional>
#include <tuple>

#include "absl/container/flat_hash_map.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "mpact/sim/util/asm/resolver_interface.h"
#include "mpact/sim/util/asm/test/riscv64x_encoder.h"
#include "mpact/sim/util/asm/test/riscv64x_enums.h"

namespace mpact {
namespace sim {
namespace riscv {
namespace isa64 {

using ::mpact::sim::util::assembler::ResolverInterface;

class RiscV64XBinEncoderInterface : public RiscV64XEncoderInterfaceBase {
 public:
  RiscV64XBinEncoderInterface();
  RiscV64XBinEncoderInterface(const RiscV64XBinEncoderInterface &) = delete;
  RiscV64XBinEncoderInterface &operator=(const RiscV64XBinEncoderInterface &) =
      delete;
  ~RiscV64XBinEncoderInterface() override = default;

  absl::StatusOr<std::tuple<uint64_t, int>> GetOpcodeEncoding(
      SlotEnum slot, int entry, OpcodeEnum opcode,
      ResolverInterface *resolver) override;
  absl::StatusOr<uint64_t> GetSrcOpEncoding(
      uint64_t address, absl::string_view text, SlotEnum slot, int entry,
      OpcodeEnum opcode, SourceOpEnum source_op, int source_num,
      ResolverInterface *resolver) override;
  absl::StatusOr<uint64_t> GetDestOpEncoding(
      uint64_t address, absl::string_view text, SlotEnum slot, int entry,
      OpcodeEnum opcode, DestOpEnum dest_op, int dest_num,
      ResolverInterface *resolver) override;
  absl::StatusOr<uint64_t> GetListDestOpEncoding(
      uint64_t address, absl::string_view text, SlotEnum slot, int entry,
      OpcodeEnum opcode, ListDestOpEnum dest_op, int dest_num,
      ResolverInterface *resolver) override;
  absl::StatusOr<uint64_t> GetListSourceOpEncoding(
      uint64_t address, absl::string_view text, SlotEnum slot, int entry,
      OpcodeEnum opcode, ListSourceOpEnum source_op, int source_num,
      ResolverInterface *resolver) override;
  absl::StatusOr<uint64_t> GetPredOpEncoding(
      uint64_t address, absl::string_view text, SlotEnum slot, int entry,
      OpcodeEnum opcode, PredOpEnum pred_op,
      ResolverInterface *resolver) override;

 private:
  using OpMap = absl::flat_hash_map<
      int, std::function<absl::StatusOr<uint64_t>(uint64_t, absl::string_view,
                                                  ResolverInterface *)>>;

  OpMap source_op_map_;
  OpMap dest_op_map_;
  OpMap list_dest_op_map_;
  OpMap list_source_op_map_;
  OpMap pred_op_map_;
};

}  // namespace isa64
}  // namespace riscv
}  // namespace sim
}  // namespace mpact

#endif  // MPACT_SIM_UTIL_ASM_TEST_RISCV64X_BIN_ENCODER_INTERFACE_H_
