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

#include "mpact/sim/generic/config.h"

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "absl/container/btree_map.h"
#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "googlemock/include/gmock/gmock.h"
#include "googletest/include/gtest/gtest.h"
#include "mpact/sim/proto/component_data.pb.h"
#include "src/google/protobuf/text_format.h"
#include "src/google/protobuf/util/message_differencer.h"

namespace {

using ::mpact::sim::generic::Config;
using ::mpact::sim::generic::ConfigBase;
using ::mpact::sim::generic::ConfigValue;
using ::mpact::sim::proto::ComponentData;
using ::mpact::sim::proto::ComponentValueEntry;

constexpr char kBoolConfigName[] = "BoolConfigName";
constexpr char kInt64ConfigName[] = "Int64ConfigName";
constexpr char kUint64ConfigName[] = "Uint64ConfigName";
constexpr char kDoubleConfigName[] = "DoubleConfigName";
constexpr char kStringConfigName[] = "StringConfigName";
constexpr char kProtoValue[] = R"pb(
  configuration { name: "BoolConfigName" bool_value: true }
  configuration { name: "Int64ConfigName" sint64_value: -123 }
  configuration { name: "Uint64ConfigName" uint64_value: 123 }
  configuration { name: "DoubleConfigName" double_value: 0.25 }
  configuration { name: "StringConfigName" string_value: "string value" }
)pb";
constexpr char kProtoNoName[] = R"pb(
  configuration { sint64_value: -123 }
  configuration { uint64_value: 123 }
  configuration { double_value: 0.25 }
  configuration { string_value: "string value" }
  configuration { bool_value: true }
)pb";
constexpr char kProtoWrongName[] = R"pb(
  configuration { name: "ConfigNameWrong" }
)pb";
constexpr char kProtoWrongValues[] = R"pb(
  configuration { name: "BoolConfigName" sint64_value: -123 }
  configuration { name: "Int64ConfigName" uint64_value: 123 }
  configuration { name: "Uint64ConfigName" double_value: 0.25 }
  configuration { name: "DoubleConfigName" string_value: "string value" }
  configuration { name: "StringConfigName" bool_value: true }
)pb";
constexpr bool kBoolValue = true;
constexpr int64_t kInt64Value = -123;
constexpr uint64_t kUint64Value = 123;
constexpr double kDoubleValue = 0.25;
constexpr char kStringValue[] = "string value";

// Simple test of construction and name property.
TEST(ConfigTest, BaseConstruction) {
  Config<bool> bool_config(kBoolConfigName);
  EXPECT_EQ(bool_config.name(), kBoolConfigName);
  EXPECT_FALSE(bool_config.HasConfigValue());
  Config<int64_t> int64_config(kInt64ConfigName);
  EXPECT_EQ(int64_config.name(), kInt64ConfigName);
  EXPECT_FALSE(int64_config.HasConfigValue());
  Config<uint64_t> uint64_config(kUint64ConfigName);
  EXPECT_EQ(uint64_config.name(), kUint64ConfigName);
  EXPECT_FALSE(uint64_config.HasConfigValue());
  Config<double> double_config(kDoubleConfigName);
  EXPECT_EQ(double_config.name(), kDoubleConfigName);
  EXPECT_FALSE(double_config.HasConfigValue());
  Config<std::string> string_config(kStringConfigName);
  EXPECT_EQ(string_config.name(), kStringConfigName);
  EXPECT_FALSE(string_config.HasConfigValue());
}

