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

#ifndef MPACT_SIM_GENERIC_COUNTERS_H_
#define MPACT_SIM_GENERIC_COUNTERS_H_

#include <cstdint>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/types/variant.h"
#include "mpact/sim/generic/counters_base.h"
#include "mpact/sim/generic/variant_helper.h"
#include "mpact/sim/proto/component_data.pb.h"

// This file defines the user facing classes for the simulator statistics
// instrumentation infrastructure. Base classes are located in counters_base.h.
// It is intended that instances of the counter classes are used to accumulate
// results locally in software components, whereas the CounterBaseInterface
// class (defined in counters_base.h) can be used to access the results in a
// type independent way, suitable for access through a registry to collect and
// process the results at the end of a simulation.
//
// While CounterBaseInterface is type agnostic, the derived classes are
// templated on the input and output data type. The output data type of a
// counter is restricted to the set of types in the absl::variant type aliased
// as CounterValue.
//
// This file defines three classes. The first is CounterValueOutputBase<T>
// which is templated, but only valid for the types in the CounterValue
// variant (see below and in counters_base.h). This class inherits from
// CounterBaseInterface and implements some of the common functionality.
//
// The second class is SimpleCounter<T>, where T is in the variant but also an
// arithmetic type (in the case a non-arithmetic type gets added to the variant
// in the future). It supports setting, incrementing and decrementing of the
// counter value.
//
// The third type is FunctionCounter<In, Out> where In and Out are the types in
// the SetValue interface (no Increment or Decrement methods are supported) and
// the CounterValueOutputBase<> class respectively. The FunctionCounter applies
// a user supplied function or functor to the input value to optionally produce
// an output value. The function/functor is of type
// std::function<bool(const In&, Out *)>, and returns true if the output value
// was updated, false otherwise.

namespace mpact {
namespace sim {
namespace generic {

// The generic value of a counter is an absl::variant containing a set of
// anticipated frequently used types that aliased to CounterValue. It is
// defined in counters_base.h as:
//   using CounterValue = absl::variant<uint64, int64, double>;
//
// Templated output base class. The template argument must be one of the types
// in the CounterValue variant, if not, the static_assert will generate an
// error. The CounterValueOutputBase<> class implements all the pure virtual
// methods from CounterBaseInterface except for ToString(). The implementations
// are declared final to allow the compiler to de-virtualize calls to these
// methods made in instances of derived classes.
template <typename T>
class CounterValueOutputBase : public CounterBaseInterface {
  // Static check to signal error when an illegal type is used.
  static_assert(mpact::sim::generic::IsMemberOfVariant<CounterValue, T>::value,
                "Template argument type is not in CounterValue variant");

 public:
  // Constructors and destructor.
  // The default constructor does not initialize the counter. A separate call
  // to Initialize(..) is required before it can be added to a component.
  CounterValueOutputBase() : is_enabled_(false), is_initialized_(false) {}
  // Constructs and initializes the counter.
  CounterValueOutputBase(std::string name, const T initial)
      : name_(std::move(name)),
        about_(),
        is_enabled_(true),
        is_initialized_(true),
        value_(std::move(initial)) {}
  ~CounterValueOutputBase() override = default;

  // Method to add an object that implements the CounterInputSetInterface<T>
  // to be added a a listener, which means that its SetValue() method will be
  // called whenever value of the counter is updated. The caller retains
  // ownership of the listener.
  void AddListener(CounterValueSetInterface<T> *listener) {
    listeners_.push_back(listener);
  }
  // Implementation of the pure virtual method from CounterBaseInterface to
  // return the value as a variant. Notice, this method is declared as final to
  // enable the compiler to "de-virtualize" the method and improve its
  // performance when called in certain circumstances.
  CounterValue GetCounterValue() const final { return CounterValue(value_); }

  // Typed getter for the counter value.
  T GetValue() const { return value_; }

  // Method for exporting the counter to proto message. Calls ExportValue
  // for the type specific export, as each different type needs to set
  // a different field in the proto message.
  absl::Status Export(
      mpact::sim::proto::ComponentValueEntry *entry) const override {
    if (entry == nullptr) return absl::InvalidArgumentError("Entry is null");
    entry->set_name(GetName());
    if (!GetAbout().empty()) {
      entry->set_about(GetAbout());
    }
    ExportValue(entry);
    return absl::OkStatus();
  }

