#include "mpact/sim/decoder/resource.h"

#include "absl/status/statusor.h"
#include "googlemock/include/gmock/gmock.h"
#include "googletest/include/gtest/gtest.h"

namespace {

// This file contains tests for Resource and ResourceFactory.

using ::mpact::sim::machine_description::instruction_set::ResourceFactory;

constexpr char kResource1PascalName[] = "Resource1Name";
constexpr char kResource1Name[] = "resource_1_name";
constexpr char kResource2Name[] = "resource_2_name";

class ResourceTest : public ::testing::Test {
 protected:
  ResourceTest() {}
  ~ResourceTest() override {}

  ResourceFactory factory_;
};

// Test that ResourceFactory works as expected.
TEST_F(ResourceTest, Factory) {
  auto result = factory_.CreateResource(kResource1Name);
  EXPECT_TRUE(result.status().ok());
  auto result2 = factory_.CreateResource(kResource1Name);
  EXPECT_TRUE(absl::IsAlreadyExists(result2.status()));
  auto* resource1 = factory_.GetOrInsertResource(kResource1Name);
  EXPECT_EQ(result.value(), resource1);
  auto* resource2 = factory_.GetOrInsertResource(kResource2Name);
  EXPECT_NE(resource2, nullptr);
  EXPECT_NE(resource2, resource1);
  auto result3 = factory_.CreateResource(kResource2Name);
  EXPECT_TRUE(absl::IsAlreadyExists(result3.status()));

  EXPECT_EQ(factory_.resource_map().find(kResource1Name)->second, resource1);
  EXPECT_EQ(factory_.resource_map().find(kResource2Name)->second, resource2);
}

// Test initial state of a new resource.
TEST_F(ResourceTest, ResourceInitial) {
  auto* resource = factory_.GetOrInsertResource(kResource1Name);
  EXPECT_TRUE(resource->is_simple());
  EXPECT_FALSE(resource->is_multi_valued());
  EXPECT_EQ(resource->name(), kResource1Name);
  EXPECT_EQ(resource->pascal_name(), kResource1PascalName);
}

// Test resource setters.
TEST_F(ResourceTest, ResourceSetters) {
  auto* resource = factory_.GetOrInsertResource(kResource1Name);
  resource->set_is_simple(false);
  resource->set_is_multi_valued(true);
  EXPECT_FALSE(resource->is_simple());
  EXPECT_TRUE(resource->is_multi_valued());
  resource->set_is_simple(true);
  resource->set_is_multi_valued(false);
  EXPECT_TRUE(resource->is_simple());
  EXPECT_FALSE(resource->is_multi_valued());
}

}  // namespace
