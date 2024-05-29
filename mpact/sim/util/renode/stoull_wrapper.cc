// Copyright 2024 Google LLC
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

#include "mpact/sim/util/renode/stoull_wrapper.h"

#include <cstddef>
#include <cstdint>
#include <string>

#include "absl/status/status.h"
#include "absl/status/statusor.h"

// Google3 does not allow for exceptions, however, std:stoull can throw
// exceptions when no number can be parsed. This wraps these exceptions,
// translating them to absl::StatusOr<> and prevents exceptions from bleeding
// through.

namespace mpact {
namespace sim {
namespace util {
namespace renode {
namespace internal {

absl::StatusOr<uint64_t> stoull(const std::string &str, size_t *indx,
                                int base) {
  uint64_t value;
  try {
    value = std::stoull(str, indx, base);
  } catch (const std::invalid_argument &e) {
    return absl::InvalidArgumentError("Conversion failed");
  } catch (const std::out_of_range &e) {
    return absl::OutOfRangeError("Conversion failed");
  } catch (...) {
    return absl::InternalError("Oops");
  }
  return value;
}

}  // namespace internal
}  // namespace renode
}  // namespace util
}  // namespace sim
}  // namespace mpact
