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

#ifndef MPACT_SIM_GENERIC_INSTRUCTION_H_
#define MPACT_SIM_GENERIC_INSTRUCTION_H_

#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "absl/numeric/int128.h"
#include "absl/types/span.h"
#include "mpact/sim/generic/arch_state.h"
#include "mpact/sim/generic/operand_interface.h"
#include "mpact/sim/generic/ref_count.h"

namespace mpact {
namespace sim {
namespace generic {

class ResourceOperandInterface;

// This is the class used to as the internal simulator representation of a
// target architecture instruction or a component operation of such an
// instruction. An example would be the individual operations of a VLIW
// instruction, or those instructions with semantics that may need to be modeled
// over multiple distinct architectural cycles (some memory loads for instance,
// where the returned data has to be transformed before being written back to
// the register). It is also used to represent the function responsible for
// issuing instructions, manages updates of simulated state due to instruction
// side-effects, and advances the program counter. Modeling the instruction
// issue as an instruction (with the instruction issue performed as its semantic
// function) makes it easier for the simulator core to remain
// architecture-agnostic. In this case, the semantic function responsible for
// instruction issue would call Execute on the child instruction, allocating and
// passing in any context structure as may be necessary for that architecture.
// Since the context may contain values that need to be accessed through a
// handle to the Instruction instance during the execution of the semantic
// function, it is copied to the instruction instance before the semantic
// function is called.
//
// The Instruction instance has pointers to Next, Child and Parent Instruction
// instances to manage the decomposition of complex instruction and instruction
// issue relationships. This is used to enable modeling of instruction
// hierarchies such as instructions in a VLIW ISA. In this case, a top level
// instruction (or instruction bundle) can use the Child pointer to point to the
// list of the individual instructions (or operations) that are to be issued as
// one "instruction bundle". The semantic function of the top level instruction
// is then responsible for "issuing" (call Execute()) for each of those
// instructions and handle any state updates/interactions required.
//
// The following diagram illustrates a possible VLIW instruction structure.
// Down arrows are the "next" pointers, whereas the right arrows are the Child
// pointers. The master_instruction implements instruction issue and
// architectural state maintenance. There is one master_instruction instance
// allocated for each PC that is decoded/executed.
//
// master_instruction --->  vliw_bundle 0 ---> inst 0
//                                               |
//                                               V
//                                             inst 1
//                                               |
//                                               V
//                                             inst 2
//
//
// master_instruction --->  vliw_bundle 1 ---> inst 0
//                                               |
//                                               V
//                                             inst 1
//                                              ...
//                                             inst N
class Instruction : public ReferenceCount {
 public:
  // Type Alias for the semantic function.
  using SemanticFunction = std::function<void(Instruction *)>;

  // Constructors and Destructors.
  explicit Instruction(ArchState *state);
  Instruction(uint64_t address, ArchState *state);
  ~Instruction() override;

  // Appends the instruction to the "next" list of instructions.
  void Append(Instruction *inst);
  // Appends the instruction the "child" list of instructions.
  void AppendChild(Instruction *inst);

  // Methods used for navigating instruction hierarchy.
  Instruction *child() const { return child_; }
  Instruction *parent() const { return parent_; }
  Instruction *next() const { return next_; }

  // Execute the instruction with the given context
  // Note: The context is stored into the instruction instance instead of
  // being passed as a parameter to the semantic function. This is intentional
  // to facilitate accessing the data in the context from the Instruction
  // instance itself. For instance, some values used as source operands for
  // an instruction may be stored in the context. The Operand instance has
  // only a handle to the Instruction instance as that is available during
  // instruction decode, and accessing the context otherwise would require
  // modifying the interface for all operands.
  void Execute(ReferenceCount *context) {
    context_ = context;
    semantic_fcn_(this);
    context_ = nullptr;
  }
  // Execute the instruction without context (context_ remains nullptr).
  void Execute() { semantic_fcn_(this); }

  // Accessors (getters/setters).
  ReferenceCount *context() const { return context_; }
  ArchState *state() const { return state_; }
  // Returns the pc value for the instruction.
  uint64_t address() const { return address_; }
  // The address should seldom be set outside the constructor.
  void set_address(uint64_t address) { address_ = address; }
  // The opcode as set by the decoder.
  int opcode() const { return opcode_; }
  void set_opcode(int opcode) { opcode_ = opcode; }
  // Returns the size in terms of pc increment value.
  int size() const { return size_; }
  // Sets the instruction size (pc increment).
  void set_size(int sz) { size_ = sz; }
  // Sets the semantic function callable - typically only used by the decoder.
  // The callable must be convertible to std::function<void(Instruction *)>.
  template <typename F>
  void set_semantic_function(F callable) {
    semantic_fcn_ = SemanticFunction(callable);
  }

