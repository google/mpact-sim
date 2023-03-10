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

#ifndef MPACT_SIM_GENERIC_FUNCTION_DELAY_LINE_H_
#define MPACT_SIM_GENERIC_FUNCTION_DELAY_LINE_H_

// The function delay line is used to schedule the execution of a void()
// function a number of cycles (advances of the delay line) into the future. See
// delay_line_base.h for the interface to the delay line.

#include <functional>

#include "mpact/sim/generic/delay_line.h"

namespace mpact {
namespace sim {
namespace generic {

// The delay record stores the handle to the function to be executed in the
// delay line and implements an Apply() method used by the DelayLine to
// call the stored function.

class FunctionDelayRecord {
 public:
  void Apply() { fcn_(); }
  // No default constructor - only use constructor that initializes the record
  FunctionDelayRecord() = delete;

  template <typename F>
  explicit FunctionDelayRecord(F fcn) : fcn_(fcn) {}

 private:
  std::function<void()> fcn_;
};

using FunctionDelayLine = DelayLine<FunctionDelayRecord>;

}  // namespace generic
}  // namespace sim
}  // namespace mpact

#endif  // MPACT_SIM_GENERIC_FUNCTION_DELAY_LINE_H_
