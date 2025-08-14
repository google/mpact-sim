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

#include "mpact/sim/util/asm/test/riscv_bin_setters.h"

#include <cstdint>
#include <string>
#include <vector>

#include "absl/base/no_destructor.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "mpact/sim/generic/type_helpers.h"
#include "re2/re2.h"

namespace mpact {
namespace sim {
namespace riscv {

namespace internal {

enum class RelocType {
  kNone = 0,
  kBranch = 16,
  kJal = 17,
  kPcrelHi20 = 23,
  kPcrelLo12I = 24,
  kPcrelLo12S = 25,
  kHi20 = 26,
  kLo12I = 27,
  kLo12S = 28,
};

using ::mpact::sim::generic::operator*;  // NOLINT(misc-unused-using-decls)

absl::NoDestructor<RE2> kSymRe("^\\s*(%[a-zA-Z0-9_]+)\\s*\\(?([^)]+)\\)?\\s*$");

absl::Status RelocateAddiIImm12(uint64_t address, absl::string_view text,
                                ResolverInterface* resolver,
                                std::vector<RelocationInfo>& relocations) {
  std::string relo;
  std::string sym;
  if (!RE2::FullMatch(text, *kSymRe, &relo, &sym)) return absl::OkStatus();
  if (relo == "%lo") {
    relocations.emplace_back(0, sym, *RelocType::kLo12I, 0, 0);
    return absl::OkStatus();
  }
  if (relo == "%pcrel_lo") {
    relocations.emplace_back(0, sym, *RelocType::kPcrelLo12I, 0, 0);
    return absl::OkStatus();
  }
  if (!relo.empty()) {
    return absl::InvalidArgumentError(
        absl::StrCat("Invalid relocation: '", relo, "'"));
  }
  return absl::OkStatus();
}

absl::Status RelocateJJImm20(uint64_t address, absl::string_view text,
                             ResolverInterface* resolver,
                             std::vector<RelocationInfo>& relocations) {
  std::string relo;
  std::string sym;
  if (!RE2::FullMatch(text, *kSymRe, &relo, &sym)) return absl::OkStatus();

  relocations.emplace_back(0, sym, *RelocType::kJal, 0, 0);
  return absl::OkStatus();
}

absl::Status RelocateJrJImm12(uint64_t address, absl::string_view text,
                              ResolverInterface* resolver,
                              std::vector<RelocationInfo>& relocations) {
  return absl::OkStatus();
}

absl::Status RelocateLuiUImm20(uint64_t address, absl::string_view text,
                               ResolverInterface* resolver,
                               std::vector<RelocationInfo>& relocations) {
  std::string relo;
  std::string sym;
  if (!RE2::FullMatch(text, *kSymRe, &relo, &sym)) return absl::OkStatus();
  relocations.emplace_back(0, sym, *RelocType::kHi20, 0, 0);
  return absl::OkStatus();
}

absl::Status RelocateSdSImm12(uint64_t address, absl::string_view text,
                              ResolverInterface* resolver,
                              std::vector<RelocationInfo>& relocations) {
  std::string relo;
  std::string sym;
  if (!RE2::FullMatch(text, *kSymRe, &relo, &sym)) return absl::OkStatus();
  if (relo == "%lo") {
    relocations.emplace_back(0, sym, *RelocType::kLo12S, 0, 0);
    return absl::OkStatus();
  }
  if (relo == "%pcrel_lo") {
    relocations.emplace_back(0, sym, *RelocType::kPcrelLo12S, 0, 0);
    return absl::OkStatus();
  }
  if (!relo.empty()) {
    return absl::InvalidArgumentError(
        absl::StrCat("Invalid relocation: '", relo, "'"));
  }
  return absl::OkStatus();
}

}  // namespace internal

}  // namespace riscv
}  // namespace sim
}  // namespace mpact
