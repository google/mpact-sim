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

#ifndef MPACT_SIM_GENERIC_VARIANT_HELPER_H_
#define MPACT_SIM_GENERIC_VARIANT_HELPER_H_

#include <type_traits>
#include <variant>

#include "absl/types/variant.h"

namespace mpact {
namespace sim {
namespace generic {

// Template function that evaluates to true_type if the type T is in the
// first I types of absl::variant<> V. It recursively evaluates the logical or
// of type T being the type at position I-1 in variant V and type T being in the
// first I-1 types of variant V. The base case is that an empty variant has no
// type and evaluates to false_type. The default value of template parameter I
// is the size of the variant type, so that IsMemberOfVariant<V,T> will evaluate
// to true_type if T is in any of the positions 0.. sizeof(V)-1 in V.
template <typename V, typename T, int I = absl::variant_size<V>::value>
struct IsMemberOfVariant
    : public std::integral_constant<
          bool, std::is_same<T, typename std::variant_alternative<
                                    I - 1, V>::type>::value ||
                    IsMemberOfVariant<V, T, I - 1>::value> {};
// Partial specialization of the template function that evaluates to false_type
// for when T is not found in the variant type V.
template <typename V, typename T>
struct IsMemberOfVariant<V, T, 0> : public std::false_type {};

}  // namespace generic
}  // namespace sim
}  // namespace mpact

#endif  // MPACT_SIM_GENERIC_VARIANT_HELPER_H_