// Testing that the value can be set and retrieved using the variant type.
TEST(ConfigTest, ConfigValue) {
  ConfigValue input_value;
  ConfigValue output_value;

  Config<bool> bool_config(kBoolConfigName);
  input_value = ConfigValue(kBoolValue);
  EXPECT_TRUE(bool_config.SetConfigValue(input_value).ok());
  EXPECT_TRUE(bool_config.HasConfigValue());
  output_value = bool_config.GetConfigValue();
  EXPECT_EQ(std::get<bool>(output_value), kBoolValue);

  Config<int64_t> int64_config(kInt64ConfigName);
  input_value = ConfigValue(kInt64Value);
  EXPECT_TRUE(int64_config.SetConfigValue(input_value).ok());
  EXPECT_TRUE(int64_config.HasConfigValue());
  output_value = int64_config.GetConfigValue();
  EXPECT_EQ(std::get<int64_t>(output_value), kInt64Value);

  Config<uint64_t> uint64_config(kUint64ConfigName);
  input_value = ConfigValue(kUint64Value);
  EXPECT_TRUE(uint64_config.SetConfigValue(input_value).ok());
  EXPECT_TRUE(uint64_config.HasConfigValue());
  output_value = uint64_config.GetConfigValue();
  EXPECT_EQ(std::get<uint64_t>(output_value), kUint64Value);

  Config<double> double_config(kDoubleConfigName);
  input_value = ConfigValue(kDoubleValue);
  EXPECT_TRUE(double_config.SetConfigValue(input_value).ok());
  EXPECT_TRUE(double_config.HasConfigValue());
  output_value = double_config.GetConfigValue();
  EXPECT_EQ(std::get<double>(output_value), kDoubleValue);

  Config<std::string> string_config(kStringConfigName);
  input_value = ConfigValue(std::string(kStringValue));
  EXPECT_TRUE(string_config.SetConfigValue(input_value).ok());
  EXPECT_TRUE(string_config.HasConfigValue());
  output_value = string_config.GetConfigValue();
  EXPECT_EQ(std::get<std::string>(output_value), kStringValue);
}

// Testing that error is returned if a ConfigValue with the wrong type
// in the variant is passed int.
TEST(ConfigTest, WrongConfigValueType) {
  ConfigValue input_value;
  Config<bool> bool_config(kBoolConfigName);
  input_value = ConfigValue(kInt64Value);
  EXPECT_TRUE(absl::IsInvalidArgument(bool_config.SetConfigValue(input_value)));
}

// Testing that the initial values can be retrieved correctly.
TEST(ConfigTest, InitialValue) {
  Config<bool> bool_config(kBoolConfigName, kBoolValue);
  EXPECT_EQ(bool_config.GetValue(), kBoolValue);

  Config<int64_t> int64_config(kInt64ConfigName, kInt64Value);
  EXPECT_EQ(int64_config.GetValue(), kInt64Value);

  Config<uint64_t> uint64_config(kUint64ConfigName, kUint64Value);
  EXPECT_EQ(uint64_config.GetValue(), kUint64Value);

  Config<double> double_config(kDoubleConfigName, kDoubleValue);
  EXPECT_EQ(double_config.GetValue(), kDoubleValue);

  Config<std::string> string_config(kStringConfigName,
                                    std::string(kStringValue));
  EXPECT_EQ(string_config.GetValue(), kStringValue);
}

// Testing that the value can be set and retrieved using the typed call.
TEST(ConfigTest, TypedValue) {
  Config<bool> bool_config(kBoolConfigName);
  bool_config.SetValue(kBoolValue);
  EXPECT_EQ(bool_config.GetValue(), kBoolValue);

  Config<int64_t> int64_config(kInt64ConfigName);
  int64_config.SetValue(kInt64Value);
  EXPECT_EQ(int64_config.GetValue(), kInt64Value);

  Config<uint64_t> uint64_config(kUint64ConfigName);
  uint64_config.SetValue(kUint64Value);
  EXPECT_EQ(uint64_config.GetValue(), kUint64Value);

  Config<double> double_config(kDoubleConfigName);
  double_config.SetValue(kDoubleValue);
  EXPECT_EQ(double_config.GetValue(), kDoubleValue);

  Config<std::string> string_config(kStringConfigName);
  string_config.SetValue(std::string(kStringValue));
  EXPECT_EQ(string_config.GetValue(), kStringValue);
}

