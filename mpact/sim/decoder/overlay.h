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

#ifndef _MPACT_SIM_DECODER_OVERLAY_H_
#define _MPACT_SIM_DECODER_OVERLAY_H_

#include <string>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "mpact/sim/decoder/bin_format_visitor.h"
#include "mpact/sim/decoder/extract.h"

// This file defines classes necessary to handle reinterpretation of bitfields
// in a format. These are known as overlays. This allows new usable "fields" to
// be created as aliases to bits in the format. An overlay consists of a
// concatenation left to right of a sequence of either field or format
// references, or constant bit strings. Field references can only refer to
// fields in the same format. Each field reference may select the whole field,
// or just a set of bit ranges. A format reference does not refer to a field
// name, instead it only enumerates bit ranges of the format itself. The overlay
// enables an overlay to create a new "field" that can consist of rearrangement
// of bits in the original format, or a way to add constant bits that may be
// implied in the instruction encoding itself.

namespace mpact {
namespace sim {
namespace decoder {
namespace bin_format {

class Format;
struct Field;

// Helper class to store an individual component in an overlay.
class BitsOrField {
 public:
  BitsOrField(Field *field, int high, int low, int width);
  BitsOrField(BinaryNum bin_num, int width);

  Field *field() const { return field_; }
  // If != >= 0, this is the high bit position of the format that this component
  // refers to.
  int high() const { return high_; }
  void set_high(int value) { high_ = value; }
  // If != >= 0, this is the low bit position of the format that this component
  // refers to.
  int low() const { return low_; }
  void set_low(int value) { low_ = value; }
  // Returns the width of the component.
  int width() const { return width_; }
  // Returns the position (counting right to left) of the high bit of the
  // overlay component within the overlay.
  int position() const { return position_; }
  void set_position(int value) { position_ = value; }
  // If high() is >= , this contains the binary number specification for the bit
  // string.
  BinaryNum bin_num() const { return bin_num_; }

 private:
  Field *field_;
  int high_ = -1;
  int low_ = -1;
  int width_ = -1;
  int position_ = -1;
  BinaryNum bin_num_;
};

// This is the overlay class that encodes a reinterpretation of bits in the
// format.
class Overlay {
 public:
  Overlay(std::string name, bool is_signed, int width, Format *format);
  ~Overlay();

  // The following methods add components to the overlay. Components are added
  // left to right in order.

  // Add a bit constant to the overlay.
  void AddBitConstant(BinaryNum bin_num);
  // Add an entire field from the format to the overlay.
  absl::Status AddFieldReference(std::string field_name);
  // Add only the bit ranges from the given field to the overlay (in order of
  // appearance in the vector).
  absl::Status AddFieldReference(std::string field_name,
                                 const std::vector<BitRange> &ranges);
  // Add the bit ranges from the format to the overlay (in order of appearance
  // in the vector).
  absl::Status AddFormatReference(const std::vector<BitRange> &ranges);

  // Adjusts high/low of each field reference.
  absl::Status ComputeHighLow();
  // Given input as the bit value of the format, returns the unsigned bit value
  // of the overlay as specified by the components.
  absl::StatusOr<uint64_t> GetValue(uint64_t input);
  std::string WriteSimpleValueExtractor(absl::string_view value,
                                        absl::string_view result) const;
  absl::StatusOr<uint64_t> GetValue(uint8_t *input);
  std::string WriteComplexValueExtractor(absl::string_view value,
                                         absl::string_view result) const;
  absl::StatusOr<uint64_t> GetBitField(uint64_t input);

  bool operator==(const Overlay &rhs) const;
  bool operator!=(const Overlay &rhs) const;

  // Accessors.
  const std::string &name() { return name_; }
  bool is_signed() const { return is_signed_; }
  int declared_width() const { return declared_width_; }
  int computed_width() const { return computed_width_; }
  uint64_t mask() const { return mask_; }
  const std::vector<BitsOrField *> component_vec() const {
    return component_vec_;
  }
  bool must_be_extracted() const { return must_be_extracted_; }
  Format *format() const { return format_; }

 private:
  std::string name_;
  bool high_low_computed_ = false;
  bool is_signed_;
  int declared_width_;
  int computed_width_ = 0;
  uint64_t mask_ = 0;
  bool must_be_extracted_ = false;
  Format *format_ = nullptr;
  std::vector<BitsOrField *> component_vec_;
};

}  // namespace bin_format
}  // namespace decoder
}  // namespace sim
}  // namespace mpact

#endif  // _MPACT_SIM_DECODER_OVERLAY_H_
