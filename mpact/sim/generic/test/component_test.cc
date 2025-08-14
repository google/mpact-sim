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

#include <cstdint>
#include <memory>

#include "absl/status/status.h"
#include "googlemock/include/gmock/gmock.h"  // IWYU pragma: keep
#include "googletest/include/gtest/gtest.h"
#include "mpact/sim/generic/config.h"
#include "mpact/sim/generic/counters.h"
#include "mpact/sim/proto/component_data.pb.h"
#include "src/google/protobuf/text_format.h"

namespace {

using ::mpact::sim::generic::Component;
using ::mpact::sim::generic::Config;
using ::mpact::sim::generic::SimpleCounter;
using ::mpact::sim::proto::ComponentData;

constexpr char kTopName[] = "top";
constexpr char kChildName[] = "child";
constexpr char kSecondChildName[] = "second_child";

constexpr char kInt64CounterName[] = "int64_counter";
constexpr int64_t kInt64CounterValue = -123;

constexpr char kUint64CounterName[] = "uint64_counter";
constexpr uint64_t kUint64CounterValue = 456;

constexpr char kInt64ConfigName[] = "int64_config";
constexpr int64_t kInt64ConfigValue = -456;
constexpr int64_t kInt64ConfigImportValue = -654;

constexpr char kUint64ConfigName[] = "uint64_config";
constexpr uint64_t kUint64ConfigValue = 123;
constexpr uint64_t kUint64ConfigImportValue = 321;

// Proto to import.
constexpr char kImportProto[] = R"pb(
  name: "top"
  configuration { name: "int64_config" sint64_value: -654 }
  statistics { name: "int64_counter" sint64_value: -321 }
  component_data {
    name: "child"
    configuration { name: "uint64_config" uint64_value: 321 }
    statistics { name: "uint64_counter" uint64_value: 654 }
  }
)pb";

constexpr char kImportProtoMalformed[] = R"pb(
  name: "top"
  configuration { sint64_value: -654 }
  statistics { name: "int64_counter" sint64_value: -321 }
  component_data {
    name: "child"
    configuration { name: "uint64_config" uint64_value: 321 }
    statistics { name: "uint64_counter" uint64_value: 654 }
  }
)pb";

constexpr char kImportProtoChildNameMissing[] = R"pb(
  name: "top"
  configuration { name: "int64_config" sint64_value: -654 }
  statistics { name: "int64_counter" sint64_value: -321 }
  component_data {
    configuration { name: "uint64_config" uint64_value: 321 }
    statistics { name: "uint64_counter" uint64_value: 654 }
  }
)pb";

constexpr char kImportProtoNameMissing[] = R"pb(
  configuration { name: "int64_config" sint64_value: -654 }
  statistics { name: "int64_counter" sint64_value: -321 }
  component_data {
    name: "child"
    configuration { name: "uint64_config" uint64_value: 321 }
    statistics { name: "uint64_counter" uint64_value: 654 }
  }
)pb";

constexpr char kImportProtoNameMismatch[] = R"pb(
  name: "not_top"
  configuration { name: "int64_config" sint64_value: -654 }
  statistics { name: "int64_counter" sint64_value: -321 }
  component_data {
    name: "child"
    configuration { name: "uint64_config" uint64_value: 321 }
    statistics { name: "uint64_counter" uint64_value: 654 }
  }
)pb";

// Test fixture. Allocates two components, two counters and two config items.
// The counters and config items are not added to the components in the fixture,
// but rather in each test as needed.
class ComponentTest : public testing::Test {
 protected:
  ComponentTest()
      : top_(kTopName),
        child_(kChildName, &top()),
        int64_counter_(kInt64CounterName, kInt64CounterValue),
        uint64_counter_(kUint64CounterName, kUint64CounterValue),
        int64_config_(kInt64ConfigName, kInt64ConfigValue),
        uint64_config_(kUint64ConfigName, kUint64ConfigValue) {}

  Component& top() { return top_; }
  Component& child() { return child_; }
  SimpleCounter<int64_t>& int64_counter() { return int64_counter_; }
  SimpleCounter<uint64_t>& uint64_counter() { return uint64_counter_; }
  SimpleCounter<uint64_t>& uninitialized_counter() {
    return uninitialized_counter_;
  }
  Config<int64_t>& int64_config() { return int64_config_; }
  Config<uint64_t>& uint64_config() { return uint64_config_; }