// Tests that the config entries correctly exports to a proto.
TEST(ConfigTest, ProtoExport) {
  Config<bool> bool_config(kBoolConfigName, kBoolValue);
  Config<int64_t> int64_config(kInt64ConfigName, kInt64Value);
  Config<uint64_t> uint64_config(kUint64ConfigName, kUint64Value);
  Config<double> double_config(kDoubleConfigName, kDoubleValue);
  Config<std::string> string_config(kStringConfigName,
                                    std::string(kStringValue));

  // Add config entries to vector of ConfigBase. Part of the motivation here
  // is that this is an intended use case for reading out the configuration
  // and exporting it to a proto. Saving the configuration after a simulation
  // is useful in post analysis to be able to tie together the configuration
  // information and the collected statistics.
  std::vector<ConfigBase *> config_vector;
  config_vector.push_back(&bool_config);
  config_vector.push_back(&int64_config);
  config_vector.push_back(&uint64_config);
  config_vector.push_back(&double_config);
  config_vector.push_back(&string_config);

  auto exported_proto = std::make_unique<ComponentData>();
  ComponentValueEntry *entry;
  for (auto const &config : config_vector) {
    entry = exported_proto->add_configuration();
    EXPECT_TRUE(config->Export(entry).ok());
  }

  // Ensure that the proto is parsed correctly.
  ComponentData fromText;
  EXPECT_TRUE(
      google::protobuf::TextFormat::ParseFromString(kProtoValue, &fromText));
  // The proto parsed from the string should be equal to that exported from
  // the configuration entries.
  EXPECT_TRUE(google::protobuf::util::MessageDifferencer::Equals(
      fromText, *exported_proto));
}

// Tests that the config entries correctly import values from a proto.
TEST(ConfigTest, ProtoImport) {
  Config<bool> bool_config(kBoolConfigName);
  Config<int64_t> int64_config(kInt64ConfigName);
  Config<uint64_t> uint64_config(kUint64ConfigName);
  Config<double> double_config(kDoubleConfigName);
  Config<std::string> string_config(kStringConfigName);

  // Add the configuration entries to a map from name to ConfigBase. This is to
  // mimic a use case where a proto is read in for a software component and the
  // values are imported into its configuration entries (stored in a registry).
  absl::btree_map<std::string, ConfigBase *> config_map;
  config_map.insert(std::make_pair(bool_config.name(), &bool_config));
  config_map.insert(std::make_pair(int64_config.name(), &int64_config));
  config_map.insert(std::make_pair(uint64_config.name(), &uint64_config));
  config_map.insert(std::make_pair(double_config.name(), &double_config));
  config_map.insert(std::make_pair(string_config.name(), &string_config));

  ComponentData fromText;
  // Parse the proto from text description.
  EXPECT_TRUE(
      google::protobuf::TextFormat::ParseFromString(kProtoValue, &fromText));
  // For each configuration entry, look up a config entry with a matching name
  // and import the proto value to the config.
  for (int index = 0; index < fromText.configuration_size(); index++) {
    const ComponentValueEntry &entry = fromText.configuration(index);
    if (entry.has_name() && config_map.contains(entry.name())) {
      ConfigBase *config = config_map.at(entry.name());
      EXPECT_TRUE(config->Import(&entry).ok());
    }
  }

  // Verify that the values are those specified in the proto.
  EXPECT_EQ(bool_config.GetValue(), kBoolValue);
  EXPECT_EQ(int64_config.GetValue(), kInt64Value);
  EXPECT_EQ(uint64_config.GetValue(), kUint64Value);
  EXPECT_EQ(double_config.GetValue(), kDoubleValue);
  EXPECT_EQ(string_config.GetValue(), kStringValue);
}

// Negative proto import test - nullptr entry.
TEST(ConfigTest, ImportFailNullProto) {
  Config<bool> bool_config(kBoolConfigName);
  Config<int64_t> int64_config(kInt64ConfigName);
  Config<uint64_t> uint64_config(kUint64ConfigName);
  Config<double> double_config(kDoubleConfigName);
  Config<std::string> string_config(kStringConfigName);

  std::vector<ConfigBase *> config_vector;
  config_vector.push_back(&bool_config);
  config_vector.push_back(&int64_config);
  config_vector.push_back(&uint64_config);
  config_vector.push_back(&double_config);
  config_vector.push_back(&string_config);

  // Expect each import to fail with invalid argument.
  for (auto *config : config_vector) {
    EXPECT_TRUE(absl::IsInvalidArgument(config->Import(nullptr)));
  }
}

