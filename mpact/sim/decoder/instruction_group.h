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

#include <string>
#include <tuple>
#include <vector>

#include "mpact/sim/decoder/bin_encoding_info.h"
#include "mpact/sim/decoder/encoding_group.h"
#include "absl/container/btree_map.h"
#include "absl/container/flat_hash_set.h"

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

  InstructionEncoding *AddInstructionEncoding(std::string name, Format *format);
  void AddInstructionEncoding(InstructionEncoding *encoding);
  // Process the encodings in the group, partitioning them into subgroups
  // according to their opcode bits to make it easy to generate a hierarchical
  // decoding tree.
  void ProcessEncodings();
  // Check encodings for duplicates etc.
  void CheckEncodings();
  // Generate and emit code for decoding this instruction group.
  std::tuple<std::string, std::string> EmitCode();
  // Return a string containing information about this instruction group and
  // how it has been partitioned across encoding groups.
  std::string WriteGroup();

  // Accessors.
  const std::string &name() const { return name_; }
  const std::string &format_name() const { return format_name_; }
  const std::string &opcode_enum() const { return opcode_enum_; }
  const std::vector<InstructionEncoding *> &encoding_vec() const {
    return encoding_vec_;
  }
  int width() const { return width_; }
  BinEncodingInfo *encoding_info() const { return encoding_info_; }

 private:
  std::string name_;
  int width_;
  std::string format_name_;
  Format *format_;
  std::string opcode_enum_;
  BinEncodingInfo *encoding_info_;
  std::vector<InstructionEncoding *> encoding_vec_;
  absl::flat_hash_set<std::string> encoding_name_set_;
  absl::btree_multimap<uint64_t, InstructionEncoding *> encoding_map_;
  std::vector<EncodingGroup *> encoding_group_vec_;
};

}  // namespace bin_format
}  // namespace decoder
}  // namespace sim
}  // namespace mpact

#endif  // MPACT_SIM_DECODER_INSTRUCTION_GROUP_H_