 private:
  Component top_;
  Component child_;
  SimpleCounter<int64_t> int64_counter_;
  SimpleCounter<uint64_t> uint64_counter_;
  SimpleCounter<uint64_t> uninitialized_counter_;
  Config<int64_t> int64_config_;
  Config<uint64_t> uint64_config_;
};

// Verify that the basic constructor works.
TEST_F(ComponentTest, Basic) { EXPECT_EQ(top().component_name(), kTopName); }

// Verify that the constructor that added the child component works. Also
// add another child component outside that constructor.
TEST_F(ComponentTest, ChildComponent) {
  EXPECT_EQ(child().component_name(), kChildName);
  EXPECT_EQ(child().parent(), &top());
  EXPECT_EQ(top().GetChildComponent(kChildName), &child());
  Component second_child(kSecondChildName);
  EXPECT_TRUE(child().AddChildComponent(second_child).ok());
  EXPECT_EQ(second_child.parent(), &child());
  ASSERT_EQ(child().GetChildComponent(kSecondChildName), &second_child);
  EXPECT_EQ(
      top().GetChildComponent(kChildName)->GetChildComponent(kSecondChildName),
      &second_child);
}

// Add counter objects to the two components. Ensure that the counters are
// found in the expected component.
TEST_F(ComponentTest, ComponentsWithCounters) {
  auto* top_counter = &int64_counter();
  auto* child_counter = &uint64_counter();
  auto* uninit_counter = &uninitialized_counter();

  EXPECT_TRUE(top().AddCounter(top_counter).ok());
  EXPECT_TRUE(child().AddCounter(child_counter).ok());
  EXPECT_TRUE(absl::IsInvalidArgument(top().AddCounter(uninit_counter)));

  EXPECT_EQ(top().GetCounter(kInt64CounterName), top_counter);
  EXPECT_EQ(top().GetCounter(kUint64CounterName), nullptr);
  EXPECT_EQ(child().GetCounter(kInt64CounterName), nullptr);
  EXPECT_EQ(child().GetCounter(kUint64CounterName), child_counter);
}

// Add config objects to the two components. Ensure that the config objects
// are found in the expected component.
TEST_F(ComponentTest, ComponentsWithConfigs) {
  auto* top_config = &int64_config();
  auto* child_config = &uint64_config();

  EXPECT_TRUE(top().AddConfig(top_config).ok());
  EXPECT_TRUE(child().AddConfig(child_config).ok());

  EXPECT_EQ(top().GetConfig(kInt64ConfigName), top_config);
  EXPECT_EQ(top().GetConfig(kUint64ConfigName), nullptr);
  EXPECT_EQ(child().GetConfig(kInt64ConfigName), nullptr);
  EXPECT_EQ(child().GetConfig(kUint64ConfigName), child_config);
}

// Verify the import done callback propagates.
TEST_F(ComponentTest, ImportDoneCallback) {
  bool top_flag = false;
  bool child_flag = false;
  top().AddImportDoneCallback([&top_flag]() -> void { top_flag = true; });
  child().AddImportDoneCallback([&child_flag]() -> void { child_flag = true; });
  top().ImportDone();
  EXPECT_TRUE(top_flag);
  EXPECT_TRUE(child_flag);
}

// Add config entries and counters to the components with their default values,
// then export the components to a proto, and verify the proto against the
// expected value.
TEST_F(ComponentTest, ExportTest) {
  auto* top_config = &int64_config();
  auto* child_config = &uint64_config();
  EXPECT_TRUE(top().AddConfig(top_config).ok());
  EXPECT_TRUE(child().AddConfig(child_config).ok());

  auto* top_counter = &int64_counter();
  auto* child_counter = &uint64_counter();
  EXPECT_TRUE(top().AddCounter(top_counter).ok());
  EXPECT_TRUE(child().AddCounter(child_counter).ok());

  auto exported_proto = std::make_unique<ComponentData>();
  EXPECT_TRUE(top().Export(exported_proto.get()).ok());

  EXPECT_TRUE(absl::IsInvalidArgument(top().Export(nullptr)));
}

// Failed import due to missing top-level name.
TEST_F(ComponentTest, ImportTestNameMissing) {
  auto* top_config = &int64_config();
  auto* child_config = &uint64_config();
  EXPECT_TRUE(top().AddConfig(top_config).ok());
  EXPECT_TRUE(child().AddConfig(child_config).ok());

  auto* top_counter = &int64_counter();
  auto* child_counter = &uint64_counter();
  EXPECT_TRUE(top().AddCounter(top_counter).ok());
  EXPECT_TRUE(child().AddCounter(child_counter).ok());

  ComponentData from_text;
  EXPECT_TRUE(google::protobuf::TextFormat::ParseFromString(
      kImportProtoNameMissing, &from_text));
  // Perform the import, expect failure.
  EXPECT_TRUE(absl::IsInternal(top().Import(from_text)));
}

