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

#ifndef MPACT_SIM_UTIL_ASM_TEST_RISCV_GETTER_HELPERS_H_
#define MPACT_SIM_UTIL_ASM_TEST_RISCV_GETTER_HELPERS_H_

#include <string>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/functional/any_invocable.h"
#include "absl/log/log.h"
#include "absl/strings/string_view.h"

// This file contains helper functions that are used to create commonly used
// operands for RiscV instructions.

namespace mpact {
namespace sim {
namespace riscv {

// Helper function to insert and entry into a "getter" map. This is used in
// the riscv_*_getter.h files.
template <typename M, typename E, typename G>
inline void Insert(M &map, E entry, G getter) {
  if (!map.contains(static_cast<int>(entry))) {
    map.insert(std::make_pair(static_cast<int>(entry), getter));
  } else {
    map.at(static_cast<int>(entry)) = getter;
  }
}

constexpr absl::string_view kXregNames[32] = {
    "x0",  "x1",  "x2",  "x3",  "x4",  "x5",  "x6",  "x7",  "x8",  "x9",  "x10",
    "x11", "x12", "x13", "x14", "x15", "x16", "x17", "x18", "x19", "x20", "x21",
    "x22", "x23", "x24", "x25", "x26", "x27", "x28", "x29", "x30", "x31"};
// ABI names for the integer registers.
constexpr absl::string_view kXregAbiNames[32] = {
    "zero", "ra", "sp", "gp", "tp",  "t0",  "t1", "t2", "s0", "s1", "a0",
    "a1",   "a2", "a3", "a4", "a5",  "a6",  "a7", "s2", "s3", "s4", "s5",
    "s6",   "s7", "s8", "s9", "s10", "s11", "t3", "t4", "t5", "t6"};
// Architectural names for the floating point registers.
constexpr absl::string_view kFregNames[32] = {
    "f0",  "f1",  "f2",  "f3",  "f4",  "f5",  "f6",  "f7",  "f8",  "f9",  "f10",
    "f11", "f12", "f13", "f14", "f15", "f16", "f17", "f18", "f19", "f20", "f21",
    "f22", "f23", "f24", "f25", "f26", "f27", "f28", "f29", "f30", "f31"};
// ABI names for the floating point registers.
constexpr absl::string_view kFregAbiNames[32] = {
    "ft0", "ft1", "ft2",  "ft3",  "ft4", "ft5", "ft6",  "ft7",
    "fs0", "fs1", "fa0",  "fa1",  "fa2", "fa3", "fa4",  "fa5",
    "fa6", "fa7", "fs2",  "fs3",  "fs4", "fs5", "fs6",  "fs7",
    "fs8", "fs9", "fs10", "fs11", "ft8", "ft9", "ft10", "ft11"};

}  // namespace riscv
}  // namespace sim
}  // namespace mpact

#endif  // MPACT_SIM_UTIL_ASM_TEST_RISCV_GETTER_HELPERS_H_
