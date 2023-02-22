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

#include <memory>

#include "googlemock/include/gmock/gmock.h"

using testing::StrEq;

namespace mpact {
namespace sim {
namespace generic {
namespace {

// Will run the test on a single resource pool, with three resource instances.
static constexpr char kTestPoolName[] = "TestPool";
static constexpr int kTestPoolSize = 35;
static constexpr int kNumResources = 3;
static constexpr char kTestResource0[] = "Resource0";
static constexpr char kTestResource1[] = "Resource1";
static constexpr char kTestResource2[] = "Resource2";
static constexpr char const *kTestResources[] = {kTestResource0, kTestResource1,
                                                 kTestResource2};

class SimpleResourceTest : public testing::Test {
 protected:
  SimpleResourceTest() {
    pool_ = std::make_unique<SimpleResourcePool>(kTestPoolName, kTestPoolSize);
  }
  ~SimpleResourceTest() override {}

  std::unique_ptr<SimpleResourcePool> pool_;
};

TEST_F(SimpleResourceTest, Instantiation) {
  EXPECT_THAT(pool_->name(), StrEq(kTestPoolName));
  EXPECT_EQ(pool_->width(), kTestPoolSize);
  EXPECT_EQ(pool_->resource_vector().GetOnesCount(), 0);
}

TEST_F(SimpleResourceTest, Resources) {
  SimpleResource *resources[kNumResources];
  for (int num = 0; num < kNumResources; num++) {
    const char *name = kTestResources[num];
    // Creat resource.
    EXPECT_TRUE(pool_->AddResource(name).ok());
    resources[num] = pool_->GetResource(name);
    EXPECT_THAT(resources[num]->name(), StrEq(name));
    // Verify resource properties.
    EXPECT_EQ(resources[num]->index(), num);
    EXPECT_EQ(resources[num]->resource_bit().GetOnesCount(), 1);
    int position;
    EXPECT_TRUE(resources[num]->resource_bit().FindFirstSetBit(&position));
    EXPECT_EQ(position, num);
    // Try reserving resource and free it.
    EXPECT_TRUE(resources[num]->IsFree());
    resources[num]->Acquire();
    EXPECT_EQ(pool_->resource_vector().GetOnesCount(), 1);
    EXPECT_TRUE(resources[num]->resource_bit().FindFirstSetBit(&position));
    EXPECT_EQ(position, num);
    EXPECT_FALSE(resources[num]->IsFree());
    resources[num]->Release();
    EXPECT_EQ(pool_->resource_vector().GetOnesCount(), 0);
  }
  for (int num = 0; num < kNumResources; num++) {
    resources[num]->Acquire();
    EXPECT_EQ(pool_->resource_vector().GetOnesCount(), num + 1);
  }
}

TEST_F(SimpleResourceTest, ResourceSets) {
  SimpleResource *resources[kNumResources];
  for (int num = 0; num < kNumResources; num++) {
    const char *name = kTestResources[num];
    // Creat resource.
    EXPECT_TRUE(pool_->AddResource(name).ok());
    resources[num] = pool_->GetResource(name);
  }

  // Create a resource set and add resource 0 and 1 to it.
  SimpleResourceSet *resource_set = pool_->CreateResourceSet();
  EXPECT_TRUE(resource_set->AddResource(kTestResource0).ok());
  EXPECT_TRUE(resource_set->AddResource(resources[1]).ok());

  // Acquire resource 2.
  resources[2]->Acquire();
  EXPECT_TRUE(resource_set->IsFree());
  // Acquire resource 1.
  resources[1]->Acquire();
  EXPECT_FALSE(resource_set->IsFree());
  // Release resource 1.
  resources[1]->Release();

  // Acquire the resource set.
  resource_set->Acquire();
  // All resources should now be reserved.
  EXPECT_EQ(pool_->resource_vector().GetOnesCount(), 3);
  EXPECT_FALSE(resources[0]->IsFree());
  EXPECT_FALSE(resources[1]->IsFree());
  EXPECT_FALSE(resources[2]->IsFree());

  // Release the resource set. Resource 2 should still be reserved.
  resource_set->Release();
  EXPECT_EQ(pool_->resource_vector().GetOnesCount(), 1);
  EXPECT_TRUE(resources[0]->IsFree());
  EXPECT_TRUE(resources[1]->IsFree());
  EXPECT_FALSE(resources[2]->IsFree());
  EXPECT_TRUE(resource_set->IsFree());
}

}  // namespace
}  // namespace generic
}  // namespace sim
}  // namespace mpact
