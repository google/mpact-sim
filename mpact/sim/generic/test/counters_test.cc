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

#include "mpact/sim/generic/counters.h"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"
#include "absl/types/variant.h"
#include "googlemock/include/gmock/gmock.h"
#include "googletest/include/gtest/gtest.h"
#include "mpact/sim/proto/component_data.pb.h"
#include "src/google/protobuf/text_format.h"

namespace {

using ::mpact::sim::generic::CounterBaseInterface;
using ::mpact::sim::generic::CounterValue;
using ::mpact::sim::generic::FunctionCounter;
using ::mpact::sim::generic::SimpleCounter;
using ::mpact::sim::proto::ComponentData;
using ::mpact::sim::proto::ComponentValueEntry;

constexpr char kSimpleCounterName[] = "TestCounter";
constexpr char kSimpleCounterAbout[] = "This is the about for TestCounter";
constexpr int64_t kMinusFive = -5;

constexpr char kInt64CounterName[] = "int64_counter";
constexpr char kUint64CounterName[] = "uint64_counter";
constexpr char kDoubleCounterName[] = "double_counter";

constexpr int64_t kInt64Value = -123;
constexpr uint64_t kUint64Value = 456;
constexpr double kDoubleValue = 0.25;

constexpr char kProtoValue[] = R"pb(
  statistics { name: "int64_counter" sint64_value: -123 }
  statistics { name: "uint64_counter" uint64_value: 456 }
  statistics { name: "double_counter" double_value: 0.25 }
)pb";

// This is a class with a function operator that is used in the
// test of the FunctionCounter where the input and output types are the same.
// The class computes the current maximum of the input values it is called with,
// returning true only if the output parameter is updated with a new value.
template <typename T>
class Max {
 public:
  bool operator()(const T &in, T *out) {
    if (value_.has_value() && (in <= *value_)) return false;
    value_ = in;
    *out = in;
    return true;
  }

 private:
  std::optional<T> value_;
};

// This is a class with a function operator that updates the output parameter
// with the number of times it has been called, always returning true. This
// is used in the FunctionCounter test where the input type is not the same
// as the output type.
class Count {
 public:
  template <typename T>
  bool operator()(const T &in, int64_t *out) {
    *out = ++value_;
    return true;
  }

 private:
  int64_t value_ = 0;
};

// Constructs a counter and tests initial values, setters and getters of
// the base class.
TEST(CountersTest, CounterBaseInterface) {
  std::string myname("this_is_a_name");
  char myname2[] = "this_is_also_a_name";
  SimpleCounter<int64_t> counterone(myname, "about this counter");
  SimpleCounter<int64_t> countertwo(myname2);
  SimpleCounter<int64_t> int64_counter(kSimpleCounterName);
  EXPECT_EQ(int64_counter.GetName(), kSimpleCounterName);
  EXPECT_EQ(int64_counter.GetAbout(), "");
  int64_counter.SetAbout(kSimpleCounterAbout);
  EXPECT_EQ(int64_counter.GetAbout(), kSimpleCounterAbout);
  EXPECT_TRUE(int64_counter.IsEnabled());
  int64_counter.SetIsEnabled(false);
  EXPECT_FALSE(int64_counter.IsEnabled());
  int64_counter.SetIsEnabled(true);
  EXPECT_TRUE(int64_counter.IsEnabled());
}

// Checks the value interface for an int64 counter.
TEST(CountersTest, SimpleInt64Counter) {
  SimpleCounter<int64_t> int64_counter(kSimpleCounterName);
  // Verify that initial value is the default int64 constructed value.
  EXPECT_EQ(int64_counter.GetValue(), int64_t());
  // Verify that it returns a struct with an int64 value available.
  CounterValue cv = int64_counter.GetCounterValue();
  EXPECT_TRUE(std::holds_alternative<int64_t>(cv));
  EXPECT_FALSE(std::holds_alternative<uint64_t>(cv));
  EXPECT_FALSE(std::holds_alternative<double>(cv));
  EXPECT_EQ(std::get<int64_t>(cv), int64_t());
}

// Checks the value interface for a uint64 counter.
TEST(CountersTest, SimpleUint64Counter) {
  SimpleCounter<uint64_t> uint64_counter(kSimpleCounterName);
  // Verify that initial value is the default int64 constructed value.
  EXPECT_EQ(uint64_counter.GetValue(), uint64_t());
  // Verify that it returns a struct with an uint64 value available.
  CounterValue cv = uint64_counter.GetCounterValue();
  EXPECT_FALSE(std::holds_alternative<int64_t>(cv));
  EXPECT_TRUE(std::holds_alternative<uint64_t>(cv));
  EXPECT_FALSE(std::holds_alternative<double>(cv));
  EXPECT_EQ(std::get<uint64_t>(cv), uint64_t());
}

// Checks the value interface for a double counter.
TEST(CountersTest, SimpleDoubleCounter) {
  SimpleCounter<double> double_counter(kSimpleCounterName);
  // Verify that initial value is the default int64 constructed value.
  EXPECT_EQ(double_counter.GetValue(), double());
  // Verify that it returns a struct with an uint64 value available.
  CounterValue cv = double_counter.GetCounterValue();
  EXPECT_FALSE(std::holds_alternative<int64_t>(cv));
  EXPECT_FALSE(std::holds_alternative<uint64_t>(cv));
  EXPECT_TRUE(std::holds_alternative<double>(cv));
  EXPECT_EQ(std::get<double>(cv), double());
}

