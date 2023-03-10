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

#ifndef MPACT_SIM_GENERIC_CONFIG_H_
#define MPACT_SIM_GENERIC_CONFIG_H_

#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "absl/status/status.h"
#include "mpact/sim/generic/variant_helper.h"
#include "mpact/sim/proto/component_data.pb.h"

// This file defines a configuration type that is intended to be used in
// a simulator to access values that are specified at run time, such as the
// depth of a fifo, etc. The idea is that configuration entries are instantiated
// in software components, optionally with a default value. Prior to the start
// of the simulation, a master configuration utility (not part of this class),
// reads in configuration data for each software component, and sets the value
// for each configuration entry accordingly. The master configuration utility
// is intended read a proto as defined in
// //proto/component_data.proto. GCL (go/gcl)
// will be one method by which this proto can be generated.

namespace mpact {
namespace sim {
namespace generic {

struct PhysicalValue {
  double value;
  proto::SIPrefix si_prefix;
  proto::SIUnit si_unit;

  // Constructors.
  explicit PhysicalValue(double value)
      : PhysicalValue(value, proto::SIPrefix::PREFIX_NONE,
                      proto::SIUnit::UNIT_NONE) {}
  PhysicalValue(double value, proto::SIUnit unit)
      : PhysicalValue(value, proto::SIPrefix::PREFIX_NONE, unit) {}
  PhysicalValue(double value_, proto::SIPrefix prefix, proto::SIUnit unit)
      : value(value_), si_prefix(prefix), si_unit(unit) {}
};

// Variant value type of the config object. Only some types are supported.
// Additional types may be added in the future. If new types are added,
// the proto definition in component_data.proto will need to be updated
// accordingly, as well as adding additional specializations for the ExportValue
// and ImportValue methods at the bottom of this file.
using ConfigValue =
    std::variant<bool, int64_t, uint64_t, double, std::string, PhysicalValue>;

// This is the base class for a configuration entry. The value is type agnostic,
// as it uses the variant class. This class is primarily intended as a handle
// to access the configuration entry from a registry where configuration entries
// of different types may be stored.
class ConfigBase {
 public:
  explicit ConfigBase(absl::string_view name) : name_(name) {}
  ConfigBase() = delete;
  ConfigBase(const ConfigBase &) = delete;
  ConfigBase &operator=(const ConfigBase &) = delete;
  virtual ~ConfigBase() = default;

  // Return true if the config value has been set since construction.
  virtual bool HasConfigValue() const = 0;
  // Variant value accessors provide type agnostic access to config value.
  virtual ConfigValue GetConfigValue() const = 0;
  virtual absl::Status SetConfigValue(const ConfigValue &) = 0;
  // Exports the Config (name and value) to the proto message.
  virtual absl::Status Export(proto::ComponentValueEntry *entry) const = 0;
  virtual absl::Status Import(const proto::ComponentValueEntry *entry) = 0;

  const std::string &name() const { return name_; }

 private:
  std::string name_;
};

// The type specific class for the configuration entry. This class is templated
// on the type of the value. However, the template argument is restricted to
// those types that are in the ConfigValue variant. The static assert in the
// class enforces this and produces a reasonable error message in case an
// instantiation with a non supported type is attempted.
template <typename T>
class Config : public ConfigBase {
  static_assert(IsMemberOfVariant<ConfigValue, T>::value,
                "Template argument type is not in ConfigValue variant");

 public:
  using ValueWrittenCallback = std::function<void()>;

  Config() = delete;
  explicit Config(absl::string_view name) : ConfigBase(name) {}
  Config(absl::string_view name, T value) : ConfigBase(name), value_(value) {}
  ~Config() override = default;

  // Return true if the value has been updated since construction.
  bool HasConfigValue() const override { return has_value_; }
  // Get and Set the value of the configuration entry using the variant value.
  // The SetConfigValue method returns an error status if the member of the
  // variant in the config_value differs in type from that of the template
  // parameter.
  ConfigValue GetConfigValue() const override {
    return ConfigValue(GetValue());
  }
  absl::Status SetConfigValue(const ConfigValue &config_value) override {
    if (!std::holds_alternative<T>(config_value)) {
      return absl::InvalidArgumentError("Invalid type in ConfigValue argument");
    }
    SetValue(std::get<T>(config_value));
    has_value_ = true;
    return absl::OkStatus();
  }
  // Get and Set the value using the value type. These are passed/returned by
  // value. This is true even in the case where a std::string might be the value
  // type. This is a) not the expected common case, and b) accessing the values
  // of the configuration entries should not be on the critical path in any
  // simulator anyway.
  T GetValue() const { return value_; }
  void SetValue(T value) {
    has_value_ = true;
    value_ = value;
    for (auto const &callback : value_written_callback_vector_) {
      callback();
    }
  }
  // Add a callback on value written. Some configuration entries may be
  // modifiable during simulation, for example, an adjustable trade-off between
  // accuracy and speed, requiring a notification when the value changes. This
  // supports this usecase. Note, the callback is made whenever the value is
  // written to, not just if it changes.
  template <typename F>
  void AddValueWrittenCallback(F callback) {
    value_written_callback_vector_.push_back(ValueWrittenCallback(callback));
  }
  // Exports the configuration entry name and value to the proto message.
  absl::Status Export(proto::ComponentValueEntry *entry) const override {
    if (entry == nullptr) return absl::InvalidArgumentError("Entry is null");
    entry->set_name(name());
    ExportValue(entry);
    return absl::OkStatus();
  }
  // Imports the configuration entry value in the proto. Returns an error if the
  // name doesn't match or the entry is nullptr.
  absl::Status Import(const proto::ComponentValueEntry *entry) override {
    if (entry == nullptr) return absl::InvalidArgumentError("Entry is null");
    if (!entry->has_name() || (entry->name() != name()))
      return absl::InternalError(
          absl::StrCat("name mismatch: '", name(), "' != '",
                       (entry->has_name() ? entry->name() : ""), "'"));
    auto status = ImportValue(entry);
    if (!status.ok()) return status;
    return absl::OkStatus();
  }