// Negative proto import test - no name in configuration value.
TEST(ConfigTest, ImportFailNoNameInProto) {
  Config<bool> bool_config(kBoolConfigName);
  Config<int64_t> int64_config(kInt64ConfigName);
  Config<uint64_t> uint64_config(kUint64ConfigName);
  Config<double> double_config(kDoubleConfigName);
  Config<std::string> string_config(kStringConfigName);

  std::vector<ConfigBase *> config_vector;
  config_vector.push_back(&bool_config);
  config_vector.push_back(&int64_config);
  config_vector.push_back(&uint64_config);
  config_vector.push_back(&double_config);
  config_vector.push_back(&string_config);

  ComponentData fromText;
  // Parse the proto from text description.
  EXPECT_TRUE(
      google::protobuf::TextFormat::ParseFromString(kProtoNoName, &fromText));
  // Expect each import to fail with internal error.
  const ComponentValueEntry &entry = fromText.configuration(0);
  for (int index = 0; index < fromText.configuration_size(); index++) {
    EXPECT_TRUE(absl::IsInternal(config_vector[index]->Import(&entry)));
  }
}

// Negative proto import test - wrong names in configuration value.
TEST(ConfigTest, ImportFailWrongNameInProto) {
  Config<bool> bool_config(kBoolConfigName);
  Config<int64_t> int64_config(kInt64ConfigName);
  Config<uint64_t> uint64_config(kUint64ConfigName);
  Config<double> double_config(kDoubleConfigName);
  Config<std::string> string_config(kStringConfigName);

  std::vector<ConfigBase *> config_vector;
  config_vector.push_back(&bool_config);
  config_vector.push_back(&int64_config);
  config_vector.push_back(&uint64_config);
  config_vector.push_back(&double_config);
  config_vector.push_back(&string_config);

  ComponentData fromText;
  // Parse the proto from text description.
  EXPECT_TRUE(google::protobuf::TextFormat::ParseFromString(kProtoWrongName,
                                                            &fromText));
  // Expect each import to fail with internal error, as the proto message
  // passed in has a name that doesn't match the configuration entry.
  for (int index = 0; index < fromText.configuration_size(); index++) {
    const ComponentValueEntry &entry = fromText.configuration(index);
    EXPECT_TRUE(absl::IsInternal(config_vector[index]->Import(&entry)));
  }
}

// Negative proto import test - wrong value fields in the proto.
TEST(ConfigTest, ImportFailWrongValue) {
  Config<bool> bool_config(kBoolConfigName);
  Config<int64_t> int64_config(kInt64ConfigName);
  Config<uint64_t> uint64_config(kUint64ConfigName);
  Config<double> double_config(kDoubleConfigName);
  Config<std::string> string_config(kStringConfigName);

  // Add the configuration entries to a map from name to ConfigBase. This is to
  // mimic a use case where a proto is read in for a software component and the
  // values are imported into its configuration entries (stored in a registry).
  absl::btree_map<std::string, ConfigBase *> config_map;
  config_map.insert(std::make_pair(bool_config.name(), &bool_config));
  config_map.insert(std::make_pair(int64_config.name(), &int64_config));
  config_map.insert(std::make_pair(uint64_config.name(), &uint64_config));
  config_map.insert(std::make_pair(double_config.name(), &double_config));
  config_map.insert(std::make_pair(string_config.name(), &string_config));

  ComponentData fromText;
  // Parse the proto from text description.
  EXPECT_TRUE(google::protobuf::TextFormat::ParseFromString(kProtoWrongValues,
                                                            &fromText));
  // For each configuration entry, look up a config entry with a matching name
  // and import the proto value to the config. Because the value fields are
  // mismatches to the type of the config entry they should all fail with
  // internal errors.
  for (int index = 0; index < fromText.configuration_size(); index++) {
    const ComponentValueEntry &entry = fromText.configuration(index);
    if (entry.has_name() && config_map.contains(entry.name())) {
      ConfigBase *config = config_map.at(entry.name());
      EXPECT_TRUE(absl::IsInternal(config->Import(&entry)));
    }
  }
}

// Tests that the value written callback is made when the config entry is
// written to.
TEST(ConfigTest, Callback) {
  Config<bool> bool_config(kBoolConfigName);
  bool it_worked = false;
  bool_config.AddValueWrittenCallback(
      [&it_worked]() -> void { it_worked = true; });
  EXPECT_FALSE(it_worked);
  bool_config.SetValue(true);
  EXPECT_TRUE(it_worked);
}

}  // namespace
