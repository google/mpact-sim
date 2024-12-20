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

#ifndef MPACT_SIM_DECODER_INSTRUCTION_GROUP_H_
#define MPACT_SIM_DECODER_INSTRUCTION_GROUP_H_

#include <cstdint>
#include <string>
#include <tuple>
#include <vector>

#include "absl/container/btree_map.h"
#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "antlr4-runtime/Token.h"
#include "mpact/sim/decoder/bin_encoding_info.h"
#include "mpact/sim/decoder/encoding_group.h"

namespace mpact {
namespace sim {
namespace decoder {
namespace bin_format {

class BinDecoder;
class InstructionEncoding;
class Format;

// An instruction group corresponds to an instruction group in the input file.
// Each instruction group gets subdivided into one or more encoding groups that
// have constraints on overlapping bits in the instruction word.
class InstructionGroup {
 public:
  InstructionGroup() = default;
  InstructionGroup(std::string name, int width, std::string format_name,
                   std::string opcode_enum, BinEncodingInfo *encoding_info);
  ~InstructionGroup();

  InstructionEncoding *AddInstructionEncoding(antlr4::Token *token,
                                              std::string name, Format *format);
  void AddInstructionEncoding(InstructionEncoding *encoding);
  // Process the encodings in the group, partitioning them into subgroups
  // according to their opcode bits to make it easy to generate a hierarchical
  // decoding tree.
  void ProcessEncodings();
  // Check encodings for duplicates etc.
  void CheckEncodings();
  // Generate and emit code for decoding this instruction group.
  std::tuple<std::string, std::string> EmitDecoderCode();
  // Collect the encodings for these instructions.
  void GetInstructionEncodings(
      absl::btree_map<std::string, std::tuple<uint64_t, int>> &encodings);
  // Return a string containing information about this instruction group and
  // how it has been partitioned across encoding groups.
  std::string WriteGroup();
  // Add a specialization to this instruction group.
  absl::Status AddSpecialization(const std::string &name,
                                 const std::string &parent_name,
                                 InstructionEncoding *encoding);

  // Accessors.
  const std::string &name() const { return name_; }
  const std::string &format_name() const { return format_name_; }
  const std::string &opcode_enum() const { return opcode_enum_; }
  const std::vector<InstructionEncoding *> &encoding_vec() const {
    return encoding_vec_;
  }
  int width() const { return width_; }
  BinEncodingInfo *encoding_info() const { return encoding_info_; }
  const absl::flat_hash_map<std::string, InstructionEncoding *> &
  encoding_name_map() const {
    return encoding_name_map_;
  }

  Format *format() const { return format_; }

 private:
  std::string name_;
  int width_;
  std::string format_name_;
  Format *format_;
  std::string opcode_enum_;
  BinEncodingInfo *encoding_info_;
  std::vector<InstructionEncoding *> encoding_vec_;
  absl::flat_hash_map<std::string, InstructionEncoding *> encoding_name_map_;
  absl::btree_multimap<uint64_t, InstructionEncoding *> encoding_map_;
  std::vector<EncodingGroup *> encoding_group_vec_;
};

}  // namespace bin_format
}  // namespace decoder
}  // namespace sim
}  // namespace mpact

#endif  // MPACT_SIM_DECODER_INSTRUCTION_GROUP_H_
