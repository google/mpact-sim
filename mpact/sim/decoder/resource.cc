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

#include "mpact/sim/decoder/resource.h"

#include <string>
#include <utility>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "mpact/sim/decoder/format_name.h"

namespace mpact {
namespace sim {
namespace machine_description {
namespace instruction_set {

Resource::Resource(std::string name) : name_(name) {
  pascal_name_ = ToPascalCase(name);
}

ResourceFactory::~ResourceFactory() {
  for (auto& [unused, resource_ptr] : resource_map_) {
    delete resource_ptr;
  }
  resource_map_.clear();
}

absl::StatusOr<Resource*> ResourceFactory::CreateResource(
    absl::string_view name) {
  if (resource_map_.contains(name)) {
    return absl::AlreadyExistsError(
        absl::StrCat("Resource '", name, "' already exists"));
  }
  auto* resource = new Resource(std::string(name));
  resource_map_.insert(std::make_pair(resource->name(), resource));
  return resource;
}

Resource* ResourceFactory::GetOrInsertResource(absl::string_view name) {
  auto iter = resource_map_.find(name);
  if (iter != resource_map_.end()) return iter->second;
  auto result = CreateResource(name);
  if (!result.ok()) return nullptr;
  return result.value();
}

}  // namespace instruction_set
}  // namespace machine_description
}  // namespace sim
}  // namespace mpact
