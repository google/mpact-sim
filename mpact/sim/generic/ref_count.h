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

#ifndef MPACT_SIM_GENERIC_REF_COUNT_H_
#define MPACT_SIM_GENERIC_REF_COUNT_H_

#include "absl/base/macros.h"

// Class to manage reference counting of simulation objects
// that does not const declare IncRef, DecRef and OnRefCountIsZero

namespace mpact {
namespace sim {
namespace generic {

// This class is thread safe.
class ReferenceCount {
 public:
  // Constructor and destructor
  ReferenceCount() : inc_count_(1), dec_count_(0) {}
  virtual ~ReferenceCount() = default;

  // Add to the increment count of the object.
  void IncRef() { inc_count_++; }

  // Add to the decrement count. If increment count and decrement count are
  // equal, call the OnRefCountIsZero method, which may be overridden in derived
  // classes.
  void DecRef() {
    ABSL_HARDENING_ASSERT(inc_count_ - dec_count_ > 0);
    dec_count_++;
    if (inc_count_ == dec_count_) {
      OnRefCountIsZero();
    }
  }

  // The default action upon hitting reference count of zero is to delete the
  // object.
  virtual void OnRefCountIsZero() { delete this; }

  // The reference count is equal to inc_count - dec_count.
  int ref_count() const { return inc_count_ - dec_count_; }

 private:
  int inc_count_;
  int dec_count_;
};

}  // namespace generic
}  // namespace sim
}  // namespace mpact

#endif  // MPACT_SIM_GENERIC_REF_COUNT_H_
