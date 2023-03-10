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

#ifndef MPACT_SIM_GENERIC_DELAY_LINE_INTERFACE_H_
#define MPACT_SIM_GENERIC_DELAY_LINE_INTERFACE_H_

// The type-agnostic interface to a delay line used in a loop to advance
// all the created (and registered) delay lines without regards to the
// records in the delay line or the actual Apply() action.

namespace mpact {
namespace sim {
namespace generic {

class DelayLineInterface {
 public:
  virtual ~DelayLineInterface() = default;
  // Advances the delay line and returns the number of valid elements remaining.
  virtual int Advance() = 0;
  // Return true if the delay line is empty.
  virtual bool IsEmpty() const = 0;
};

}  // namespace generic
}  // namespace sim
}  // namespace mpact

#endif  // MPACT_SIM_GENERIC_DELAY_LINE_INTERFACE_H_
