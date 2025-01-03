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

#ifndef MPACT_SIM_UTIL_ASM_TEST_RISCV_BIN_SETTERS_H_
#define MPACT_SIM_UTIL_ASM_TEST_RISCV_BIN_SETTERS_H_

#include <cstdint>
#include <initializer_list>
#include <utility>

#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "mpact/sim/util/asm/resolver_interface.h"
#include "mpact/sim/util/asm/test/riscv_getter_helpers.h"

namespace mpact {
namespace sim {
namespace riscv {

using ::mpact::sim::util::assembler::ResolverInterface;

constexpr std::initializer_list<const std::pair<absl::string_view, uint64_t>>
    kRegisterList = {
        {"x0", 0},   {"x1", 1},   {"x2", 2},   {"x3", 3},   {"x4", 4},
        {"x5", 5},   {"x6", 6},   {"x7", 7},   {"x8", 8},   {"x9", 9},
        {"x10", 10}, {"x11", 11}, {"x12", 12}, {"x13", 13}, {"x14", 14},
        {"x15", 15}, {"x16", 16}, {"x17", 17}, {"x18", 18}, {"x19", 19},
        {"x20", 20}, {"x21", 21}, {"x22", 22}, {"x23", 23}, {"x24", 24},
        {"x25", 25}, {"x26", 26}, {"x27", 27}, {"x28", 28}, {"x29", 29},
        {"x30", 30}, {"x31", 31}, {"zero", 0}, {"ra", 1},   {"sp", 2},
        {"gp", 3},   {"tp", 4},   {"t0", 5},   {"t1", 6},   {"t2", 7},
        {"s0", 8},   {"s1", 9},   {"a0", 10},  {"a1", 11},  {"a2", 12},
        {"a3", 13},  {"a4", 14},  {"a5", 15},  {"a6", 16},  {"a7", 17},
        {"s2", 18},  {"s3", 19},  {"s4", 20},  {"s5", 21},  {"s6", 22},
        {"s7", 23},  {"s8", 24},  {"s9", 25},  {"s10", 26}, {"s11", 27},
        {"t3", 28},  {"t4", 29},  {"t5", 30},  {"t6", 31}};

template <typename T>
absl::StatusOr<T> SimpleTextToInt(absl::string_view text,
                                  ResolverInterface *resolver) {
  T value;
  if (text.substr(0, 2) == "0x") {
    if (absl::SimpleHexAtoi(text.substr(2), &value)) return value;
    return absl::InvalidArgumentError(
        absl::StrCat("Invalid hexadecimal immediate: ", text));
  }
  if (absl::SimpleAtoi(text, &value)) return value;
  if (resolver != nullptr) {
    auto res = resolver->Resolve(text);
    if (!res.ok()) {
      return res.status();
    }
    return static_cast<T>(res.value());
  }
  return absl::InvalidArgumentError(absl::StrCat("Invalid argument: ", text));
}

using ValueMap = absl::flat_hash_map<absl::string_view, uint64_t>;

template <typename Enum, typename Map, typename Encoder>
void AddRiscvSourceOpBinSetters(Map &map) {
  Insert(map, *Enum::kIImm12,
         [](uint64_t address, absl::string_view text,
            ResolverInterface *resolver) -> absl::StatusOr<uint64_t> {
           auto res = SimpleTextToInt<int32_t>(text, resolver);
           if (!res.ok()) return res.status();
           return Encoder::IType::InsertImm12(res.value(), 0ULL);
         });
  Insert(map, *Enum::kIUimm6,
         [](uint64_t address, absl::string_view text,
            ResolverInterface *resolver) -> absl::StatusOr<uint64_t> {
           auto res = SimpleTextToInt<uint32_t>(text, resolver);
           if (!res.ok()) return res.status();
           return Encoder::RSType::InsertRUimm6(res.value(), 0ULL);
         });
  Insert(map, *Enum::kJImm12,
         [](uint64_t address, absl::string_view text,
            ResolverInterface *resolver) -> absl::StatusOr<uint64_t> {
           auto res = SimpleTextToInt<int32_t>(text, resolver);
           if (!res.ok()) return res.status();
           return Encoder::IType::InsertImm12(res.value(), 0ULL);
         });
  Insert(map, *Enum::kJImm20,
         [](uint64_t address, absl::string_view text,
            ResolverInterface *resolver) -> absl::StatusOr<uint64_t> {
           auto res = SimpleTextToInt<int32_t>(text, resolver);
           if (!res.ok()) return res.status();
           uint32_t delta = res.value() - address;
           auto value = Encoder::JType::InsertJImm(delta, 0ULL);
           return value;
         });
  Insert(map, *Enum::kRs1,
         [](uint64_t address, absl::string_view text,
            ResolverInterface *resolver) -> absl::StatusOr<uint64_t> {
           static ValueMap map(kRegisterList);
           auto iter = map.find(text);
           if (iter == map.end()) {
             return absl::InvalidArgumentError(
                 absl::StrCat("Invalid source operand: ", text));
           }
           return Encoder::RSType::InsertRs1(iter->second, 0ULL);
         });
  Insert(map, *Enum::kRs2,
         [](uint64_t address, absl::string_view text,
            ResolverInterface *resolver) -> absl::StatusOr<uint64_t> {
           static ValueMap map(kRegisterList);
           auto iter = map.find(text);
           if (iter == map.end()) {
             return absl::InvalidArgumentError(
                 absl::StrCat("Invalid source operand: ", text));
           }
           return Encoder::SType::InsertRs2(iter->second, 0ULL);
         });
  Insert(map, *Enum::kSImm12,
         [](uint64_t address, absl::string_view text,
            ResolverInterface *resolver) -> absl::StatusOr<uint64_t> {
           auto res = SimpleTextToInt<uint32_t>(text, resolver);
           if (!res.ok()) return res.status();
           return Encoder::SType::InsertSImm(res.value(), 0ULL);
         });
  Insert(map, *Enum::kUImm20,
         [](uint64_t address, absl::string_view text,
            ResolverInterface *resolver) -> absl::StatusOr<uint64_t> {
           auto res = SimpleTextToInt<uint32_t>(text, resolver);
           if (!res.ok()) return res.status();
           return Encoder::UType::InsertUImm(res.value(), 0ULL);
         });
}

template <typename Enum, typename Map, typename Encoder>
void AddRiscvDestOpBinSetters(Map &map) {
  Insert(map, *Enum::kRd,
         [](uint64_t address, absl::string_view text,
            ResolverInterface *resolver) -> absl::StatusOr<uint64_t> {
           static ValueMap map(kRegisterList);
           auto iter = map.find(text);
           if (iter == map.end()) {
             return absl::InvalidArgumentError(
                 absl::StrCat("Invalid destination operand: ", text));
           }
           return Encoder::RSType::InsertRd(iter->second, 0ULL);
         });
}

}  // namespace riscv
}  // namespace sim
}  // namespace mpact

#endif  // MPACT_SIM_UTIL_ASM_TEST_RISCV_BIN_SETTERS_H_