  // PredicateOperand interface used for those ISAs that implement
  // instruction predicates.
  PredicateOperandInterface *Predicate() const { return predicate_; }
  void SetPredicate(PredicateOperandInterface *predicate);

  // SourceOperand interfaces for the instruction.
  SourceOperandInterface *Source(int i) const { return sources_[i]; }
  void AppendSource(SourceOperandInterface *op);
  int SourcesSize() const;

  // DestinationOperand interfaces for the instruction.
  DestinationOperandInterface *Destination(int i) const { return dests_[i]; }
  void AppendDestination(DestinationOperandInterface *op);
  int DestinationsSize() const;

  // Hold ResourceOperand interfaces for the instruction.
  inline std::vector<ResourceOperandInterface *> &ResourceHold() {
    return resource_hold_;
  }
  inline void AppendResourceHold(ResourceOperandInterface *op) {
    resource_hold_.push_back(op);
  }

  // Acquire ResourceOperand interfaces for the instruction.
  inline std::vector<ResourceOperandInterface *> &ResourceAcquire() {
    return resource_acquire_;
  }
  inline void AppendResourceAcquire(ResourceOperandInterface *op) {
    resource_acquire_.push_back(op);
  }

  void SetDisassemblyString(std::string disasm) {
    disasm_string_ = std::move(disasm);
  }

  std::string AsString() const;

  // Setter and getter for the integer attributes.
  absl::Span<const int> Attributes() const { return attributes_; }

  void SetAttributes(absl::Span<const int> attributes);

 private:
  // Instruction operands.
  PredicateOperandInterface *predicate_ = nullptr;
  std::vector<SourceOperandInterface *> sources_;
  std::vector<DestinationOperandInterface *> dests_;

