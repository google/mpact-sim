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

#ifndef MPACT_SIM_DECODER_BIN_DECODER_H_
#define MPACT_SIM_DECODER_BIN_DECODER_H_

#include <deque>
#include <string>
#include <vector>

#include "absl/container/btree_map.h"
#include "mpact/sim/decoder/bin_encoding_info.h"
#include "mpact/sim/decoder/decoder_error_listener.h"

// This file contains information specific to each top level binary decoder
// (specified in the .bin file) for which code will be generated.

namespace mpact {
namespace sim {
namespace decoder {
namespace bin_format {

class BinEncodingInfo;
class InstructionGroup;

class BinDecoder {
 public:
  using InstructionGroupMultiMap =
      absl::btree_multimap<std::string, InstructionGroup *>;

  BinDecoder(std::string name, BinEncodingInfo *encoding_info,
             DecoderErrorListener *error_listener);
  ~BinDecoder();

  // Checks for invalid encodings, such as some duplicates.
  void CheckEncodings();
  // Select instruction group for decoder generation.
  void AddInstructionGroup(InstructionGroup *group);

  // Accessors.
  const std::string &name() const { return name_; }
  DecoderErrorListener *error_listener() const { return error_listener_; }
  BinEncodingInfo *encoding_info() const { return encoding_info_; }
  std::vector<InstructionGroup *> instruction_group_vec() {
    return instruction_group_vec_;
  }
  std::deque<std::string> &namespaces() { return namespaces_; }

 private:
  // Encoder name.
  std::string name_;
  // The global decoder structure.
  BinEncodingInfo *encoding_info_;
  // Error handler.
  DecoderErrorListener *error_listener_;
  // The set of instruction groups in this decoder.
  std::vector<InstructionGroup *> instruction_group_vec_;
  // Namespace container.
  std::deque<std::string> namespaces_;
};

}  // namespace bin_format
}  // namespace decoder
}  // namespace sim
}  // namespace mpact

#endif  // MPACT_SIM_DECODER_BIN_DECODER_H_
