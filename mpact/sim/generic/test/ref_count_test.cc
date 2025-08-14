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

#include "mpact/sim/generic/ref_count.h"

#include "googlemock/include/gmock/gmock.h"  // IWYU pragma: keep
#include "googletest/include/gtest/gtest.h"

namespace mpact {
namespace sim {
namespace generic {
namespace {

// Test class that counts calls to the destructor and OnRefCountIsZero.
class TestRefCount : public ReferenceCount {
 public:
  TestRefCount(int* destructor_call_count, int* refcount_zero_call_count)
      : destructor_call_count_(destructor_call_count),
        refcount_zero_call_count_(refcount_zero_call_count) {}

  TestRefCount() = delete;

  ~TestRefCount() override {
    if (nullptr != destructor_call_count_) {
      ++(*destructor_call_count_);
    }
  }

  void OnRefCountIsZero() override {
    if (nullptr != refcount_zero_call_count_) {
      ++(*refcount_zero_call_count_);
    }
    ReferenceCount::OnRefCountIsZero();
  }

 private:
  int* destructor_call_count_;
  int* refcount_zero_call_count_;
};

// Call constructor and ensure that the refcount is initialized to 1.
TEST(RefCount, Create) {
  ReferenceCount* ref_count = new ReferenceCount();
  EXPECT_EQ(ref_count->ref_count(), 1);
  ref_count->DecRef();
}

// Test that ref count increases when Inc is called.
TEST(RefCount, IncRef) {
  ReferenceCount* ref_count = new ReferenceCount();
  EXPECT_EQ(ref_count->ref_count(), 1);
  ref_count->IncRef();
  EXPECT_EQ(ref_count->ref_count(), 2);
  ref_count->DecRef();
  EXPECT_EQ(ref_count->ref_count(), 1);
  ref_count->DecRef();
}

// Test that the override OnRefCountIsZero is called and that the number
// of destructor calls matches that count.

TEST(RefCount, OnRefCountIsZero) {
  int destructor_call_count = 0;
  int refcount_zero_call_count = 0;
  TestRefCount* test_ref_count =
      new TestRefCount(&destructor_call_count, &refcount_zero_call_count);

  EXPECT_EQ(test_ref_count->ref_count(), 1);
  test_ref_count->DecRef();
  EXPECT_EQ(refcount_zero_call_count, 1);
  EXPECT_EQ(destructor_call_count, 1);
}

}  // namespace
}  // namespace generic
}  // namespace sim
}  // namespace mpact
