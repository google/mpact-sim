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

#ifndef THIRD_PARTY_MPACT_RISCV_RISCV_RISCV_I_INSTRUCTIONS_H_
#define THIRD_PARTY_MPACT_RISCV_RISCV_RISCV_I_INSTRUCTIONS_H_

#include <cstdint>

#include "mpact/sim/generic/instruction.h"

// This file contains the declarations of the instruction semantic functions
// for the RiscV i instructions. The 32 bit instruction versions are in the
// RV32 namespace, whereas the 64 bit versions are in the RV64 namespace.
// Instructions that are the same across 32 and 64 bits are in the riscv
// namespace.

namespace mpact {
namespace sim {
namespace riscv {

using ::mpact::sim::generic::Instruction;

void RiscVIllegalInstruction(const Instruction* inst);

// No operands necessary for Nop. For now, all hint instructions should be
// decoded as Nop.
void RiscVINop(const Instruction* instruction);

namespace RV32 {

// For the following, source operand 0 refers to the register specified in rs1,
// and source operand 1 refers to either the register specified in rs2, or the
// immediate. Destination operand 0 refers to the register specified in rd.
void RiscVIAdd(const Instruction* instruction);
void RiscVISub(const Instruction* instruction);
void RiscVISlt(const Instruction* instruction);
void RiscVISltu(const Instruction* instruction);
void RiscVIAnd(const Instruction* instruction);
void RiscVIOr(const Instruction* instruction);
void RiscVIXor(const Instruction* instruction);
void RiscVISll(const Instruction* instruction);
void RiscVISrl(const Instruction* instruction);
void RiscVISra(const Instruction* instruction);
// For the following two semantic functions, source operand 0 refers to the
// immediate value, and destination 0 the register specified in rd. Note, the
// value of the immediate shall be properly shifted.
void RiscVILui(const Instruction* instruction);
void RiscVIAuipc(const Instruction* instruction);
// Source operand 0 contains the immediate value. Destination operand 0 refers
// to the pc destination operand, wheras destination operand 1 refers to the
// link register specified in rd.
void RiscVIJal(const Instruction* instruction);
// Source operand 0 refers to the base registers specified by rs1, source
// operand 1 contains the immediate value. Destination operand 0 refers to the
// pc destination operand, wheras destination operand 1 refers to the
// link register specified in rd.
void RiscVIJalr(const Instruction* instruction);
// For the following branch instructions. Source operand 0 refers to the
// register specified by rs1, source operand 2 refers to the register specified
// by rs2, and source operand 3 refers to the immediate offset. Destination
// operand 0 refers to the pc destination operand.
void RiscVIBeq(const Instruction* instruction);
void RiscVIBne(const Instruction* instruction);
void RiscVIBlt(const Instruction* instruction);
void RiscVIBltu(const Instruction* instruction);
void RiscVIBge(const Instruction* instruction);
void RiscVIBgeu(const Instruction* instruction);
// Each of the load instructions are modeled by a pair of semantic instruction
// functions. The "main" function computes the effective address and initiates
// the load, the "child" function processes the load result and writes it back
// to the destination register.
// For the "main" semantic function, source operand 0 is the base register,
// source operand 1 the offset. Destination operand 0 is the register specified
// by rd. The "child" semantic function will get a copy of the destination
// operand.
void RiscVILd(const Instruction* instruction);
void RiscVILw(const Instruction* instruction);
void RiscVILwChild(const Instruction* instruction);
void RiscVILh(const Instruction* instruction);
void RiscVILhChild(const Instruction* instruction);
void RiscVILhu(const Instruction* instruction);
void RiscVILhuChild(const Instruction* instruction);
void RiscVILb(const Instruction* instruction);
void RiscVILbChild(const Instruction* instruction);
void RiscVILbu(const Instruction* instruction);
void RiscVILbuChild(const Instruction* instruction);
// For each store instruction semantic function, source operand 0 is the base
// register, source operand 1 is the offset, while source operand 2 is the value
// to be stored referred to by rs2.
void RiscVISd(const Instruction* instruction);
void RiscVISw(const Instruction* instruction);
void RiscVISh(const Instruction* instruction);
void RiscVISb(const Instruction* instruction);

}  // namespace RV32

namespace RV64 {

// For the following, source operand 0 refers to the register specified in rs1,
// and source operand 1 refers to either the register specified in rs2, or the
// immediate. Destination operand 0 refers to the register specified in rd.
void RiscVIAdd(const Instruction* instruction);
void RiscVIAddw(const Instruction* instruction);
void RiscVISub(const Instruction* instruction);
void RiscVISubw(const Instruction* instruction);
void RiscVISlt(const Instruction* instruction);
void RiscVISltu(const Instruction* instruction);
void RiscVIAnd(const Instruction* instruction);
void RiscVIOr(const Instruction* instruction);
void RiscVIXor(const Instruction* instruction);
void RiscVISll(const Instruction* instruction);
void RiscVISrl(const Instruction* instruction);
void RiscVISra(const Instruction* instruction);
void RiscVISllw(const Instruction* instruction);
void RiscVISrlw(const Instruction* instruction);
void RiscVISraw(const Instruction* instruction);
// For the following two semantic functions, source operand 0 refers to the
// immediate value, and destination 0 the register specified in rd. Note, the
// value of the immediate shall be properly shifted.
void RiscVILui(const Instruction* instruction);
void RiscVIAuipc(const Instruction* instruction);
// No operands necessary for Nop. For now, all hint instructions should be
// decoded as Nop.
void RiscVINop(const Instruction* instruction);
// Source operand 0 contains the immediate value. Destination operand 0 refers
// to the pc destination operand, wheras destination operand 1 refers to the
// link register specified in rd.
void RiscVIJal(const Instruction* instruction);
// Source operand 0 refers to the base registers specified by rs1, source
// operand 1 contains the immediate value. Destination operand 0 refers to the
// pc destination operand, wheras destination operand 1 refers to the
// link register specified in rd.
void RiscVIJalr(const Instruction* instruction);
// For the following branch instructions. Source operand 0 refers to the
// register specified by rs1, source operand 2 refers to the register specified
// by rs2, and source operand 3 refers to the immediate offset. Destination
// operand 0 refers to the pc destination operand.
void RiscVIBeq(const Instruction* instruction);
void RiscVIBne(const Instruction* instruction);
void RiscVIBlt(const Instruction* instruction);
void RiscVIBltu(const Instruction* instruction);
void RiscVIBge(const Instruction* instruction);
void RiscVIBgeu(const Instruction* instruction);
// Each of the load instructions are modeled by a pair of semantic instruction
// functions. The "main" function computes the effective address and initiates
// the load, the "child" function processes the load result and writes it back
// to the destination register.
// For the "main" semantic function, source operand 0 is the base register,
// source operand 1 the offset. Destination operand 0 is the register specified
// by rd. The "child" semantic function will get a copy of the destination
// operand.
void RiscVILd(const Instruction* instruction);
void RiscVILdChild(const Instruction* instruction);
void RiscVILw(const Instruction* instruction);
void RiscVILwChild(const Instruction* instruction);
void RiscVILwu(const Instruction* instruction);
void RiscVILwuChild(const Instruction* instruction);
void RiscVILh(const Instruction* instruction);
void RiscVILhChild(const Instruction* instruction);
void RiscVILhu(const Instruction* instruction);
void RiscVILhuChild(const Instruction* instruction);
void RiscVILb(const Instruction* instruction);
void RiscVILbChild(const Instruction* instruction);
void RiscVILbu(const Instruction* instruction);
void RiscVILbuChild(const Instruction* instruction);
// For each store instruction semantic function, source operand 0 is the base
// register, source operand 1 is the offset, while source operand 2 is the value
// to be stored referred to by rs2.
void RiscVISd(const Instruction* instruction);
void RiscVISw(const Instruction* instruction);
void RiscVISh(const Instruction* instruction);
void RiscVISb(const Instruction* instruction);

}  // namespace RV64

// The Fence instruction takes a single source operand (index 0) which consists
// of an immediate value containing the right justified concatenation of the FM,
// predecessor, and successor bit fields of the instruction.
void RiscVIFence(const Instruction* instruction);
// Ecall and EBreak take no source or destination operands.
void RiscVIEcall(const Instruction* instruction);
void RiscVIEbreak(const Instruction* instruction);
// Trap doesn't implement any specific instruction, but can be called as an
// instruction, for instance for unknown instructions.
void RiscVITrap(bool is_interrupt, uint32_t cause, Instruction* instruction);

}  // namespace riscv
}  // namespace sim
}  // namespace mpact

#endif  // THIRD_PARTY_MPACT_RISCV_RISCV_RISCV_I_INSTRUCTIONS_H_
