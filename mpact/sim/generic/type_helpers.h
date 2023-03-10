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

#ifndef MPACT_SIM_GENERIC_TYPE_HELPERS_H_
#define MPACT_SIM_GENERIC_TYPE_HELPERS_H_

#include <type_traits>

#include "absl/numeric/int128.h"

namespace mpact {
namespace sim {
namespace riscv {

// Helper template type to get the int type twice as wide.
template <typename T>
struct WideType {};
template <>
struct WideType<int8_t> {
  using type = int16_t;
};
template <>
struct WideType<int16_t> {
  using type = int32_t;
};
template <>
struct WideType<int32_t> {
  using type = int64_t;
};
template <>
struct WideType<uint8_t> {
  using type = uint16_t;
};
template <>
struct WideType<uint16_t> {
  using type = uint32_t;
};
template <>
struct WideType<uint32_t> {
  using type = uint64_t;
};
template <>
struct WideType<uint64_t> {
  // 128 bit integer type from absl acts as a uint128_t.
  using type = absl::uint128;
};
template <>
struct WideType<int64_t> {
  // 128 bit integer type from absl acts as an int128_t.
  using type = absl::int128;
};

// Helper template type to get the int type half as wide.
template <typename T>
struct NarrowType {};
template <>
struct NarrowType<int16_t> {
  using type = int8_t;
};
template <>
struct NarrowType<int32_t> {
  using type = int16_t;
};
template <>
struct NarrowType<int64_t> {
  using type = int32_t;
};
template <>
struct NarrowType<uint16_t> {
  using type = uint8_t;
};
template <>
struct NarrowType<uint32_t> {
  using type = uint16_t;
};
template <>
struct NarrowType<uint64_t> {
  using type = uint32_t;
};

// Convenience template type used to get an int/uint type of width sizeof(W)
// but with the signed/unsigned trait of S.
template <typename W, typename S, typename E = void>
struct SameSignedType {};

template <typename W, typename S>
struct SameSignedType<W, S,
                      typename std::enable_if<std::is_signed<S>::value>::type> {
  using type = typename std::make_signed<W>::type;
};
template <typename W, typename S>
struct SameSignedType<
    W, S, typename std::enable_if<std::is_unsigned<S>::value>::type> {
  using type = typename std::make_unsigned<W>::type;
};

// std::make_unsigned doesn't handle the absl 128 bit int types, so need to
// define a utility for that.
template <typename T>
struct MakeUnsigned {
  using type = typename std::make_unsigned<T>::type;
};
template <>
struct MakeUnsigned<absl::int128> {
  using type = absl::uint128;
};
template <>
struct MakeUnsigned<absl::uint128> {
  using type = absl::uint128;
};

// Helper template for floating point type information. This allows the specific
// information for each fp type to be easily extracted.
template <typename T>
struct FPTypeInfo {
  using UIntType = typename std::make_unsigned<T>::type;
  using IntType = typename std::make_signed<T>::type;
};

template <>
struct FPTypeInfo<float> {
  using T = float;
  using UIntType = uint32_t;
  using IntType = std::make_signed<UIntType>::type;
  static const int kBitSize = sizeof(float) << 3;
  static const int kExpSize = 8;
  static const int kExpBias = 127;
  static const int kSigSize = kBitSize - kExpSize - /*sign*/ 1;
  static const UIntType kInfMask = (1ULL << (kBitSize - 1)) - 1;
  static const UIntType kExpMask = ((1ULL << kExpSize) - 1) << kSigSize;
  static const UIntType kSigMask = (1ULL << kSigSize) - 1;
  static const UIntType kCanonicalNaN = 0b0'1111'1111'1ULL << (kSigSize - 1);
  static const UIntType kPosInf = kExpMask;
  static const UIntType kNegInf = kExpMask | (1ULL << (kBitSize - 1));
  static inline bool IsInf(T value) {
    UIntType uint_val = *reinterpret_cast<UIntType *>(&value);
    return (uint_val & kInfMask) == kPosInf;
  }
  static inline bool IsNaN(T value) {
    UIntType uint_val = *reinterpret_cast<UIntType *>(&value);
    return ((uint_val & kExpMask) == kExpMask) && ((uint_val & kSigMask) != 0);
  }
  static inline bool IsSNaN(T value) {
    UIntType uint_val = *reinterpret_cast<UIntType *>(&value);
    return IsNaN(value) && (((1 << (kSigSize - 1)) & uint_val) == 0);
  }
  static inline bool IsQNaN(T value) {
    UIntType uint_val = *reinterpret_cast<UIntType *>(&value);
    return IsNaN(value) && (((1 << (kSigSize - 1)) & uint_val) != 0);
  }
};

template <>
struct FPTypeInfo<double> {
  using T = double;
  using UIntType = uint64_t;
  using IntType = std::make_signed<UIntType>::type;
  static const int kBitSize = sizeof(double) << 3;
  static const int kExpSize = 11;
  static const int kExpBias = 1023;
  static const int kSigSize = kBitSize - kExpSize - /*sign*/ 1;
  static const UIntType kInfMask = (1ULL << (kBitSize - 1)) - 1;
  static const UIntType kExpMask = ((1ULL << kExpSize) - 1) << kSigSize;
  static const UIntType kSigMask = (1ULL << kSigSize) - 1;
  static const UIntType kCanonicalNaN = 0b0'1111'1111'111'1ULL
                                        << (kSigSize - 1);
  static const UIntType kPosInf = kExpMask;
  static const UIntType kNegInf = kExpMask | (1ULL << (kBitSize - 1));
  static inline bool IsInf(T value) {
    UIntType uint_val = *reinterpret_cast<UIntType *>(&value);
    return (uint_val & kInfMask) == kPosInf;
  }
  static inline bool IsNaN(T value) {
    UIntType uint_val = *reinterpret_cast<UIntType *>(&value);
    return ((uint_val & kExpMask) == kExpMask) && ((uint_val & kSigMask) != 0);
  }
  static inline bool IsSNaN(T value) {
    UIntType uint_val = *reinterpret_cast<UIntType *>(&value);
    return IsNaN(value) && (((1ULL << (kSigSize - 1)) & uint_val) == 0);
  }
  static inline bool IsQNaN(T value) {
    UIntType uint_val = *reinterpret_cast<UIntType *>(&value);
    return IsNaN(value) && (((1ULL << (kSigSize - 1)) & uint_val) != 0);
  }
};

template <>
struct FPTypeInfo<uint32_t> {
  using T = uint32_t;
  using UIntType = uint32_t;
  using IntType = std::make_signed<UIntType>::type;
  static const int kBitSize = sizeof(float) << 3;
  static const int kExpSize = 8;
  static const int kExpBias = 127;
  static const int kSigSize = kBitSize - kExpSize - /*sign*/ 1;
  static const UIntType kInfMask = (1ULL << (kBitSize - 1)) - 1;
  static const UIntType kExpMask = ((1ULL << kExpSize) - 1) << kSigSize;
  static const UIntType kSigMask = (1ULL << kSigSize) - 1;
  static const UIntType kCanonicalNaN = 0b0'1111'1111'1ULL << (kSigSize - 1);
  static const UIntType kPosInf = kExpMask;
  static const UIntType kNegInf = kExpMask | (1ULL << (kBitSize - 1));
  static inline bool IsInf(T value) { return (value & kInfMask) == kPosInf; }
  static inline bool IsNaN(T value) {
    return ((value & kExpMask) == kExpMask) && ((value & kSigMask) != 0);
  }
  static inline bool IsSNaN(T value) {
    return IsNaN(value) && (((1ULL << (kSigSize - 1)) & value) == 0);
  }
  static inline bool IsQNaN(T value) {
    return IsNaN(value) && (((1ULL << (kSigSize - 1)) & value) != 0);
  }
};

template <>
struct FPTypeInfo<int32_t> {
  using T = int32_t;
  using UIntType = uint32_t;
  using IntType = std::make_signed<UIntType>::type;
  static const int kBitSize = sizeof(float) << 3;
  static const int kExpSize = 8;
  static const int kExpBias = 127;
  static const int kSigSize = kBitSize - kExpSize - /*sign*/ 1;
  static const UIntType kInfMask = (1ULL << (kBitSize - 1)) - 1;
  static const UIntType kExpMask = ((1ULL << kExpSize) - 1) << kSigSize;
  static const UIntType kSigMask = (1ULL << kSigSize) - 1;
  static const UIntType kCanonicalNaN = 0b0'1111'1111'1ULL << (kSigSize - 1);
  static const UIntType kPosInf = kExpMask;
  static const UIntType kNegInf = kExpMask | (1ULL << (kBitSize - 1));
  static inline bool IsInf(T value) { return (value & kInfMask) == kPosInf; }
  static inline bool IsNaN(T value) {
    return ((value & kExpMask) == kExpMask) && ((value & kSigMask) != 0);
  }
  static inline bool IsSNaN(T value) {
    return IsNaN(value) && (((1ULL << (kSigSize - 1)) & value) == 0);
  }
  static inline bool IsQNaN(T value) {
    return IsNaN(value) && (((1ULL << (kSigSize - 1)) & value) != 0);
  }
};

template <>
struct FPTypeInfo<uint64_t> {
  using T = uint64_t;
  using UIntType = uint64_t;
  using IntType = std::make_signed<UIntType>::type;
  static const int kBitSize = sizeof(float) << 3;
  static const int kExpSize = 8;
  static const int kExpBias = 127;
  static const int kSigSize = kBitSize - kExpSize - /*sign*/ 1;
  static const UIntType kInfMask = (1ULL << (kBitSize - 1)) - 1;
  static const UIntType kExpMask = ((1ULL << kExpSize) - 1) << kSigSize;
  static const UIntType kSigMask = (1ULL << kSigSize) - 1;
  static const UIntType kCanonicalNaN = 0b0'1111'1111'1ULL << (kSigSize - 1);
  static const UIntType kPosInf = kExpMask;
  static const UIntType kNegInf = kExpMask | (1ULL << (kBitSize - 1));
  static inline bool IsInf(T value) { return (value & kInfMask) == kPosInf; }
  static inline bool IsNaN(T value) {
    return ((value & kExpMask) == kExpMask) && ((value & kSigMask) != 0);
  }
  static inline bool IsSNaN(T value) {
    return IsNaN(value) && (((1ULL << (kSigSize - 1)) & value) == 0);
  }
  static inline bool IsQNaN(T value) {
    return IsNaN(value) && (((1ULL << (kSigSize - 1)) & value) != 0);
  }
};

template <>
struct FPTypeInfo<int64_t> {
  using T = int64_t;
  using UIntType = uint64_t;
  using IntType = std::make_signed<UIntType>::type;
  static const int kBitSize = sizeof(float) << 3;
  static const int kExpSize = 8;
  static const int kExpBias = 127;
  static const int kSigSize = kBitSize - kExpSize - /*sign*/ 1;
  static const UIntType kInfMask = (1ULL << (kBitSize - 1)) - 1;
  static const UIntType kExpMask = ((1ULL << kExpSize) - 1) << kSigSize;
  static const UIntType kSigMask = (1ULL << kSigSize) - 1;
  static const UIntType kCanonicalNaN = 0b0'1111'1111'1ULL << (kSigSize - 1);
  static const UIntType kPosInf = kExpMask;
  static const UIntType kNegInf = kExpMask | (1ULL << (kBitSize - 1));
  static inline bool IsInf(T value) { return (value & kInfMask) == kPosInf; }
  static inline bool IsNaN(T value) {
    return ((value & kExpMask) == kExpMask) && ((value & kSigMask) != 0);
  }
  static inline bool IsSNaN(T value) {
    return IsNaN(value) && (((1ULL << (kSigSize - 1)) & value) == 0);
  }
  static inline bool IsQNaN(T value) {
    return IsNaN(value) && (((1ULL << (kSigSize - 1)) & value) != 0);
  }
};

// This templated helper function defines the dereference '*' operand for
// enumeration class types and uses it to cast the enum class member to the
// underlying type. That means for an enum class E and member e, this
// method allows you to specify '*E::e' as equivalent to
// 'static_cast<int>(E::e)'.
template <typename T>
constexpr auto operator*(T e) noexcept ->
    typename std::enable_if<std::is_enum<T>::value,
                            typename std::underlying_type<T>::type>::type {
  return static_cast<typename std::underlying_type<T>::type>(e);
}

}  // namespace riscv
}  // namespace sim
}  // namespace mpact

#endif  // MPACT_SIM_GENERIC_TYPE_HELPERS_H_
