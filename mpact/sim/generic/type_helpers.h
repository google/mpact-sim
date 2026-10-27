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

#include <cstdint>
#include <string>
#include <type_traits>

#include "absl/base/casts.h"
#include "absl/numeric/int128.h"

namespace mpact {
namespace sim {
namespace generic {

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

// Make a half floating point type since it isn't native to C++.
struct HalfFP {
  uint16_t value;
};

// Represents a 16-bit brain floating-point (bfloat16) value (1 sign bit,
// 8 exponent bits, 7 mantissa bits).
struct Bfloat16 {
  uint16_t value;
};

// Represents an 8-bit floating-point value in E4M3 format (1 sign bit,
// 4 exponent bits, 3 mantissa bits).
struct Fp8E4m3 {
  uint8_t value;
};

// Represents an 8-bit floating-point value in E5M2 format (1 sign bit,
// 5 exponent bits, 2 mantissa bits).
struct Fp8E5m2 {
  uint8_t value;
};

// Helper template for floating point type information. This allows the specific
// information for each fp type to be easily extracted.
template <typename T>
struct FPTypeInfo {
  using UIntType = typename std::make_unsigned<T>::type;
  using IntType = typename std::make_signed<T>::type;
};

template <>
struct FPTypeInfo<HalfFP> {
  using T = HalfFP;
  using UIntType = uint16_t;
  using IntType = std::make_signed<UIntType>::type;
  static constexpr int kBitSize = sizeof(HalfFP) << 3;
  static constexpr int kExpSize = 5;
  static constexpr int kExpBias = 15;
  static constexpr int kSigSize = kBitSize - kExpSize - /*sign*/ 1;
  static constexpr UIntType kInfMask = (1ULL << (kBitSize - 1)) - 1;
  static constexpr UIntType kExpMask = ((1ULL << kExpSize) - 1) << kSigSize;
  static constexpr UIntType kSigMask = (1ULL << kSigSize) - 1;
  static constexpr UIntType kCanonicalNaN = 0b0'11111'1ULL << (kSigSize - 1);
  static constexpr UIntType kPosInf = kExpMask;
  static constexpr UIntType kNegInf = kExpMask | (1ULL << (kBitSize - 1));
  static bool IsInf(T value) {
    UIntType uint_val = *reinterpret_cast<UIntType*>(&value);
    return (uint_val & kInfMask) == kPosInf;
  }
  static bool IsNaN(T value) {
    UIntType uint_val = *reinterpret_cast<UIntType*>(&value);
    return ((uint_val & kExpMask) == kExpMask) && ((uint_val & kSigMask) != 0);
  }
  static bool IsSNaN(T value) {
    UIntType uint_val = *reinterpret_cast<UIntType*>(&value);
    return IsNaN(value) && (((1 << (kSigSize - 1)) & uint_val) == 0);
  }
  static bool IsQNaN(T value) {
    UIntType uint_val = *reinterpret_cast<UIntType*>(&value);
    return IsNaN(value) && (((1 << (kSigSize - 1)) & uint_val) != 0);
  }
  static bool SignBit(T value) {
    UIntType uint_val = *reinterpret_cast<UIntType*>(&value);
    return 1 == (uint_val >> (kBitSize - 1));
  }
};

template <>
struct FPTypeInfo<Bfloat16> {
  using T = Bfloat16;
  using UIntType = uint16_t;
  using IntType = std::make_signed_t<UIntType>;
  static constexpr int kBitSize = sizeof(Bfloat16) << 3;
  static constexpr int kExpSize = 8;
  static constexpr int kExpBias = 127;
  static constexpr int kSigSize = kBitSize - kExpSize - /*sign*/ 1;
  static constexpr UIntType kInfMask = (1ULL << (kBitSize - 1)) - 1;
  static constexpr UIntType kExpMask = ((1ULL << kExpSize) - 1) << kSigSize;
  static constexpr UIntType kSigMask = (1ULL << kSigSize) - 1;
  static constexpr UIntType kCanonicalNaN = 0b0'1111'1111'1ULL
                                            << (kSigSize - 1);
  static constexpr UIntType kPosInf = kExpMask;
  static constexpr UIntType kNegInf = kExpMask | (1ULL << (kBitSize - 1));
  static bool IsInf(T value) {
    UIntType uint_val = *reinterpret_cast<UIntType*>(&value);
    return (uint_val & kInfMask) == kPosInf;
  }
  static bool IsNaN(T value) {
    UIntType uint_val = *reinterpret_cast<UIntType*>(&value);
    return ((uint_val & kExpMask) == kExpMask) && ((uint_val & kSigMask) != 0);
  }
  static bool IsSNaN(T value) {
    UIntType uint_val = *reinterpret_cast<UIntType*>(&value);
    return IsNaN(value) && (((1 << (kSigSize - 1)) & uint_val) == 0);
  }
  static bool IsQNaN(T value) {
    UIntType uint_val = *reinterpret_cast<UIntType*>(&value);
    return IsNaN(value) && (((1 << (kSigSize - 1)) & uint_val) != 0);
  }
  static bool SignBit(T value) {
    UIntType uint_val = *reinterpret_cast<UIntType*>(&value);
    return 1 == (uint_val >> (kBitSize - 1));
  }
};

template <>
struct FPTypeInfo<Fp8E4m3> {
  using T = Fp8E4m3;
  using UIntType = uint8_t;
  using IntType = std::make_signed_t<UIntType>;
  static constexpr int kBitSize = sizeof(Fp8E4m3) << 3;
  static constexpr int kExpSize = 4;
  static constexpr int kExpBias = 7;
  static constexpr int kSigSize = kBitSize - kExpSize - /*sign*/ 1;
  static constexpr UIntType kExpMask = ((1ULL << kExpSize) - 1) << kSigSize;
  static constexpr UIntType kSigMask = (1ULL << kSigSize) - 1;
  static constexpr UIntType kCanonicalNaN = 0x7F;
  static bool IsInf(T /*value*/) { return false; }
  static bool IsNaN(T value) {
    UIntType uint_val = *reinterpret_cast<UIntType*>(&value);
    return (uint_val & 0x7F) == 0x7F;
  }
  static bool IsSNaN(T /*value*/) { return false; }
  static bool IsQNaN(T value) { return IsNaN(value); }
  static bool SignBit(T value) {
    UIntType uint_val = *reinterpret_cast<UIntType*>(&value);
    return 1 == (uint_val >> (kBitSize - 1));
  }
};

template <>
struct FPTypeInfo<Fp8E5m2> {
  using T = Fp8E5m2;
  using UIntType = uint8_t;
  using IntType = std::make_signed_t<UIntType>;
  static constexpr int kBitSize = sizeof(Fp8E5m2) << 3;
  static constexpr int kExpSize = 5;
  static constexpr int kExpBias = 15;
  static constexpr int kSigSize = kBitSize - kExpSize - /*sign*/ 1;
  static constexpr UIntType kInfMask = (1ULL << (kBitSize - 1)) - 1;
  static constexpr UIntType kExpMask = ((1ULL << kExpSize) - 1) << kSigSize;
  static constexpr UIntType kSigMask = (1ULL << kSigSize) - 1;
  static constexpr UIntType kCanonicalNaN = 0b0'11111'1ULL << (kSigSize - 1);
  static constexpr UIntType kPosInf = kExpMask;
  static constexpr UIntType kNegInf = kExpMask | (1ULL << (kBitSize - 1));
  static bool IsInf(T value) {
    UIntType uint_val = *reinterpret_cast<UIntType*>(&value);
    return (uint_val & kInfMask) == kPosInf;
  }
  static bool IsNaN(T value) {
    UIntType uint_val = *reinterpret_cast<UIntType*>(&value);
    return ((uint_val & kExpMask) == kExpMask) && ((uint_val & kSigMask) != 0);
  }
  static bool IsSNaN(T value) {
    UIntType uint_val = *reinterpret_cast<UIntType*>(&value);
    return IsNaN(value) && (((1 << (kSigSize - 1)) & uint_val) == 0);
  }
  static bool IsQNaN(T value) {
    UIntType uint_val = *reinterpret_cast<UIntType*>(&value);
    return IsNaN(value) && (((1 << (kSigSize - 1)) & uint_val) != 0);
  }
  static bool SignBit(T value) {
    UIntType uint_val = *reinterpret_cast<UIntType*>(&value);
    return 1 == (uint_val >> (kBitSize - 1));
  }
};

template <>
struct FPTypeInfo<float> {
  using T = float;
  using UIntType = uint32_t;
  using IntType = std::make_signed<UIntType>::type;
  static constexpr int kBitSize = sizeof(float) << 3;
  static constexpr int kExpSize = 8;
  static constexpr int kExpBias = 127;
  static constexpr int kSigSize = kBitSize - kExpSize - /*sign*/ 1;
  static constexpr UIntType kInfMask = (1ULL << (kBitSize - 1)) - 1;
  static constexpr UIntType kExpMask = ((1ULL << kExpSize) - 1) << kSigSize;
  static constexpr UIntType kSigMask = (1ULL << kSigSize) - 1;
  static constexpr UIntType kCanonicalNaN = 0b0'1111'1111'1ULL
                                            << (kSigSize - 1);
  static constexpr UIntType kPosInf = kExpMask;
  static constexpr UIntType kNegInf = kExpMask | (1ULL << (kBitSize - 1));
  static bool IsInf(T value) {
    UIntType uint_val = *reinterpret_cast<UIntType*>(&value);
    return (uint_val & kInfMask) == kPosInf;
  }
  static bool IsNaN(T value) {
    UIntType uint_val = *reinterpret_cast<UIntType*>(&value);
    return ((uint_val & kExpMask) == kExpMask) && ((uint_val & kSigMask) != 0);
  }
  static bool IsSNaN(T value) {
    UIntType uint_val = *reinterpret_cast<UIntType*>(&value);
    return IsNaN(value) && (((1 << (kSigSize - 1)) & uint_val) == 0);
  }
  static bool IsQNaN(T value) {
    UIntType uint_val = *reinterpret_cast<UIntType*>(&value);
    return IsNaN(value) && (((1 << (kSigSize - 1)) & uint_val) != 0);
  }
  static bool SignBit(T value) {
    UIntType uint_val = *reinterpret_cast<UIntType*>(&value);
    return 1 == (uint_val >> (kBitSize - 1));
  }
};

template <>
struct FPTypeInfo<double> {
  using T = double;
  using UIntType = uint64_t;
  using IntType = std::make_signed_t<UIntType>;
  static constexpr int kBitSize = sizeof(double) << 3;
  static constexpr int kExpSize = 11;
  static constexpr int kExpBias = 1023;
  static constexpr int kSigSize = kBitSize - kExpSize - /*sign*/ 1;
  static constexpr UIntType kInfMask = (1ULL << (kBitSize - 1)) - 1;
  static constexpr UIntType kExpMask = ((1ULL << kExpSize) - 1) << kSigSize;
  static constexpr UIntType kSigMask = (1ULL << kSigSize) - 1;
  static constexpr UIntType kCanonicalNaN = 0b0'1111'1111'111'1ULL
                                            << (kSigSize - 1);
  static constexpr UIntType kPosInf = kExpMask;
  static constexpr UIntType kNegInf = kExpMask | (1ULL << (kBitSize - 1));
  static bool IsInf(T value) {
    UIntType uint_val = *reinterpret_cast<UIntType*>(&value);
    return (uint_val & kInfMask) == kPosInf;
  }
  static bool IsNaN(T value) {
    UIntType uint_val = *reinterpret_cast<UIntType*>(&value);
    return ((uint_val & kExpMask) == kExpMask) && ((uint_val & kSigMask) != 0);
  }
  static bool IsSNaN(T value) {
    UIntType uint_val = *reinterpret_cast<UIntType*>(&value);
    return IsNaN(value) && (((1ULL << (kSigSize - 1)) & uint_val) == 0);
  }
  static bool IsQNaN(T value) {
    UIntType uint_val = *reinterpret_cast<UIntType*>(&value);
    return IsNaN(value) && (((1ULL << (kSigSize - 1)) & uint_val) != 0);
  }
  static bool SignBit(T value) {
    UIntType uint_val = *reinterpret_cast<UIntType*>(&value);
    return 1 == (uint_val >> (kBitSize - 1));
  }
};

template <>
struct FPTypeInfo<uint16_t> {
  using T = uint16_t;
  using UIntType = uint16_t;
  using IntType = std::make_signed_t<UIntType>;
  static constexpr int kBitSize = sizeof(HalfFP) << 3;
  static constexpr int kExpSize = 5;
  static constexpr int kExpBias = 15;
  static constexpr int kSigSize = kBitSize - kExpSize - /*sign*/ 1;
  static constexpr UIntType kInfMask = (1ULL << (kBitSize - 1)) - 1;
  static constexpr UIntType kExpMask = ((1ULL << kExpSize) - 1) << kSigSize;
  static constexpr UIntType kSigMask = (1ULL << kSigSize) - 1;
  static constexpr UIntType kCanonicalNaN = 0b0'11111'1ULL << (kSigSize - 1);
  static constexpr UIntType kPosInf = kExpMask;
  static constexpr UIntType kNegInf = kExpMask | (1ULL << (kBitSize - 1));
  static bool IsInf(T value) { return (value & kInfMask) == kPosInf; }
  static bool IsNaN(T value) {
    return ((value & kExpMask) == kExpMask) && ((value & kSigMask) != 0);
  }
  static bool IsSNaN(T value) {
    return IsNaN(value) && (((1 << (kSigSize - 1)) & value) == 0);
  }
  static bool IsQNaN(T value) {
    return IsNaN(value) && (((1 << (kSigSize - 1)) & value) != 0);
  }
  static bool SignBit(T value) { return 1 == (value >> (kBitSize - 1)); }
};

template <>
struct FPTypeInfo<int16_t> {
  using T = int16_t;
  using UIntType = uint16_t;
  using IntType = std::make_signed_t<UIntType>;
  static constexpr int kBitSize = sizeof(HalfFP) << 3;
  static constexpr int kExpSize = 5;
  static constexpr int kExpBias = 15;
  static constexpr int kSigSize = kBitSize - kExpSize - /*sign*/ 1;
  static constexpr UIntType kInfMask = (1ULL << (kBitSize - 1)) - 1;
  static constexpr UIntType kExpMask = ((1ULL << kExpSize) - 1) << kSigSize;
  static constexpr UIntType kSigMask = (1ULL << kSigSize) - 1;
  static constexpr UIntType kCanonicalNaN = 0b0'11111'1ULL << (kSigSize - 1);
  static constexpr UIntType kPosInf = kExpMask;
  static constexpr UIntType kNegInf = kExpMask | (1ULL << (kBitSize - 1));
  static bool IsInf(T value) { return (value & kInfMask) == kPosInf; }
  static bool IsNaN(T value) {
    return ((value & kExpMask) == kExpMask) && ((value & kSigMask) != 0);
  }
  static bool IsSNaN(T value) {
    return IsNaN(value) && (((1 << (kSigSize - 1)) & value) == 0);
  }
  static bool IsQNaN(T value) {
    return IsNaN(value) && (((1 << (kSigSize - 1)) & value) != 0);
  }
  static bool SignBit(T value) { return 1 == (value >> (kBitSize - 1)); }
};

template <>
struct FPTypeInfo<uint32_t> {
  using T = uint32_t;
  using UIntType = uint32_t;
  using IntType = std::make_signed_t<UIntType>;
  static constexpr int kBitSize = sizeof(float) << 3;
  static constexpr int kExpSize = 8;
  static constexpr int kExpBias = 127;
  static constexpr int kSigSize = kBitSize - kExpSize - /*sign*/ 1;
  static constexpr UIntType kInfMask = (1ULL << (kBitSize - 1)) - 1;
  static constexpr UIntType kExpMask = ((1ULL << kExpSize) - 1) << kSigSize;
  static constexpr UIntType kSigMask = (1ULL << kSigSize) - 1;
  static constexpr UIntType kCanonicalNaN = 0b0'1111'1111'1ULL
                                            << (kSigSize - 1);
  static constexpr UIntType kPosInf = kExpMask;
  static constexpr UIntType kNegInf = kExpMask | (1ULL << (kBitSize - 1));
  static bool IsInf(T value) { return (value & kInfMask) == kPosInf; }
  static bool IsNaN(T value) {
    return ((value & kExpMask) == kExpMask) && ((value & kSigMask) != 0);
  }
  static bool IsSNaN(T value) {
    return IsNaN(value) && (((1ULL << (kSigSize - 1)) & value) == 0);
  }
  static bool IsQNaN(T value) {
    return IsNaN(value) && (((1ULL << (kSigSize - 1)) & value) != 0);
  }
  static bool SignBit(T value) { return 1 == (value >> (kBitSize - 1)); }
};

template <>
struct FPTypeInfo<int32_t> {
  using T = int32_t;
  using UIntType = uint32_t;
  using IntType = std::make_signed_t<UIntType>;
  static constexpr int kBitSize = sizeof(float) << 3;
  static constexpr int kExpSize = 8;
  static constexpr int kExpBias = 127;
  static constexpr int kSigSize = kBitSize - kExpSize - /*sign*/ 1;
  static constexpr UIntType kInfMask = (1ULL << (kBitSize - 1)) - 1;
  static constexpr UIntType kExpMask = ((1ULL << kExpSize) - 1) << kSigSize;
  static constexpr UIntType kSigMask = (1ULL << kSigSize) - 1;
  static constexpr UIntType kCanonicalNaN = 0b0'1111'1111'1ULL
                                            << (kSigSize - 1);
  static constexpr UIntType kPosInf = kExpMask;
  static constexpr UIntType kNegInf = kExpMask | (1ULL << (kBitSize - 1));
  static bool IsInf(T value) { return (value & kInfMask) == kPosInf; }
  static bool IsNaN(T value) {
    return ((value & kExpMask) == kExpMask) && ((value & kSigMask) != 0);
  }
  static bool IsSNaN(T value) {
    return IsNaN(value) && (((1ULL << (kSigSize - 1)) & value) == 0);
  }
  static bool IsQNaN(T value) {
    return IsNaN(value) && (((1ULL << (kSigSize - 1)) & value) != 0);
  }
  static bool SignBit(T value) { return 1 == (value >> (kBitSize - 1)); }
};

template <>
struct FPTypeInfo<uint64_t> {
  using T = uint64_t;
  using UIntType = uint64_t;
  using IntType = std::make_signed_t<UIntType>;
  static constexpr int kBitSize = sizeof(float) << 3;
  static constexpr int kExpSize = 8;
  static constexpr int kExpBias = 127;
  static constexpr int kSigSize = kBitSize - kExpSize - /*sign*/ 1;
  static constexpr UIntType kInfMask = (1ULL << (kBitSize - 1)) - 1;
  static constexpr UIntType kExpMask = ((1ULL << kExpSize) - 1) << kSigSize;
  static constexpr UIntType kSigMask = (1ULL << kSigSize) - 1;
  static constexpr UIntType kCanonicalNaN = 0b0'1111'1111'1ULL
                                            << (kSigSize - 1);
  static constexpr UIntType kPosInf = kExpMask;
  static constexpr UIntType kNegInf = kExpMask | (1ULL << (kBitSize - 1));
  static bool IsInf(T value) { return (value & kInfMask) == kPosInf; }
  static bool IsNaN(T value) {
    return ((value & kExpMask) == kExpMask) && ((value & kSigMask) != 0);
  }
  static bool IsSNaN(T value) {
    return IsNaN(value) && (((1ULL << (kSigSize - 1)) & value) == 0);
  }
  static bool IsQNaN(T value) {
    return IsNaN(value) && (((1ULL << (kSigSize - 1)) & value) != 0);
  }
  static bool SignBit(T value) { return 1 == (value >> (kBitSize - 1)); }
};

template <>
struct FPTypeInfo<int64_t> {
  using T = int64_t;
  using UIntType = uint64_t;
  using IntType = std::make_signed_t<UIntType>;
  static constexpr int kBitSize = sizeof(float) << 3;
  static constexpr int kExpSize = 8;
  static constexpr int kExpBias = 127;
  static constexpr int kSigSize = kBitSize - kExpSize - /*sign*/ 1;
  static constexpr UIntType kInfMask = (1ULL << (kBitSize - 1)) - 1;
  static constexpr UIntType kExpMask = ((1ULL << kExpSize) - 1) << kSigSize;
  static constexpr UIntType kSigMask = (1ULL << kSigSize) - 1;
  static constexpr UIntType kCanonicalNaN = 0b0'1111'1111'1ULL
                                            << (kSigSize - 1);
  static constexpr UIntType kPosInf = kExpMask;
  static constexpr UIntType kNegInf = kExpMask | (1ULL << (kBitSize - 1));
  static bool IsInf(T value) { return (value & kInfMask) == kPosInf; }
  static bool IsNaN(T value) {
    return ((value & kExpMask) == kExpMask) && ((value & kSigMask) != 0);
  }
  static bool IsSNaN(T value) {
    return IsNaN(value) && (((1ULL << (kSigSize - 1)) & value) == 0);
  }
  static bool IsQNaN(T value) {
    return IsNaN(value) && (((1ULL << (kSigSize - 1)) & value) != 0);
  }
  static bool SignBit(T value) { return 1 == (value >> (kBitSize - 1)); }
};

inline float ConvertHalfToSingle(HalfFP half) {
  uint32_t float_uint;
  bool sign = half.value >> (FPTypeInfo<HalfFP>::kBitSize - 1);

  if (half.value == FPTypeInfo<HalfFP>::kPosInf) {
    float_uint = FPTypeInfo<float>::kPosInf;
    return *reinterpret_cast<float*>(&float_uint);
  }

  if (half.value == FPTypeInfo<HalfFP>::kNegInf) {
    float_uint = FPTypeInfo<float>::kNegInf;
    return *reinterpret_cast<float*>(&float_uint);
  }

  if (FPTypeInfo<HalfFP>::IsNaN(half)) {
    float_uint = FPTypeInfo<float>::kCanonicalNaN;
    float_uint |= sign << (FPTypeInfo<float>::kBitSize - 1);
    return *reinterpret_cast<float*>(&float_uint);
  }

  if (half.value == 0) {
    float_uint = 0;
    return *reinterpret_cast<float*>(&float_uint);
  }

  if (half.value == 1 << (FPTypeInfo<HalfFP>::kBitSize - 1)) {
    float_uint = 1 << (FPTypeInfo<float>::kBitSize - 1);
    return *reinterpret_cast<float*>(&float_uint);
  }

  uint32_t exp = (half.value & FPTypeInfo<HalfFP>::kExpMask) >>
                 (FPTypeInfo<HalfFP>::kSigSize);
  uint32_t sig = half.value & FPTypeInfo<HalfFP>::kSigMask;
  if (exp == 0 && sig != 0) {
    // Subnormal value.
    int32_t shift_count = 0;
    while ((sig & (1 << FPTypeInfo<HalfFP>::kSigSize)) == 0) {
      sig <<= 1;
      shift_count++;
    }
    sig &= FPTypeInfo<HalfFP>::kSigMask;
    exp = 1 - shift_count;
  }
  exp += FPTypeInfo<float>::kExpBias - FPTypeInfo<HalfFP>::kExpBias;
  sig <<= FPTypeInfo<float>::kSigSize - FPTypeInfo<HalfFP>::kSigSize;
  float_uint = (exp << FPTypeInfo<float>::kSigSize) | sig;
  float_uint |= sign << (FPTypeInfo<float>::kBitSize - 1);
  return *reinterpret_cast<float*>(&float_uint);
}

// Converts a 16-bit brain floating-point value (Bfloat16) to an IEEE
// single-precision float.
inline float ConvertBfloat16ToSingle(Bfloat16 bf16) {
  uint32_t float_uint = static_cast<uint32_t>(bf16.value) << 16;
  return absl::bit_cast<float>(float_uint);
}

// Alias for ConvertBfloat16ToSingle.
inline float ConvertBf16ToSingle(Bfloat16 bf16) {
  return ConvertBfloat16ToSingle(bf16);
}

// Converts an 8-bit E4M3 floating-point value (Fp8E4m3) to an IEEE
// single-precision float.
inline float ConvertFp8E4m3ToSingle(Fp8E4m3 fp8) {
  uint8_t x = fp8.value;
  uint32_t sign = (x >> 7) & 1;
  uint32_t exp = (x >> 3) & 0xF;
  uint32_t frac = x & 0x7;

  constexpr uint32_t kBiasDiffSubnormal = 127 - 6;
  constexpr uint32_t kBiasDiffNormal = 120;  // 127 - 7

  uint32_t res;
  if (exp == 0) {
    if (frac == 0) {
      res = sign << 31;
    } else {
      exp = kBiasDiffSubnormal;
      while ((frac & (0x7 + 1)) == 0) {
        frac <<= 1;
        exp--;
      }
      frac &= 0x7;
      res = (sign << 31) | (exp << 23) | (frac << 20);
    }
  } else if (exp == 0xF && frac == 0x7) {
    res = (sign << 31) | (0xFF << 23) | (0x7F << 16);
  } else {
    res = (sign << 31) | ((exp + kBiasDiffNormal) << 23) | (frac << 20);
  }
  return absl::bit_cast<float>(res);
}

// Converts an 8-bit E5M2 floating-point value (Fp8E5m2) to an IEEE
// single-precision float.
inline float ConvertFp8E5m2ToSingle(Fp8E5m2 fp8) {
  uint8_t x = fp8.value;
  uint32_t sign = (x >> 7) & 1;
  uint32_t exp = (x >> 2) & 0x1F;
  uint32_t frac = x & 0x3;

  constexpr uint32_t kBiasDiffSubnormal = 127 - 14;
  constexpr uint32_t kBiasDiffNormal = 112;  // 127 - 15

  uint32_t res;
  if (exp == 0) {
    if (frac == 0) {
      res = sign << 31;
    } else {
      exp = kBiasDiffSubnormal;
      while ((frac & (0x3 + 1)) == 0) {
        frac <<= 1;
        exp--;
      }
      frac &= 0x3;
      res = (sign << 31) | (exp << 23) | (frac << 21);
    }
  } else if (exp == 0x1F) {
    res = (sign << 31) | (0xFF << 23) | (frac == 0 ? 0 : 0x400000);
  } else {
    res = (sign << 31) | ((exp + kBiasDiffNormal) << 23) | (frac << 21);
  }
  return absl::bit_cast<float>(res);
}

// A replacement for std::is_floating_point that works for half precision.
template <typename T>
struct IsMpactFp {
  static constexpr bool value = std::is_floating_point<T>::value;
};

template <>
struct IsMpactFp<HalfFP> {
  static constexpr bool value = true;
};

template <>
struct IsMpactFp<Bfloat16> {
  static constexpr bool value = true;
};

template <>
struct IsMpactFp<Fp8E4m3> {
  static constexpr bool value = true;
};

template <>
struct IsMpactFp<Fp8E5m2> {
  static constexpr bool value = true;
};

// A helper to print the contents of a floating point that also works for half
// precision.
template <typename T>
inline std::string FloatingPointToString(T floating_point) {
  return std::to_string(floating_point);
}

template <>
inline std::string FloatingPointToString(HalfFP floating_point) {
  // Convert to float and then convert to string.
  return FloatingPointToString<float>(ConvertHalfToSingle(floating_point));
}

template <>
inline std::string FloatingPointToString(Bfloat16 floating_point) {
  return FloatingPointToString<float>(ConvertBfloat16ToSingle(floating_point));
}

template <>
inline std::string FloatingPointToString(Fp8E4m3 floating_point) {
  return FloatingPointToString<float>(ConvertFp8E4m3ToSingle(floating_point));
}

template <>
inline std::string FloatingPointToString(Fp8E5m2 floating_point) {
  return FloatingPointToString<float>(ConvertFp8E5m2ToSingle(floating_point));
}

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

}  // namespace generic
}  // namespace sim
}  // namespace mpact

#endif  // MPACT_SIM_GENERIC_TYPE_HELPERS_H_