  // Must be called before being added to a component if the counter was
  // created using the default constructor.
  void Initialize(std::string name, const T initial) {
    name_ = std::move(name);
    value_ = std::move(initial);
    is_initialized_ = true;
    is_enabled_ = true;
  }

  // Implementation of pure virtual methods inherited from CounterBaseInterface
  // that act as accessors. Keeping the names in PascalCase since they implement
  // an inherited interface.
  std::string GetName() const final { return name_; }
  std::string GetAbout() const final { return about_; }
  void SetAbout(std::string about) final { about_ = std::move(about); }
  bool IsEnabled() const final { return is_enabled_; }
  void SetIsEnabled(bool is_enabled) final { is_enabled_ = is_enabled; }
  bool IsInitialized() const final { return is_initialized_; }

 protected:
  // The setter for the counter value used by derived classes. Calls the set
  // of registered listener objects with the new value.
  void UpdateValue(T value) {
    value_ = std::move(value);
    for (auto *listener : listeners_) {
      listener->SetValue(value_);
    }
  }

 private:
  // This method exports the typed value of the counter to the appropriate
  // field in the proto message. Note, this method is defined at the bottom of
  // this file.
  void ExportValue(mpact::sim::proto::ComponentValueEntry *entry) const;
  // Objects pointed to are owned elsewhere. Their lifetimes must exceed the
  // lifetime of the counter, or at least beyond the last call to UpdateValue.
  std::vector<CounterValueSetInterface<T> *> listeners_;
  std::string name_;
  std::string about_;
  bool is_enabled_;
  bool is_initialized_;
  T value_;
};

// A simple arithmetic counter class that supports increment and decrement. Only
// enabled for arithmetic types. This class has multiple inheritance from both
// the input interface and the output base class. The input interface is pure
// virtual, so there is no multiple implementation inheritance. There is also
// no possibility of a diamond pattern. The input interface inheritance is also
// necessary should instances of this class be used as a listener to another
// counter.
template <typename T>
class SimpleCounter : public CounterValueOutputBase<T>,
                      public CounterValueIncrementInterface<T> {
  // Static checks on the template argument type. The IsMemberOfVariant<T> check
  // is repeated to simplify error messages. If it is omitted and T is not in
  // the counter type, additional error messages may be generated for each of
  // the methods inherited from CounterBaseInterface and
  // CounterValueOutputInterface.
  static_assert(mpact::sim::generic::IsMemberOfVariant<CounterValue, T>::value,
                "Template argument type T is not in CounterValue variant.");
  static_assert(std::is_arithmetic<T>::value,
                "Template argument type T must be arithmetic.");

 public:
  // Since this class derives from templated classes, calls to the base class
  // methods must be qualified or use this->. Electing to do the former with the
  // following using declarations.
  using CounterValueOutputBase<T>::IsEnabled;
  using CounterValueOutputBase<T>::UpdateValue;
  using CounterValueOutputBase<T>::GetValue;

  // Constructor and destructor.
  SimpleCounter() : CounterValueOutputBase<T>() {}
  SimpleCounter(std::string name, const T &initial)
      : CounterValueOutputBase<T>(std::move(name), initial) {}
  explicit SimpleCounter(std::string name)
      : SimpleCounter(std::move(name), T()) {}
  SimpleCounter &operator=(const SimpleCounter &) = delete;
  ~SimpleCounter() override = default;

  // Implementing the methods from the CounterValueIncrementInterface<T>. Note
  // that the methods are declared final to enable de-virtualization
  // optimizations in the compiler.
  void Increment(const T &val) final {
    if (IsEnabled()) UpdateValue(GetValue() + val);
  }
  void Decrement(const T &val) final {
    if (IsEnabled()) UpdateValue(GetValue() - val);
  }
  void SetValue(const T &val) final {
    if (IsEnabled()) UpdateValue(val);
  }

  // From CounterValueOutputInterface<T>. This method is not declared final
  // to make it possible to customize formatting of the string in derived
  // classes.
  std::string ToString() const override { return absl::StrCat(GetValue()); }
};

// A complex counter that calls a function on its input value. If the
// function returns true, then the functions' computed output value is used to
// update the counter. The function can be any function object that can be cast
// to the ProcessingFunction type, including free functions, class methods,
// or call operators on class instances. This allows the function object to
// maintain state which enables more advanced computations, such as max/min
// etc. The fact that the counter is only updated when the function returns
// true allows for input samples to be filtered. This class has multiple
// inheritance from both the input interface and the output base class. The
// input interface is pure virtual, so there is no multiple implementation
// inheritance. There is also no possibility of a diamond pattern. The input
// interface inheritance is also necessary should instances of this class be
// used as a listener to another counter.
template <typename In, typename Out = In>
class FunctionCounter : public CounterValueOutputBase<Out>,
                        public CounterValueSetInterface<In> {
  // Static checks on the template argument type. The IsMemberOfVariant<T> check
  // is repeated to simplify error messages. If it is omitted and T is not in
  // the counter type, additional error messages may be generated for each of
  // the methods inherited from CounterBaseInterface and CounterValueOutputBase.
  static_assert(
      mpact::sim::generic::IsMemberOfVariant<CounterValue, Out>::value,
      "Template argument type Out is not in CounterValue variant.");

 public:
  using ProcessingFunction = std::function<bool(const In &, Out *)>;
  // Since this class derives from templated classes, calls to the base class
  // methods must be qualified or use this->. Electing to do the former with the
  // following using declarations.
  using CounterValueOutputBase<Out>::IsEnabled;
  using CounterValueOutputBase<Out>::UpdateValue;
  using CounterValueOutputBase<Out>::GetValue;

  // Constructors and destructor. The name is mandatory for the constructor. The
  // initial value is optional and defaults to the default constructed value.
  // The processing function may be given in the constructor or assigned later.
  // If not passed in to the constructor it defaults to a function that always
  // returns false and thus never updates the counter value.
  template <typename F>
  FunctionCounter(std::string name, const Out &initial, F processing_function)
      : CounterValueOutputBase<Out>(std::move(name), initial),
        processing_function_(
            std::move(ProcessingFunction(processing_function))) {}
  template <typename F>
  FunctionCounter(std::string name, F processing_function)
      : FunctionCounter(std::move(name), Out(),
                        std::move(processing_function)) {}
  FunctionCounter(std::string name, const Out &initial)
      : FunctionCounter(std::move(name), initial,
                        [](const In &, Out *) -> bool { return false; }) {}
  explicit FunctionCounter(std::string name)
      : FunctionCounter(std::move(name), Out()) {}
  FunctionCounter &operator=(const FunctionCounter &) = delete;
  ~FunctionCounter() override = default;

  // Set the value processing function.
  template <typename F>
  void SetFunction(F fcn) {
    processing_function_ = std::move(ProcessingFunction(fcn));
  }

  // The following method is defined final to enable devirtualization
  // optimization in the compiler.
  // Process the input value and update the counter if indicated.
  void SetValue(const In &in_value) final {
    if (IsEnabled()) {
      Out out_value;
      if (processing_function_(in_value, &out_value)) UpdateValue(out_value);
    }
  }

  // From CounterValueOutputBase<T>. This method is not declared final
  // to make it possible to customize formatting of the string in derived
  // classes.
  std::string ToString() const override { return absl::StrCat(GetValue()); }

 private:
  ProcessingFunction processing_function_;
};

// Definition of ExportValue methods, one for each type in the CounterValue
// variant.
// NOTE: add a specialization for every new type added to the variant.
template <>
inline void CounterValueOutputBase<uint64_t>::ExportValue(
    mpact::sim::proto::ComponentValueEntry *entry) const {
  entry->set_uint64_value(GetValue());
}
template <>
inline void CounterValueOutputBase<int64_t>::ExportValue(
    mpact::sim::proto::ComponentValueEntry *entry) const {
  entry->set_sint64_value(GetValue());
}
template <>
inline void CounterValueOutputBase<double>::ExportValue(
    mpact::sim::proto::ComponentValueEntry *entry) const {
  entry->set_double_value(GetValue());
}

}  // namespace generic
}  // namespace sim
}  // namespace mpact

#endif  // MPACT_SIM_GENERIC_COUNTERS_H_
