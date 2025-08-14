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

#ifndef MPACT_SIM_GENERIC_DECODER_INTERFACE_H_
#define MPACT_SIM_GENERIC_DECODER_INTERFACE_H_

#include <cstdint>

namespace mpact {
namespace sim {
namespace generic {

class Instruction;

// This is the simulator's interface to the instruction decoder.
class DecoderInterface {
 public:
  // Return a decoded instruction for the given address. If there are errors
  // in the instruction decoding, the decoder should still produce an
  // instruction that can be executed, but its semantic action function should
  // set an error condition in the simulation when executed.
  virtual Instruction* DecodeInstruction(uint64_t address) = 0;
  virtual int GetNumOpcodes() const = 0;
  virtual const char* GetOpcodeName(int index) const = 0;
  virtual ~DecoderInterface() = default;
};

}  // namespace generic
}  // namespace sim
}  // namespace mpact

#endif  // MPACT_SIM_GENERIC_DECODER_INTERFACE_H_
