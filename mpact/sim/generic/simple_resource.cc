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

#include "mpact/sim/generic/simple_resource.h"

#include <string>

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"

namespace mpact {
namespace sim {
namespace generic {

// Initialize the bitmap and set the bit specified by index.
SimpleResource::SimpleResource(absl::string_view name, int index,
                               SimpleResourcePool *pool)
    : name_(name), index_(index), pool_(pool) {
  resource_bit_.Resize(pool_->width());
  resource_bit_.Set(index);
}

// Just call into the pool to reserve/free/check.
void SimpleResource::Acquire() { pool_->Acquire(this); }

void SimpleResource::Release() { pool_->Release(this); }

bool SimpleResource::IsFree() const { return pool_->IsFree(this); }

SimpleResourceSet::SimpleResourceSet(SimpleResourcePool *pool) : pool_(pool) {
  resource_vector_.Resize(pool_->width());
}

// Verify that the resource comes from the same pool. If so, add it (union with)
// the bitmap. Return appropriate status.
absl::Status SimpleResourceSet::AddResource(SimpleResource *resource) {
  // If the resource is nullptr, just return ok status.
  if (resource == nullptr) return absl::OkStatus();
  // Make sure it belongs to the same pool as the resource set.
  if (resource->pool() != pool_) {
    return absl::Status(
        absl::StatusCode::kInternal,
        "SimpleResourceSet: Attempt to add resource from different pool");
  }
  resource_vector_.Or(resource->resource_bit());
  return absl::OkStatus();
}

// If the resource doesn't exist, add it to the resource pool, before adding it
// to the resource set.
absl::Status SimpleResourceSet::AddResource(absl::string_view name) {
  SimpleResource *resource = pool_->GetResource(name);
  if (nullptr == resource) {
    auto status = pool_->AddResource(name);
    if (!status.ok()) return status;
    resource = pool_->GetResource(name);
  }
  auto status = AddResource(resource);
  if (!status.ok()) return status;
  return absl::OkStatus();
}

// Call into the pool to reserve/free/check.
void SimpleResourceSet::Acquire() { pool_->Acquire(this); }

void SimpleResourceSet::Release() { pool_->Release(this); }

bool SimpleResourceSet::IsFree() const { return pool_->IsFree(this); }

std::string SimpleResourceSet::AsString() const {
  std::string out = "[";
  std::string sep;
  for (int index = 0; resource_vector_.FindNextSetBit(&index); ++index) {
    auto *resource = pool_->GetResource(index);
    if (resource == nullptr) {
      LOG(ERROR) << absl::StrCat("Cannot find resource (", index, ") in pool '",
                                 pool_->name(), "'");
      continue;
    }
    absl::StrAppend(&out, sep, resource->name());
    sep = ", ";
  }
  absl::StrAppend(&out, "]");
  return out;
}

SimpleResourcePool::SimpleResourcePool(absl::string_view name, int width)
    : name_(name), width_(width) {
  resource_vector_.Resize(width_);
}

SimpleResourcePool::~SimpleResourcePool() {
  for (const auto &entry : resource_name_map_) {
    delete entry.second;
  }
  resource_name_map_.clear();
  for (auto entry : resource_sets_) {
    delete entry;
  }
  resource_sets_.clear();
}

// Add named resource to the pool. If the pool has reached maximum size, return
// an error.
absl::StatusOr<SimpleResource *> SimpleResourcePool::AddResourceInternal(
    absl::string_view name) {
  if (resource_name_map_.size() == width_) {
    return absl::InternalError(absl::StrCat(
        "SimpleResourcePool: Attempted to add too many resources to pool '",
        name_, "'"));
  }
  if (resource_name_map_.contains(name)) {
    return absl::AlreadyExistsError(
        absl::StrCat("SimpleResourcePool: Resource '", name,
                     "' already exists in pool '", name_, "'"));
  }
  int index = resource_name_map_.size();
  SimpleResource *resource = new SimpleResource(name, index, this);
  resource_name_map_.emplace(name, resource);
  resources_.push_back(resource);
  return resource;
}

absl::Status SimpleResourcePool::AddResource(absl::string_view name) {
  auto result = AddResourceInternal(name);
  if (result.ok()) return absl::OkStatus();

  return result.status();
}

SimpleResource *SimpleResourcePool::GetResource(unsigned index) const {
  if ((index < 0) || (index >= resources_.size())) return nullptr;
  return resources_[index];
}

SimpleResource *SimpleResourcePool::GetResource(absl::string_view name) const {
  auto ptr = resource_name_map_.find(name);
  if (ptr == resource_name_map_.end()) return nullptr;
  return ptr->second;
}

SimpleResource *SimpleResourcePool::GetOrAddResource(absl::string_view name) {
  auto *resource = GetResource(name);
  if (resource != nullptr) return resource;

  auto result = AddResourceInternal(name);
  if (!result.ok()) {
    LOG(ERROR) << "Unable to add resource '" << name
               << "' to resource pool: " << result.status().message();
    return nullptr;
  }

  return result.value();
}

SimpleResourceSet *SimpleResourcePool::CreateResourceSet() {
  resource_sets_.push_front(new SimpleResourceSet(this));
  return resource_sets_.front();
}

// Bitmap operations to reserve, free and check resources. Union with a bitmap
// from a resource or resource set to reserve (set), difference to free (clear),
// and non-empty intersection (non-zero and) to check.
bool SimpleResourcePool::IsFree(const SimpleResourceSet *resource_set) const {
  return !resource_vector_.IsIntersectionNonEmpty(
      resource_set->resource_vector());
}

bool SimpleResourcePool::IsFree(const SimpleResource *resource) const {
  return !resource_vector_.IsIntersectionNonEmpty(resource->resource_bit());
}

void SimpleResourcePool::Acquire(const SimpleResourceSet *resource_set) {
  resource_vector_.Or(resource_set->resource_vector());
}

void SimpleResourcePool::Acquire(const SimpleResource *resource) {
  resource_vector_.Or(resource->resource_bit());
}

void SimpleResourcePool::Release(const SimpleResourceSet *resource_set) {
  resource_vector_.AndNot(resource_set->resource_vector());
}

void SimpleResourcePool::Release(const SimpleResource *resource) {
  resource_vector_.AndNot(resource->resource_bit());
}

std::string SimpleResourcePool::ReservedAsString() const {
  std::string out = "[";
  std::string sep;
  for (int index = 0; resource_vector_.FindNextSetBit(&index); ++index) {
    auto *resource = resources_[index];
    if (resource == nullptr) {
      LOG(ERROR) << absl::StrCat("Cannot find resource (", index, ") in pool '",
                                 name(), "'");
      continue;
    }
    absl::StrAppend(&out, sep, resource->name());
    sep = ", ";
  }
  absl::StrAppend(&out, "]");
  return out;
}

}  // namespace generic
}  // namespace sim
}  // namespace mpact
