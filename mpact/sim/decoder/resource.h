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

#ifndef MPACT_SIM_DECODER_RESOURCE_H_
#define MPACT_SIM_DECODER_RESOURCE_H_

#include <string>

#include "absl/container/btree_map.h"
#include "absl/container/btree_set.h"
#include "absl/status/statusor.h"

namespace mpact {
namespace sim {
namespace machine_description {
namespace instruction_set {

class ResourceFactory;

// This is a descriptor of a resource declared in the ISA description. It
// contains its name (and Pascal case name), and whether the resource is
// simple (never acquired after the initial cycle, nor released until the
// final cycle (or operand latency), or complex (otherwise). It also
// keeps track of whether the resource is multi_valued (e.g., is a resource
// "class") such as GP registers.
class Resource {
  friend class ResourceFactory;

 public:
  // Constructor is private. No default constructor.
  Resource() = delete;
  ~Resource() = default;

  bool is_simple() const { return is_simple_; }
  void set_is_simple(bool value) { is_simple_ = value; }
  bool is_multi_valued() const { return is_multi_valued_; }
  void set_is_multi_valued(bool value) { is_multi_valued_ = value; }

  const std::string &name() const { return name_; }
  const std::string &pascal_name() const { return pascal_name_; }

 private:
  explicit Resource(std::string name);
  std::string name_;
  std::string pascal_name_;
  bool is_multi_valued_ = false;
  bool is_simple_ = true;
};

// Resource factory class. This is used so that there's a single registry of
// resources.
class ResourceFactory {
 public:
  using ResourceMap = absl::btree_map<std::string, Resource *>;
  using ArgumentSet = absl::btree_set<std::string>;

  ResourceFactory() = default;
  ~ResourceFactory();

  // If the resource doesn't yet exist, create a new resource and return the
  // pointer, otherwise return an error code.
  absl::StatusOr<Resource *> CreateResource(absl::string_view name);
  // Return the named resource, or if it does not exist, create it, and return
  // a pointer to the newly created resource.
  Resource *GetOrInsertResource(absl::string_view name);
  const ResourceMap &resource_map() const { return resource_map_; }

 private:
  ResourceMap resource_map_;
};

}  // namespace instruction_set
}  // namespace machine_description
}  // namespace sim
}  // namespace mpact

#endif  // MPACT_SIM_DECODER_RESOURCE_H_
