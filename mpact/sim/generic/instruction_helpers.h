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

#ifndef MPACT_SIM_GENERIC_INSTRUCTION_HELPERS_H_
#define MPACT_SIM_GENERIC_INSTRUCTION_HELPERS_H_

#include <functional>

#include "mpact/sim/generic/instruction.h"

// This file contains a set of inline templated functions that can be used to
// avoid a lot of the boiler plate code in writing simple semantic functions.
// Using optimization level of O3 should allow these functions to be perfectly
// inlined in the semantic functions that reference them.

namespace mpact {
namespace sim {
namespace generic {

// This is a templated helper function used to factor out common code in
// two operand instruction semantic functions. It reads two source operands
// and applies the function argument to them, storing the result to the
// destination operand. This version supports different types for the result and
// each of the two source operands.
template <typename Result, typename Argument1, typename Argument2>
inline void BinaryOp(Instruction *instruction,
                     std::function<Result(Argument1, Argument2)> operation) {
  Argument1 lhs = generic::GetInstructionSource<Argument1>(instruction, 0);
  Argument2 rhs = generic::GetInstructionSource<Argument2>(instruction, 1);
  Result dest_value = operation(lhs, rhs);
  auto *db = instruction->Destination(0)->AllocateDataBuffer();
  db->SetSubmit<Result>(0, dest_value);
}

// This is a templated helper function used to factor out common code in
// two operand instruction semantic functions. It reads two source operands
// and applies the function argument to them, storing the result to the
// destination operand. This version supports different types for the result
// and the operands, but the two source operands must have the same type.
template <typename Result, typename Argument>
inline void BinaryOp(Instruction *instruction,
                     std::function<Result(Argument, Argument)> operation) {
  Argument lhs = generic::GetInstructionSource<Argument>(instruction, 0);
  Argument rhs = generic::GetInstructionSource<Argument>(instruction, 1);
  Result dest_value = operation(lhs, rhs);
  auto *db = instruction->Destination(0)->AllocateDataBuffer();
  db->SetSubmit<Result>(0, dest_value);
}

// This is a templated helper function used to factor out common code in
// two operand instruction semantic functions. It reads two source operands
// and applies the function argument to them, storing the result to the
// destination operand. This version requires both result and source operands
// to have the same type.
template <typename Result>
inline void BinaryOp(Instruction *instruction,
                     std::function<Result(Result, Result)> operation) {
  Result lhs = generic::GetInstructionSource<Result>(instruction, 0);
  Result rhs = generic::GetInstructionSource<Result>(instruction, 1);
  Result dest_value = operation(lhs, rhs);
  auto *db = instruction->Destination(0)->AllocateDataBuffer();
  db->SetSubmit<Result>(0, dest_value);
}

// This is a templated helper function used to factor out common code in
// single operand instruction semantic functions. It reads one source operand
// and applies the function argument to it, storing the result to the
// destination operand. This version supports the result and argument having
// different types.
template <typename Result, typename Argument>
inline void UnaryOp(Instruction *instruction,
                    std::function<Result(Argument)> operation) {
  Argument lhs = generic::GetInstructionSource<Argument>(instruction, 0);
  Result dest_value = operation(lhs);
  auto *db = instruction->Destination(0)->AllocateDataBuffer();
  db->SetSubmit<Result>(0, dest_value);
}

// This is a templated helper function used to factor out common code in
// single operand instruction semantic functions. It reads one source operand
// and applies the function argument to it, storing the result to the
// destination operand. This version requires that the result and argument have
// the same type.
template <typename Result>
inline void UnaryOp(Instruction *instruction,
                    std::function<Result(Result)> operation) {
  Result lhs = generic::GetInstructionSource<Result>(instruction, 0);
  Result dest_value = operation(lhs);
  auto *db = instruction->Destination(0)->AllocateDataBuffer();
  db->SetSubmit<Result>(0, dest_value);
}

// This is a templated helper function used to factor out common code in
// three operand vector instruction semantic functions. This version
// allows for different types for the result and each argument.
template <typename Result, typename Argument1, typename Argument2,
          typename Argument3>
inline void TernaryVectorOp(
    Instruction *instruction,
    std::function<Result(Argument1, Argument2, Argument3)> operation) {
  auto *dst = instruction->Destination(0);
  auto *db = dst->AllocateDataBuffer();
  int size = dst->shape()[0];
  for (int i = 0; i < size; i++) {
    Argument1 x_val =
        generic::GetInstructionSource<Argument1>(instruction, 0, i);
    Argument2 y_val =
        generic::GetInstructionSource<Argument2>(instruction, 1, i);
    Argument3 z_val =
        generic::GetInstructionSource<Argument3>(instruction, 2, i);
    Result value = operation(x_val, y_val, z_val);
    db->Set<Result>(i, value);
  }
  db->Submit();
}

// This is a templated helper function used to factor out common code in
// three operand vector instruction semantic functions. This version
// allows for different types for the result and the three arguments, though
// the arguments have to all have the same type.
template <typename Result, typename Argument>
inline void TernaryVectorOp(
    Instruction *instruction,
    std::function<Result(Argument, Argument, Argument)> operation) {
  auto *dst = instruction->Destination(0);
  auto *db = dst->AllocateDataBuffer();
  int size = dst->shape()[0];
  for (int i = 0; i < size; i++) {
    Argument x_val = generic::GetInstructionSource<Argument>(instruction, 0, i);
    Argument y_val = generic::GetInstructionSource<Argument>(instruction, 1, i);
    Argument z_val = generic::GetInstructionSource<Argument>(instruction, 2, i);
    Result value = operation(x_val, y_val, z_val);
    db->Set<Result>(i, value);
  }
  db->Submit();
}

// This is a templated helper function used to factor out common code in
// three operand vector instruction semantic functions. This version
// requires the result and arguments to have the same type.
template <typename Result>
inline void TernaryVectorOp(
    Instruction *instruction,
    std::function<Result(Result, Result, Result)> operation) {
  auto *dst = instruction->Destination(0);
  auto *db = dst->AllocateDataBuffer();
  int size = dst->shape()[0];
  for (int i = 0; i < size; i++) {
    Result x_val = generic::GetInstructionSource<Result>(instruction, 0, i);
    Result y_val = generic::GetInstructionSource<Result>(instruction, 1, i);
    Result z_val = generic::GetInstructionSource<Result>(instruction, 2, i);
    Result value = operation(x_val, y_val, z_val);
    db->Set<Result>(i, value);
  }
  db->Submit();
}

// This is a templated helper function used to factor out common code in
// two operand vector instruction semantic functions. This version
// allows for different types for the result and each argument.
template <typename Result, typename Argument1, typename Argument2>
inline void BinaryVectorOp(
    Instruction *instruction,
    std::function<Result(Argument1, Argument2)> operation) {
  auto *dst = instruction->Destination(0);
  auto *db = dst->AllocateDataBuffer();
  int size = dst->shape()[0];
  for (int i = 0; i < size; i++) {
    Argument1 lhs = generic::GetInstructionSource<Argument1>(instruction, 0, i);
    Argument2 rhs = generic::GetInstructionSource<Argument2>(instruction, 1, i);
    Result value = operation(lhs, rhs);
    db->Set<Result>(i, value);
  }
  db->Submit();
}

// This is a templated helper function used to factor out common code in
// two operand vector instruction semantic functions. This version
// allows for different types for the result and the three arguments, though
// the arguments have to have the same type.
template <typename Result, typename Argument>
inline void BinaryVectorOp(
    Instruction *instruction,
    std::function<Result(Argument, Argument)> operation) {
  auto *dst = instruction->Destination(0);
  auto *db = dst->AllocateDataBuffer();
  int size = dst->shape()[0];
  for (int i = 0; i < size; i++) {
    Argument lhs = generic::GetInstructionSource<Argument>(instruction, 0, i);
    Argument rhs = generic::GetInstructionSource<Argument>(instruction, 1, i);
    Result value = operation(lhs, rhs);
    db->Set<Result>(i, value);
  }
  db->Submit();
}

// This is a templated helper function used to factor out common code in
// two operand vector instruction semantic functions. This version
// requires the result and arguments to have the same type.
template <typename Result>
inline void BinaryVectorOp(Instruction *instruction,
                           std::function<Result(Result, Result)> operation) {
  auto *dst = instruction->Destination(0);
  auto *db = dst->AllocateDataBuffer();
  int size = dst->shape()[0];
  for (int i = 0; i < size; i++) {
    Result lhs = generic::GetInstructionSource<Result>(instruction, 0, i);
    Result rhs = generic::GetInstructionSource<Result>(instruction, 1, i);
    Result value = operation(lhs, rhs);
    db->Set<Result>(i, value);
  }
  db->Submit();
}

// This is a templated helper function used to factor out common code in
// single operand vector instruction semantic functions. This version
// allows the result and argument to have different types.
template <typename Result, typename Argument>
inline void UnaryVectorOp(Instruction *instruction,
                          std::function<Result(Argument)> operation) {
  auto *dst = instruction->Destination(0);
  auto *db = dst->AllocateDataBuffer();
  int size = dst->shape()[0];
  for (int i = 0; i < size; i++) {
    Argument lhs = generic::GetInstructionSource<Argument>(instruction, 0, i);
    Result value = operation(lhs);
    db->Set<Result>(i, value);
  }
  db->Submit();
}

// This is a templated helper function used to factor out common code in
// single operand vector instruction semantic functions. This version
// requires the result and argument to have the same type.
template <typename Result>
inline void UnaryVectorOp(Instruction *instruction,
                          std::function<Result(Result)> operation) {
  auto *dst = instruction->Destination(0);
  auto *db = dst->AllocateDataBuffer();
  int size = dst->shape()[0];
  for (int i = 0; i < size; i++) {
    Result lhs = generic::GetInstructionSource<Result>(instruction, 0, i);
    Result value = operation(lhs);
    db->Set<Result>(i, value);
  }
  db->Submit();
}

}  // namespace generic
}  // namespace sim
}  // namespace mpact

#endif  // MPACT_SIM_GENERIC_INSTRUCTION_HELPERS_H_
