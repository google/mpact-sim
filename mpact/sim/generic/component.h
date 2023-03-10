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

#ifndef MPACT_SIM_GENERIC_COMPONENT_H_
#define MPACT_SIM_GENERIC_COMPONENT_H_

#include <functional>
#include <string>
#include <vector>

#include "absl/container/btree_map.h"
#include "absl/status/status.h"
#include "mpact/sim/generic/config.h"
#include "mpact/sim/generic/counters.h"
#include "mpact/sim/proto/component_data.pb.h"

namespace mpact {
namespace sim {
namespace generic {

// A Component represents an abstraction of a hierarchical block of the
// simulated architecture. It may, but is not required to correspond to a block
// in the actual hardware design. By itself it has no real functionality in the
// simulation, but acts as a named entity that can have configurations and/or
// counter instrumentation instances associated with it, as well as zero or more
// Component instances as children. In general, it is expected that there is
// only one root instance of a Component.
class Component {
 public:
  // Type alias for import done callback function.
  using CallbackFunction = std::function<void()>;
  // Create a Component with no parent.
  explicit Component(std::string name);
  // Create a Component under the given parent. Adds the component to the
  // parent's child components.
  Component(std::string name, Component *parent);
  Component() = delete;
  Component(const Component &) = delete;
  Component operator=(const Component &) = delete;
  virtual ~Component() = default;

  // Methods to add and access child components, counters and config entries.
  absl::Status AddChildComponent(Component &child);
  absl::Status AddCounter(CounterBaseInterface *counter);
  absl::Status AddConfig(ConfigBase *config);
  Component *GetChildComponent(absl::string_view name) const;
  CounterBaseInterface *GetCounter(absl::string_view name) const;
  ConfigBase *GetConfig(absl::string_view name) const;

  // Imports the ComponentData proto into the current Component, registered
  // child component instances, and registered ConfigBase instances. No values
  // are imported into counters. This method may be overridden.
  virtual absl::Status Import(
      const mpact::sim::proto::ComponentData &component_data);

  // This method is called when all imports are done. Can be overridden.
  virtual void ImportDone() const;

  // Register a callback function to be called when import is done.
  template <typename F>
  void AddImportDoneCallback(const F &callback) {
    callback_vec_.push_back(callback);
  }

  // Exports the data from the current Component instance, its' registered child
  // components, registered ConfigBase instances, and registered CounterBase
  // instances.
  absl::Status Export(proto::ComponentData *component_data);

  // Accessors.
  const std::string &component_name() const { return component_name_; }
  Component *parent() const { return parent_; }

 protected:
  // The Import method is divided into import self and import children. Each
  // can be individually overridden.
  virtual absl::Status ImportSelf(
      const mpact::sim::proto::ComponentData &component_data);
  virtual absl::Status ImportChildren(
      const mpact::sim::proto::ComponentData &component_data);

 private:
  // Using btree_map to ensure proto exports are done in name order.
  using ComponentMap = absl::btree_map<std::string, Component *>;
  using CounterMap = absl::btree_map<std::string, CounterBaseInterface *>;
  using ConfigMap = absl::btree_map<std::string, ConfigBase *>;

  // Private accessor.
  void SetParent(Component *parent) { parent_ = parent; }

  std::string component_name_;
  Component *parent_ = nullptr;

  // None of the objects pointed to by these maps are owned by this object.
  ComponentMap child_map_;
  CounterMap counter_map_;
  ConfigMap config_map_;
  std::vector<CallbackFunction> callback_vec_;
};

}  // namespace generic
}  // namespace sim
}  // namespace mpact

#endif  // MPACT_SIM_GENERIC_COMPONENT_H_
