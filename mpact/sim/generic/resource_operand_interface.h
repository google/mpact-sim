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

#ifndef GENERIC_RESOURCE_OPERAND_INTERFACE_H_
#define GENERIC_RESOURCE_OPERAND_INTERFACE_H_

#include <string>

namespace mpact {
namespace sim {
namespace generic {

// The resource operand interface factors out the interface functions
// necessary to check resource availability and acquire the necessary
// resources at instruction issue time, regardless of the resource type
// implementation.
class ResourceOperandInterface {
 public:
  virtual ~ResourceOperandInterface() = default;

  // Returns true if the resource is free and can be acquired.
  virtual bool IsFree() = 0;
  // Acquire the resource - returns OkStatus if successful. The resource will
  // be freed automatically according to the underlying resource implementation.
  virtual void Acquire() = 0;
  // Printable representation.
  virtual std::string AsString() const = 0;
};

}  // namespace generic
}  // namespace sim
}  // namespace mpact

#endif  // GENERIC_RESOURCE_OPERAND_INTERFACE_H_
