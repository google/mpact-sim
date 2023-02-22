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

#include "mpact/sim/generic/component.h"

#include <string>
#include <utility>

namespace mpact {
namespace sim {
namespace generic {

using ::mpact::sim::proto::ComponentData;
using ::mpact::sim::proto::ComponentValueEntry;

// The Get[ChildComponent, Counter, Config] methods are all identical except
// for the element data type. This template static function factors out the
// commonalities.
template <typename M, typename T>
static T *GetMapEntry(const M &map, absl::string_view name) {
  auto ptr = map.find(name);
  return (ptr == map.end()) ? nullptr : ptr->second;
}

// The Add[ChildComponent,Counter,Config] methods are all identical except
// for the element data type. This template static function factors out the
// commonalities.
template <typename M, typename T>
static absl::Status AddMapEntry(absl::string_view name, T *entry, M *map) {
  if (entry == nullptr) return absl::InvalidArgumentError("entry is nullptr");
  auto ptr = map->find(name);
  if (ptr != map->end()) {
    return absl::InternalError(
        absl::StrCat("entry with name '", name, "' already inserted"));
  }
  map->emplace(name, entry);
  return absl::OkStatus();
}

// Constructors.
Component::Component(std::string name) : component_name_(std::move(name)) {}

Component::Component(std::string name, Component *parent)
    : component_name_(std::move(name)) {
  if (parent != nullptr) {
    parent->AddChildComponent(*this).IgnoreError();
  }
}

// Call the generic static function to Add the element.
absl::Status Component::AddChildComponent(Component &child) {
  auto status = AddMapEntry(child.component_name(), &child, &child_map_);
  if (!status.ok()) return status;
  child.SetParent(this);
  return absl::OkStatus();
}
absl::Status Component::AddCounter(CounterBaseInterface *counter) {
  if (!counter->IsInitialized()) {
    return absl::InvalidArgumentError("Counter has not been initialized");
  }
  return AddMapEntry(counter->GetName(), counter, &counter_map_);
}
absl::Status Component::AddConfig(ConfigBase *config) {
  return AddMapEntry(config->name(), config, &config_map_);
}

// Call the generic static function to Get the element.
Component *Component::GetChildComponent(absl::string_view name) const {
  return GetMapEntry<ComponentMap, Component>(child_map_, name);
}
CounterBaseInterface *Component::GetCounter(absl::string_view name) const {
  return GetMapEntry<CounterMap, CounterBaseInterface>(counter_map_, name);
}
ConfigBase *Component::GetConfig(absl::string_view name) const {
  return GetMapEntry<ConfigMap, ConfigBase>(config_map_, name);
}

// Import information from the component data proto.
absl::Status Component::Import(const ComponentData &component_data) {
  // Checking that the proto name matches. Recursive calls will not generate
  // this error, but need to check at the top level.
  if (!component_data.has_name() ||
      (component_name() != component_data.name())) {
    return absl::InternalError(absl::StrCat(
        "Name mismatch on import '", component_name(), "' != '",
        (component_data.has_name() ? component_data.name() : ""), "'"));
  }
  // First import self - as this may cause new child components to be created
  // based on the values of the configuration entries.
  auto status = ImportSelf(component_data);
  if (!status.ok()) return status;
  // Then import for the child components.
  status = ImportChildren(component_data);
  if (!status.ok()) return status;
  return absl::OkStatus();
}

absl::Status Component::ImportSelf(const ComponentData &component_data) {
  for (auto const &entry : component_data.configuration()) {
    if (!entry.has_name()) {
      // The proto is malformed.
      return absl::InternalError("Missing name in component value");
    }
    ConfigBase *config = GetConfig(entry.name());
    // It's not an error if there are proto values for config entries that
    // aren't registered. Just skip and continue.
    if (config == nullptr) continue;

    auto status = config->Import(&entry);
    if (!status.ok()) return status;
  }
  return absl::OkStatus();
}

absl::Status Component::ImportChildren(const ComponentData &component_data) {
  for (auto const &child_data : component_data.component_data()) {
    if (!child_data.has_name()) {
      return absl::InternalError("Unnamed child component");
    }
    Component *child = GetChildComponent(child_data.name());
    if (child == nullptr) continue;
    auto status = child->Import(child_data);
    if (!status.ok()) return status;
  }
  return absl::OkStatus();
}

void Component::ImportDone() const {
  // Propagate down the component hierarchy.
  for (auto const &[unused, child] : child_map_) {
    child->ImportDone();
  }
  // Notify through callbacks.
  for (auto const &callback : callback_vec_) {
    callback();
  }
}

// Export the information reachable from this component to the mutable
// component data proto.
absl::Status Component::Export(ComponentData *component_data) {
  if (component_data == nullptr) {
    return absl::InvalidArgumentError("Component data is null");
  }
  component_data->set_name(component_name());
  // Export the configuration values.
  for (auto const &[unused, config_pair] : config_map_) {
    ComponentValueEntry *entry = component_data->add_configuration();
    auto status = config_pair->Export(entry);
    if (!status.ok()) return status;
  }
  // Export the counter values.
  for (auto const &[unused, counter_pair] : counter_map_) {
    ComponentValueEntry *entry = component_data->add_statistics();
    auto status = counter_pair->Export(entry);
    if (!status.ok()) return status;
  }
  // Recursively export child component data.
  for (auto const &[unused, child_pair] : child_map_) {
    ComponentData *child_data = component_data->add_component_data();
    auto status = child_pair->Export(child_data);
    if (!status.ok()) return status;
  }
  return absl::OkStatus();
}

}  // namespace generic
}  // namespace sim
}  // namespace mpact
