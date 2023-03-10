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

#ifndef MPACT_SIM_GENERIC_COUNTERS_BASE_H_
#define MPACT_SIM_GENERIC_COUNTERS_BASE_H_

#include <cstdint>
#include <string>
#include <variant>

#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "absl/types/variant.h"
#include "mpact/sim/proto/component_data.pb.h"

namespace mpact {
namespace sim {
namespace generic {

// The generic value of a counter is an absl::variant. That means it can
// contain a value of any one of the types listed in the template. In time
// additional types will be added as needed.
using CounterValue = std::variant<uint64_t, int64_t, double>;

// Base class for a counter. This base class does not have any information
// about the type of the input or output of a counter, but can be used to
// obtain the variant containing the value or a string representation of the
// value.
class CounterBaseInterface {
 public:
  virtual ~CounterBaseInterface() = default;

  // Pure virtual methods to access the counter value in a type agnostic way.
  virtual std::string ToString() const = 0;
  virtual CounterValue GetCounterValue() const = 0;

  // Enables/disables the counter. This functionality is intended to be used to
  // limit the collection of statistics only to those regions or intervals of
  // the simulation that is of interest.
  virtual void SetIsEnabled(bool is_enabled) = 0;
  virtual bool IsEnabled() const = 0;
  // Returns true if the counter has been properly initialized.
  virtual bool IsInitialized() const = 0;

  // Exports the counter values to the proto message.
  virtual absl::Status Export(
      mpact::sim::proto::ComponentValueEntry *entry) const = 0;

  // Access name and set/get about string.
  virtual std::string GetName() const = 0;
  virtual void SetAbout(std::string about) = 0;
  virtual std::string GetAbout() const = 0;
};

// Templated input interface of a counter. Allows a counter to be assigned a new
// value of type T. This template does not limit the range of legal types, as
// the type of the actual value stored is determined by the
// CounterValueOutputBase<> template class, and need not be the same as the
// input type.
template <typename T>
class CounterValueSetInterface {
 public:
  virtual ~CounterValueSetInterface() = default;
  virtual void SetValue(const T &val) = 0;
};

// Extended input interface of a counter. Adds methods to increment and
// decrement the value of the counter.
template <typename T>
class CounterValueIncrementInterface : public CounterValueSetInterface<T> {
 public:
  ~CounterValueIncrementInterface() override = default;
  virtual void Increment(const T &val) = 0;
  virtual void Decrement(const T &val) = 0;
};

}  // namespace generic
}  // namespace sim
}  // namespace mpact

#endif  // MPACT_SIM_GENERIC_COUNTERS_BASE_H_