// Failed import due to malformed component data.
TEST_F(ComponentTest, ImportTestMalformed) {
  auto* top_config = &int64_config();
  auto* child_config = &uint64_config();
  EXPECT_TRUE(top().AddConfig(top_config).ok());
  EXPECT_TRUE(child().AddConfig(child_config).ok());

  auto* top_counter = &int64_counter();
  auto* child_counter = &uint64_counter();
  EXPECT_TRUE(top().AddCounter(top_counter).ok());
  EXPECT_TRUE(child().AddCounter(child_counter).ok());

  ComponentData from_text;
  EXPECT_TRUE(google::protobuf::TextFormat::ParseFromString(
      kImportProtoMalformed, &from_text));
  // Perform the import, expect failure.
  EXPECT_TRUE(absl::IsInternal(top().Import(from_text)));
}

// Failed import due to missing child name.
TEST_F(ComponentTest, ImportTestChildNameMissing) {
  auto* top_config = &int64_config();
  auto* child_config = &uint64_config();
  EXPECT_TRUE(top().AddConfig(top_config).ok());
  EXPECT_TRUE(child().AddConfig(child_config).ok());

  auto* top_counter = &int64_counter();
  auto* child_counter = &uint64_counter();
  EXPECT_TRUE(top().AddCounter(top_counter).ok());
  EXPECT_TRUE(child().AddCounter(child_counter).ok());

  ComponentData from_text;
  EXPECT_TRUE(google::protobuf::TextFormat::ParseFromString(
      kImportProtoChildNameMissing, &from_text));
  // Perform the import, expect failure.
  EXPECT_TRUE(absl::IsInternal(top().Import(from_text)));
}

// Failed import due to top level name mismatch.
TEST_F(ComponentTest, ImportTestNameMismatch) {
  auto* top_config = &int64_config();
  auto* child_config = &uint64_config();
  EXPECT_TRUE(top().AddConfig(top_config).ok());
  EXPECT_TRUE(child().AddConfig(child_config).ok());

  auto* top_counter = &int64_counter();
  auto* child_counter = &uint64_counter();
  EXPECT_TRUE(top().AddCounter(top_counter).ok());
  EXPECT_TRUE(child().AddCounter(child_counter).ok());

  ComponentData from_text;
  EXPECT_TRUE(google::protobuf::TextFormat::ParseFromString(
      kImportProtoNameMismatch, &from_text));
  // Perform the import, expect failure.
  EXPECT_TRUE(absl::IsInternal(top().Import(from_text)));
}

// Import a proto into the components. Verify that the value of config entries
// are updated, but the counters are not.
TEST_F(ComponentTest, ImportTest) {
  auto* top_config = &int64_config();
  auto* child_config = &uint64_config();
  EXPECT_TRUE(top().AddConfig(top_config).ok());
  EXPECT_TRUE(child().AddConfig(child_config).ok());

  auto* top_counter = &int64_counter();
  auto* child_counter = &uint64_counter();
  EXPECT_TRUE(top().AddCounter(top_counter).ok());
  EXPECT_TRUE(child().AddCounter(child_counter).ok());

  ComponentData from_text;
  EXPECT_TRUE(
      google::protobuf::TextFormat::ParseFromString(kImportProto, &from_text));

  // Verify original values.
  EXPECT_EQ(top_config->GetValue(), kInt64ConfigValue);
  EXPECT_EQ(child_config->GetValue(), kUint64ConfigValue);
  EXPECT_EQ(top_counter->GetValue(), kInt64CounterValue);
  EXPECT_EQ(child_counter->GetValue(), kUint64CounterValue);

  // Perform the import.
  EXPECT_TRUE(top().Import(from_text).ok());

  // Verify that the config values are changed to those specified in the proto.
  EXPECT_EQ(top_config->GetValue(), kInt64ConfigImportValue);
  EXPECT_EQ(child_config->GetValue(), kUint64ConfigImportValue);
  // But the counter values shouldn't have changed on import.
  EXPECT_EQ(top_counter->GetValue(), kInt64CounterValue);
  EXPECT_EQ(child_counter->GetValue(), kUint64CounterValue);
}

}  // namespace
