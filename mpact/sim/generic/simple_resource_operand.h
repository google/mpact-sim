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

#ifndef MPACT_SIM_GENERIC_SIMPLE_RESOURCE_OPERAND_H_
#define MPACT_SIM_GENERIC_SIMPLE_RESOURCE_OPERAND_H_

#include <string>

#include "mpact/sim/generic/delay_line.h"
#include "mpact/sim/generic/resource_operand_interface.h"
#include "mpact/sim/generic/simple_resource.h"

namespace mpact {
namespace sim {
namespace generic {

// The delay record for acquiring or releasing a SimpleResourceSet, used in the
// SimpleResourceSetReleaseDelayLine class.
class SimpleResourceDelayRecord {
 public:
  SimpleResourceDelayRecord() = delete;
  explicit SimpleResourceDelayRecord(SimpleResourceSet* resource_set)
      : resource_set_(resource_set) {}

  ~SimpleResourceDelayRecord() = default;

  void Apply() { resource_set_->Release(); }

 private:
  SimpleResourceSet* resource_set_;
};

// Type def for the SimpleResourceDelayLine.
using SimpleResourceDelayLine = DelayLine<SimpleResourceDelayRecord>;

// The SimpleResourceOperand is used in Instruction instances to acquire
// resources. Each operand has a latency, after which the resources encoded in
// the SimpleResourceSet are released. An instruction may have 0, 1 or more
// SimpleResourceSetReleaseOperands. For best performance, all resources that
// are to be released in the same cycle should be put in a resource set in the
// same SimpleResourceSetReleaseOperand instance.
class SimpleResourceOperand : public ResourceOperandInterface {
 public:
  // Constructors and Destructors. Default constructor deleted.
  SimpleResourceOperand(SimpleResourceSet* resource_set, int latency,
                        SimpleResourceDelayLine* delay_line)
      : resource_set_(resource_set),
        latency_(latency),
        delay_line_(delay_line) {}
  SimpleResourceOperand(SimpleResourceSet* resource_set, int latency)
      : SimpleResourceOperand(resource_set, latency, nullptr) {}
  SimpleResourceOperand() = delete;
  ~SimpleResourceOperand() override = default;

  // If the latency is 0 release immediately, otherwise, add the resource set
  // to the resource set delay line for release after 'latency' cycles.
  inline void Release(int latency) {
    if (latency == 0) {
      resource_set_->Release();
      return;
    }
    delay_line_->Add(latency, resource_set_);
  }

  // Uses latency_.
  inline void Release() { Release(latency_); }

  bool IsFree() override { return resource_set_->IsFree(); }

  void Acquire() override {
    resource_set_->Acquire();
    // Schedule release
    delay_line_->Add(latency_, resource_set_);
  }

  std::string AsString() const override { return resource_set_->AsString(); }

  // Accessor.
  void set_delay_line(SimpleResourceDelayLine* delay_line) {
    delay_line_ = delay_line;
  }

  SimpleResourceSet* resource_set() const { return resource_set_; }
  int latency() const { return latency_; }

 private:
  // Pointer to the resource set that will be released.
  SimpleResourceSet* resource_set_ = nullptr;
  // Latency of the release. 0 = immediate, 1 = before next cycle, etc.
  int latency_;
  // Pointer to the delay line to be used.
  SimpleResourceDelayLine* delay_line_;
};

}  // namespace generic
}  // namespace sim
}  // namespace mpact

#endif  // MPACT_SIM_GENERIC_SIMPLE_RESOURCE_OPERAND_H_
