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

#ifndef MPACT_SIM_GENERIC_SIMPLE_RESOURCE_H_
#define MPACT_SIM_GENERIC_SIMPLE_RESOURCE_H_

#include <list>
#include <string>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "mpact/sim/generic/resource_bitset.h"

namespace mpact {
namespace sim {
namespace generic {

// A simple resource belongs to a SimpleResourcePool. It can be in two
// states: reserved or free, indicated by a single bit (1 - reserved, 0 - free)
// in a bit vector. While the exact semantics of reserved and free depends on
// the underlying use of the classes, the initial intended use is to have
// reserved mean that a write is pending to a register or an empty fifo to
// indicate when to stall (or hold) issue of an instruction due to RAW (or WAW)
// dependencies. A SimpleResource instance can be attached to a the object it
// represents so that the object can mark the resource free (or reserved) based
// on the state of the object. For instance, for a register/fifo, the instance
// may Release() the resource when the write lands.

// A SimpleResource is part of a SimpleResourcePool that represents the set of
// resources as a bit vector. This makes it easy to specify a set of required
// resources as a SimpleResourceSet and test for availability with bitwise AND.

// A SimpleResourceSet can be associated with an instruction to indicate the set
// of resources that must be available prior to issue. The issue logic then
// tests against the resource pool to see if any of the required resources are
// reserved or not.

class SimpleResourcePool;

// The SimpleResource class models a single simulated resource that can have
// two states: free or reserved. It is "simple" because it cannot be reserved
// for cycles in the future. Any reserve/free action takes effect immediately.
class SimpleResource {
  friend class SimpleResourcePool;

 private:
  // The constructor is private. It is called from a SimpleResourcePool
  // instance in which the resource belongs.
  SimpleResource(absl::string_view name, int index, SimpleResourcePool *pool);
  SimpleResource() = delete;
  virtual ~SimpleResource() = default;

 public:
  // Mark the resource reserved in the associated resource pool.
  void Acquire();
  // Mark the resource free in the associated resource pool.
  void Release();
  // Return true if the resource is not marked reserved in the associated
  // resource pool.
  bool IsFree() const;

  // Returns the "one hot" bitvector for the resource.
  const ResourceBitSet &resource_bit() const { return resource_bit_; }

  // The bit index of the resource in the bitvector.
  int index() const { return index_; }

  // Other accessors.
  const std::string &name() const { return name_; }
  const SimpleResourcePool *pool() const { return pool_; }

 private:
  ResourceBitSet resource_bit_;
  std::string name_;
  int index_;
  SimpleResourcePool *pool_;
};

// The SimpleResourceSet models a set of individual SimpleResources that are
// reserved/released/checked (CanReserve()) at the same time.
class SimpleResourceSet {
  friend class SimpleResourcePool;

 private:
  explicit SimpleResourceSet(SimpleResourcePool *);
  SimpleResourceSet() = delete;
  virtual ~SimpleResourceSet() = default;

 public:
  // Adds the resource to the resource set. Return error if the resource
  // belongs to a different pool.
  absl::Status AddResource(SimpleResource *resource);
  absl::Status AddResource(absl::string_view name);

  // Mark the resources in the set reserved in the associated resource pool.
  void Acquire();

  // Mark the resources free in the associated resource pool.
  void Release();

  // Return true if the resource set is not marked reserved in the associated
  // resource pool.
  bool IsFree() const;

  std::string AsString() const;

  // Return the bit vector for the resources in this set.
  const ResourceBitSet &resource_vector() const { return resource_vector_; }

 private:
  ResourceBitSet resource_vector_;
  SimpleResourcePool *pool_;
};

// The SimpleResourcePool is a class for managing a group of SimpleResource
// instances. The resource pool is meant to group together resources that
// typically would be checked/reserved/freed at the same.
class SimpleResourcePool {
 public:
  // Create a named resource pool with the given maximum size.
  SimpleResourcePool(absl::string_view name, int width);
  SimpleResourcePool() = delete;
  virtual ~SimpleResourcePool();

  // Add a named resource to the resource pool. This will assign the resource
  // an index in the set of named resources in the pool.
  absl::Status AddResource(absl::string_view name);

  // Return the SimpleResource pointer of the named resource, or nullptr if it
  // hasn't been added.
  SimpleResource *GetResource(absl::string_view name) const;
  // Return the SimpleResource pointer of resource with bit index 'index', or
  // nullptr if it hasn't been added.
  SimpleResource *GetResource(unsigned index) const;
  // If the resource does not exisit, add it. Return a pointer to the named
  // resource.
  SimpleResource *GetOrAddResource(absl::string_view name);
  // Create an resource set for the current resource pool.
  SimpleResourceSet *CreateResourceSet();

  // Return true if the resources in the resource/resource set are not reserved
  // in the resource pool.
  bool IsFree(const SimpleResourceSet *resource_set) const;
  bool IsFree(const SimpleResource *resource) const;

  // Mark the resource/resource set reserved in the resource pool.
  void Acquire(const SimpleResourceSet *resource_set);
  void Acquire(const SimpleResource *resource);

  // Mark the resource/resource set unreserved in the resource pool.
  void Release(const SimpleResourceSet *resource_set);
  void Release(const SimpleResource *resource);

  // List reserved resources as string.
  std::string ReservedAsString() const;

  // Accessors.
  const std::string &name() const { return name_; }
  const ResourceBitSet &resource_vector() const { return resource_vector_; }

  // The width is the max number of resources (i.e., bitwidth of the resource
  // vector).
  unsigned width() const { return width_; }

 private:
  absl::StatusOr<SimpleResource *> AddResourceInternal(absl::string_view name);
  absl::flat_hash_map<std::string, SimpleResource *> resource_name_map_;
  std::vector<SimpleResource *> resources_;
  std::list<SimpleResourceSet *> resource_sets_;
  ResourceBitSet resource_vector_;
  std::string name_;
  unsigned width_;
};

}  // namespace generic
}  // namespace sim
}  // namespace mpact

#endif  // MPACT_SIM_GENERIC_SIMPLE_RESOURCE_H_