  // The resources that must be available in order to issue the instruction.
  // This includes any registers that are read.
  std::vector<ResourceOperandInterface *> resource_hold_;
  // The resources that must be reserved/acquired by the instruction. Each
  // vector element is a set of resources that are acquired when the instruction
  // issues. The method Acquire() should be called on each element of the
  // vector. The operands should contain all registers and other resources that
  // that need to be reserved for writing.
  std::vector<ResourceOperandInterface *> resource_acquire_;
  // Simulated instruction size.
  int size_;
  // Simulated instruction address.
  uint64_t address_;
  // Integer value of the opcode enum.
  int opcode_;
  // Text string of disassembly of the instruction.
  std::string disasm_string_;
  // Optional integer attribute array. This allows the decoder to create and
  // store a set of different attributes in the instruction. This is implemented
  // as absl::Span<int> so it can be used as an array of integer attributes
  // with length determined by the decoder. An example attribute is a
  // privilege level, or whether the instruction is a branch or not.
  // The attribute array is owned by the Instruction object. The attributes are
  // accessed as a const span, so the attributes are read only.
  int *attribute_array_ = nullptr;
  absl::Span<const int> attributes_ =
      absl::MakeConstSpan(static_cast<int *>(nullptr), 0);
  // Architecture state object.
  ArchState *state_;
  // Instruction execution context (this is usuall nullptr).
  ReferenceCount *context_;
  // Semantic function that implements the instruction semantics.
  SemanticFunction semantic_fcn_;
  // Pointer to the child (or sub) instruction. Used to break an instruction
  // up into multiple semantic actions, such as a VLIW instruction.
  Instruction *child_;
  // Parent instruction pointer from child instruction.
  Instruction *parent_;
  // Pointer to the "next" instruction (instructions can be linked into a list
  // of instructions), such as those instances that make up the instructions in
  // a VLIW instruction word.
  Instruction *next_;
};

// Templated inline helper functions for operand access. These are intended to
// provide access to instruction operands from instruction semantic functions
// that are themselves templated on the operand types.

// The base case shouldn't be matched. No return statement is provided.
template <typename T>
inline T GetInstructionSource(const Instruction *inst, int index) { /*empty */ }

// The following provide specializations for each of the integral types of
// operand values, both signed and unsigned.
template <>
inline bool GetInstructionSource<bool>(const Instruction *inst, int index) {
  return inst->Source(index)->AsBool(0);
}
template <>
inline uint8_t GetInstructionSource<uint8_t>(const Instruction *inst,
                                             int index) {
  return inst->Source(index)->AsUint8(0);
}
template <>
inline int8_t GetInstructionSource<int8_t>(const Instruction *inst, int index) {
  return inst->Source(index)->AsInt8(0);
}
template <>
inline uint16_t GetInstructionSource<uint16_t>(const Instruction *inst,
                                               int index) {
  return inst->Source(index)->AsUint16(0);
}
template <>
inline int16_t GetInstructionSource<int16_t>(const Instruction *inst,
                                             int index) {
  return inst->Source(index)->AsInt16(0);
}
template <>
inline uint32_t GetInstructionSource<uint32_t>(const Instruction *inst,
                                               int index) {
  return inst->Source(index)->AsUint32(0);
}
template <>
inline int32_t GetInstructionSource<int32_t>(const Instruction *inst,
                                             int index) {
  return inst->Source(index)->AsInt32(0);
}
template <>
inline float GetInstructionSource<float>(const Instruction *inst, int index) {
  auto value = inst->Source(index)->AsUint32(0);
  return *reinterpret_cast<float *>(&value);
}
template <>
inline uint64_t GetInstructionSource<uint64_t>(const Instruction *inst,
                                               int index) {
  return inst->Source(index)->AsUint64(0);
}
template <>
inline int64_t GetInstructionSource<int64_t>(const Instruction *inst,
                                             int index) {
  return inst->Source(index)->AsInt64(0);
}
template <>
inline double GetInstructionSource<double>(const Instruction *inst, int index) {
  auto value = inst->Source(index)->AsUint64(0);
  return *reinterpret_cast<double *>(&value);
}
template <>
inline absl::uint128 GetInstructionSource<absl::uint128>(
    const Instruction *inst, int index) {
  return static_cast<absl::uint128>(inst->Source(index)->AsUint64(0));
}
template <>
inline absl::int128 GetInstructionSource<absl::int128>(const Instruction *inst,
                                                       int index) {
  return static_cast<absl::int128>(inst->Source(index)->AsInt64(0));
}

// The base case shouldn't be matched. No return statement is provided, so it
// will generate a compile time error if no other case is matched.
template <typename T>
inline T GetInstructionSource(const Instruction *inst, int index,
                              int element) { /*empty */ }
// The following provide specializations for each of the integral types of
// operand values, both signed and unsigned.
template <>
inline bool GetInstructionSource<bool>(const Instruction *inst, int index,
                                       int element) {
  return inst->Source(index)->AsBool(element);
}
template <>
inline uint8_t GetInstructionSource<uint8_t>(const Instruction *inst, int index,
                                             int element) {
  return inst->Source(index)->AsUint8(element);
}
template <>
inline int8_t GetInstructionSource<int8_t>(const Instruction *inst, int index,
                                           int element) {
  return inst->Source(index)->AsInt8(element);
}
template <>
inline uint16_t GetInstructionSource<uint16_t>(const Instruction *inst,
                                               int index, int element) {
  return inst->Source(index)->AsUint16(element);
}
template <>
inline int16_t GetInstructionSource<int16_t>(const Instruction *inst, int index,
                                             int element) {
  return inst->Source(index)->AsInt16(element);
}
template <>
inline uint32_t GetInstructionSource<uint32_t>(const Instruction *inst,
                                               int index, int element) {
  return inst->Source(index)->AsUint32(element);
}
template <>
inline int32_t GetInstructionSource<int32_t>(const Instruction *inst, int index,
                                             int element) {
  return inst->Source(index)->AsInt32(element);
}
template <>
inline float GetInstructionSource<float>(const Instruction *inst, int index,
                                         int element) {
  auto value = inst->Source(index)->AsUint32(element);
  return *reinterpret_cast<float *>(&value);
}
template <>
inline uint64_t GetInstructionSource<uint64_t>(const Instruction *inst,
                                               int index, int element) {
  return inst->Source(index)->AsUint64(element);
}
template <>
inline int64_t GetInstructionSource<int64_t>(const Instruction *inst, int index,
                                             int element) {
  return inst->Source(index)->AsInt64(element);
}
template <>
inline double GetInstructionSource<double>(const Instruction *inst, int index,
                                           int element) {
  auto value = inst->Source(index)->AsUint64(element);
  return *reinterpret_cast<double *>(&value);
}

}  // namespace generic
}  // namespace sim
}  // namespace mpact

#endif  // MPACT_SIM_GENERIC_INSTRUCTION_H_