// Verifies that the counter is properly initialized with the initial value.
TEST(CountersTest, SimpleCounterInitialValue) {
  SimpleCounter<int64_t> int64_counter(kSimpleCounterName, kMinusFive);
  EXPECT_EQ(int64_counter.GetValue(), kMinusFive);
  CounterValue cv = int64_counter.GetCounterValue();
  EXPECT_EQ(std::get<int64_t>(cv), kMinusFive);
  EXPECT_EQ(int64_counter.ToString(), absl::StrCat(kMinusFive));
  EXPECT_EQ(static_cast<mpact::sim::generic::CounterValueOutputBase<int64_t> *>(
                &int64_counter)
                ->ToString(),
            absl::StrCat(kMinusFive));
}

// Tests the SetValue call in the input interface.
TEST(CountersTest, SimpleCounterSetValue) {
  SimpleCounter<int64_t> int64_counter(kSimpleCounterName);
  for (int i = 0; i < 10; i++) {
    int64_counter.SetValue(i);
    EXPECT_EQ(int64_counter.GetValue(), i);
    EXPECT_EQ(int64_counter.ToString(), absl::StrCat(i));
  }
}

// Tests the increment/decrement calls in the extended input interface.
TEST(CountersTest, SimpleCounterIncrementDecrement) {
  SimpleCounter<int64_t> int64_counter(kSimpleCounterName, 0);
  int64_t value = 0;
  EXPECT_EQ(int64_counter.GetValue(), value);
  for (int i = 0; i < 5; i++) {
    int64_counter.Increment(i);
    value += i;
    EXPECT_EQ(int64_counter.GetValue(), value);
  }
  for (int i = 5; i < 10; i++) {
    int64_counter.Decrement(i);
    value -= i;
    EXPECT_EQ(int64_counter.GetValue(), value);
  }
}

// Tests that the listener functionality works when using SetValue,
// Increment and Decrement calls.
TEST(CountersTest, ListenerTest) {
  SimpleCounter<int64_t> leader("Leader", 1);
  SimpleCounter<int64_t> listener("Listener", 0);
  leader.AddListener(&listener);
  EXPECT_NE(leader.GetValue(), listener.GetValue());
  leader.SetValue(kMinusFive);
  EXPECT_EQ(leader.GetValue(), listener.GetValue());
  leader.Increment(kMinusFive);
  EXPECT_EQ(leader.GetValue(), listener.GetValue());
  leader.Decrement(kMinusFive);
  EXPECT_EQ(leader.GetValue(), listener.GetValue());
}

// Tests the function counter with input type = output type (int64).
TEST(CountersTest, FunctionMaxTest) {
  std::vector<double> values = {1.1, 5.2, 3.3, 9.4, 2.5, 0.6};
  // Pass in default constructed instance of Max<double>.
  FunctionCounter<double> max("max", Max<double>());
  for (auto const &val : values) {
    max.SetValue(val);
  }
  // Verify that the computed max is the same as that computed by max_element.
  EXPECT_EQ(max.GetValue(), *std::max_element(values.begin(), values.end()));
}

// Tests the function counter with input type (double) != output type (int64).
TEST(CountersTest, FunctionCountTest) {
  std::vector<double> values = {1.1, 5.2, 3.3, 9.4, 2.5, 0.6};
  // Pass in default constructed instance of Count<double>.
  FunctionCounter<double, int64_t> count("count", Count());
  for (auto const &val : values) {
    count.SetValue(val);
  }
  // Verify that the element count is the same as the vector size.
  EXPECT_EQ(count.GetValue(), values.size());
  EXPECT_EQ(count.ToString(), absl::StrCat(values.size()));
}

// Tests export of counter values to proto message.
TEST(CountersTest, ExportTest) {
  SimpleCounter<uint64_t> uint64_counter(kUint64CounterName,
                                         "About this counter", kUint64Value);
  SimpleCounter<int64_t> int64_counter(kInt64CounterName, kInt64Value);
  SimpleCounter<double> double_counter(kDoubleCounterName, kDoubleValue);

  // Add counters to vector of CounterBaseInterface. The motivation is that this
  // is an intended use case for reading out the values of the counters and
  // exporting it to a proto in order to save the simulation results at the end
  // of a run.
  std::vector<CounterBaseInterface *> counter_vector;
  counter_vector.push_back(&int64_counter);
  counter_vector.push_back(&uint64_counter);
  counter_vector.push_back(&double_counter);

  auto exported_proto = std::make_unique<ComponentData>();
  ComponentValueEntry *entry;
  for (auto const &counter : counter_vector) {
    entry = exported_proto->add_statistics();
    EXPECT_TRUE(counter->Export(entry).ok());
  }

  // Ensure that the proto is parsed correctly.
  ComponentData from_text;
  EXPECT_TRUE(
      google::protobuf::TextFormat::ParseFromString(kProtoValue, &from_text));
}

}  // namespace
