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

#ifndef MPACT_SIM_DECODER_BIN_ENCODING_INFO_H_
#define MPACT_SIM_DECODER_BIN_ENCODING_INFO_H_

#include <string>

#include "absl/container/btree_map.h"
#include "absl/container/btree_set.h"
#include "absl/status/statusor.h"
#include "mpact/sim/decoder/decoder_error_listener.h"

namespace mpact {
namespace sim {
namespace decoder {
namespace bin_format {

class Format;
class InstructionGroup;
class BinDecoder;

// This class is the top level container for the parsed data from the binary
// instruction format input file.
class BinEncodingInfo {
 public:
  using InstructionGroupMap = absl::btree_map<std::string, InstructionGroup *>;
  using FormatMap = absl::btree_map<std::string, Format *>;

  BinEncodingInfo() = delete;
  BinEncodingInfo(std::string opcode_enum,
                  DecoderErrorListener *error_listener);
  ~BinEncodingInfo();

  // Add a file to be included in the generated code.
  void AddIncludeFile(std::string include_file);
  // Add instruction format declaration - no parent (inherited) format.
  absl::StatusOr<Format *> AddFormat(std::string name, int width);
  // Add instruction format declaration with a parent format.
  absl::StatusOr<Format *> AddFormat(std::string name, int width,
                                     std::string parent_name);
  // Returns the pointer to the format with the given name, or nullptr if it
  // hasn't been added.
  Format *GetFormat(absl::string_view name) const;

  // Add the instruction group declaration.
  absl::StatusOr<InstructionGroup *> AddInstructionGroup(
      std::string name, int width, std::string format_name);

  // Propagates bitfield extractors where possible.
  void PropagateExtractors();

  // Create and add binary decoder descriptor.
  BinDecoder *AddBinDecoder(std::string name);

  // Accessors.
  const FormatMap &format_map() const { return format_map_; }
  const InstructionGroupMap &instruction_group_map() const {
    return instruction_group_map_;
  }
  DecoderErrorListener *error_listener() const { return error_listener_; }
  const absl::btree_set<std::string> &include_files() const {
    return include_files_;
  }
  BinDecoder *decoder() const { return decoder_; }

 private:
  std::string opcode_enum_;
  // The error listener is passed from the parse tree visitor. It is used to
  // report semantic errors found during later checks.
  DecoderErrorListener *error_listener_;
  FormatMap format_map_;
  InstructionGroupMap instruction_group_map_;
  // Include files.
  absl::btree_set<std::string> include_files_;
  BinDecoder *decoder_ = nullptr;
};

}  // namespace bin_format
}  // namespace decoder
}  // namespace sim
}  // namespace mpact

#endif  // MPACT_SIM_DECODER_BIN_ENCODING_INFO_H_
