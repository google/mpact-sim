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

#ifndef MPACT_SIM_GENERIC_SIGNED_TYPE_H_
#define MPACT_SIM_GENERIC_SIGNED_TYPE_H_

#include <cstdint>

namespace mpact {
namespace sim {
namespace generic {
namespace internal {

// This file defines a template "function" is used to provide the signed type of
// the same size as the argument type, provided it's one of the basic integral
// types. For signed type arguments the template defines the identical type.
//
// The base case defines an empty struct while the specializations provide the
// valid type translations.
template <typename T>
struct SignedType {};
template <>
struct SignedType<bool> {
  using type = bool;
};
template <>
struct SignedType<int8_t> {
  using type = int8_t;
};
template <>
struct SignedType<uint8_t> {
  using type = int8_t;
};
template <>
struct SignedType<int16_t> {
  using type = int16_t;
};
template <>
struct SignedType<uint16_t> {
  using type = int16_t;
};
template <>
struct SignedType<int32_t> {
  using type = int32_t;
};
template <>
struct SignedType<uint32_t> {
  using type = int32_t;
};
template <>
struct SignedType<int64_t> {
  using type = int64_t;
};
template <>
struct SignedType<uint64_t> {
  using type = int64_t;
};

}  // namespace internal
}  // namespace generic
}  // namespace sim
}  // namespace mpact

#endif  // MPACT_SIM_GENERIC_SIGNED_TYPE_H_