 private:
  // Note, the following two methods are declared in this class, but defined
  // in specializations outside the class, as each specialization requires
  // writing to a different field in the proto message.
  // Exports the value to the proto message.
  void ExportValue(proto::ComponentValueEntry *entry) const;
  // Imports the value from the proto message.
  absl::Status ImportValue(const proto::ComponentValueEntry *entry);
  bool has_value_ = false;
  T value_;
  std::vector<ValueWrittenCallback> value_written_callback_vector_;
};

// The following specializations for the ExportValue and ImportValue methods are
// declared as inline to avoid a warning for potentially generating an ODR
// violation.
// ExportValue() specializations for the types in the ConfigValue variant.
// NOTE: add a specialization for every new type added to the variant.
template <>
inline void Config<bool>::ExportValue(proto::ComponentValueEntry *entry) const {
  entry->set_bool_value(GetValue());
}
template <>
inline void Config<int64_t>::ExportValue(
    proto::ComponentValueEntry *entry) const {
  entry->set_sint64_value(GetValue());
}
template <>
inline void Config<uint64_t>::ExportValue(
    proto::ComponentValueEntry *entry) const {
  entry->set_uint64_value(GetValue());
}
template <>
inline void Config<double>::ExportValue(
    proto::ComponentValueEntry *entry) const {
  entry->set_double_value(GetValue());
}
template <>
inline void Config<std::string>::ExportValue(
    proto::ComponentValueEntry *entry) const {
  entry->set_string_value(GetValue());
}
template <>
inline void Config<PhysicalValue>::ExportValue(
    proto::ComponentValueEntry *entry) const {
  auto *pvalue = entry->mutable_physical_value();
  const PhysicalValue &value = GetValue();
  pvalue->set_value(value.value);
  pvalue->set_si_prefix(value.si_prefix);
  pvalue->set_si_unit(value.si_unit);
}
// ImportValue() specializations for the types in the ConfigValue variant.
// NOTE: add a specialization for every new type added to the variant.
template <>
inline absl::Status Config<bool>::ImportValue(
    const proto::ComponentValueEntry *entry) {
  if (!entry->has_bool_value()) return absl::InternalError("No valid value");
  SetValue(entry->bool_value());
  return absl::OkStatus();
}
template <>
inline absl::Status Config<int64_t>::ImportValue(
    const proto::ComponentValueEntry *entry) {
  if (!entry->has_sint64_value()) return absl::InternalError("No valid value");
  SetValue(entry->sint64_value());
  return absl::OkStatus();
}
template <>
inline absl::Status Config<uint64_t>::ImportValue(
    const proto::ComponentValueEntry *entry) {
  if (!entry->has_uint64_value()) return absl::InternalError("No valid value");
  SetValue(entry->uint64_value());
  return absl::OkStatus();
}
template <>
inline absl::Status Config<double>::ImportValue(
    const proto::ComponentValueEntry *entry) {
  if (!entry->has_double_value()) return absl::InternalError("No valid value");
  SetValue(entry->double_value());
  return absl::OkStatus();
}
template <>
inline absl::Status Config<std::string>::ImportValue(
    const proto::ComponentValueEntry *entry) {
  if (!entry->has_string_value()) return absl::InternalError("No valid value");
  SetValue(entry->string_value());
  return absl::OkStatus();
}

template <>
inline absl::Status Config<PhysicalValue>::ImportValue(
    const proto::ComponentValueEntry *entry) {
  if (!entry->has_physical_value())
    return absl::InternalError("No valid value");
  SetValue(PhysicalValue(entry->physical_value().value(),
                         entry->physical_value().si_prefix(),
                         entry->physical_value().si_unit()));
  return absl::OkStatus();
}

}  // namespace generic
}  // namespace sim
}  // namespace mpact

#endif  // MPACT_SIM_GENERIC_CONFIG_H_
