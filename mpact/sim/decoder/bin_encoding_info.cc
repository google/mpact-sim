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

#include "mpact/sim/decoder/bin_encoding_info.h"

#include <string>
#include <utility>

#include "mpact/sim/decoder/bin_decoder.h"
#include "mpact/sim/decoder/decoder_error_listener.h"
#include "mpact/sim/decoder/format.h"
#include "mpact/sim/decoder/instruction_group.h"
#include "absl/status/status.h"
#include "absl/strings/string_view.h"

namespace mpact {
namespace sim {
namespace decoder {
namespace bin_format {

BinEncodingInfo::BinEncodingInfo(std::string opcode_enum,
                                 DecoderErrorListener *error_listener)
    : opcode_enum_(opcode_enum), error_listener_(error_listener) {}

BinEncodingInfo::~BinEncodingInfo() { delete decoder_; }

// Add name of an include file to be included in the generated code.
void BinEncodingInfo::AddIncludeFile(std::string include_file) {
  include_files_.insert(std::move(include_file));
}

// Adding a format that does not have a parent (to inherit from ).
absl::StatusOr<Format *> BinEncodingInfo::AddFormat(std::string name,
                                                    int width) {
  // Verify that the format name hasn't been used.
  if (format_map_.contains(name)) {
    return absl::InternalError(
        absl::StrCat("Error: format '", name, "' already defined"));
  }
  auto format = new Format(name, width, this);
  format_map_.emplace(name, format);
  return format;
}

// Adding a format that does have a parent.
absl::StatusOr<Format *> BinEncodingInfo::AddFormat(std::string name, int width,
                                                    std::string parent_name) {
  // Verify that the format name hasn't been used.
  if (format_map_.contains(name)) {
    return absl::InternalError(
        absl::StrCat("Error: format '", name, "' already defined"));
  }
  auto format = new Format(name, width, parent_name, this);
  format_map_.emplace(name, format);
  return format;
}

// Lookup a format by name. Return nullptr if it isn't found.
Format *BinEncodingInfo::GetFormat(absl::string_view name) const {
  auto iter = format_map_.find(name);
  if (iter == format_map_.end()) return nullptr;
  return iter->second;
}

// Add the named instruction group. Instruction encodings are added directly
// to the group using the returned pointer.
absl::StatusOr<InstructionGroup *> BinEncodingInfo::AddInstructionGroup(
    std::string name, int width, std::string format_name) {
  if (instruction_group_map_.contains(name)) {
    return absl::InternalError(
        absl::StrCat("Error: instruction group '", name, "' already defined"));
  }
  auto group =
      new InstructionGroup(name, width, format_name, opcode_enum_, this);
  instruction_group_map_.emplace(name, group);
  return group;
}

// Top level method that calls the checking method of each format. This is
// called after all the formats have been added.
absl::Status BinEncodingInfo::CheckFormats() {
  for (auto &[unused, format] : format_map_) {
    // For the base formats (those who do not inherit from another format).
    if (format->base_format() == nullptr) {
      format->PropagateExtractorsUp();
    }
  }

  for (auto &[unused, format] : format_map_) {
    if (format->base_format() == nullptr) {
      format->PropagateExtractorsDown();
    }
  }
  return absl::OkStatus();
}

BinDecoder *BinEncodingInfo::AddBinDecoder(std::string name) {
  if (decoder_ != nullptr) {
    error_listener_->semanticError(nullptr, "Can only select one decoder");
    return nullptr;
  }
  auto *bin_decoder = new BinDecoder(name, this, error_listener());
  decoder_ = bin_decoder;
  return bin_decoder;
}

}  // namespace bin_format
}  // namespace decoder
}  // namespace sim
}  // namespace mpact
