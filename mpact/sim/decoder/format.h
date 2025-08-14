#ifndef MPACT_SIM_DECODER_FORMAT_H_
#define MPACT_SIM_DECODER_FORMAT_H_

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

#include <map>
#include <string>
#include <vector>

#include "absl/container/btree_map.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "antlr4-runtime/Token.h"

// This file declares the classes necessary to manage instruction formats,
// defined as a sequence of fields (or sub-formats) as well as a set of
// overlays. The format provides a way of defining an interface for accessing
// the different parts of an instruction encoding.

namespace mpact {
namespace sim {
namespace decoder {
namespace bin_format {

class BinEncodingInfo;
class Overlay;
class Format;

// Helper struct to store the information about an individual field.
struct Field {
  std::string name;
  bool is_signed;
  int high;
  int low;
  int width;
  Format* format = nullptr;
  Field(std::string name_, bool is_signed_, int width_, Format* format_)
      : name(name_),
        is_signed(is_signed_),
        high(-1),
        low(-1),
        width(width_),
        format(format_) {}
};

class Format;

// Captures a reference to a format by name and how many instances.
struct FormatReference {
  std::string name;
  int size;
  explicit FormatReference(std::string name_) : FormatReference(name_, 1) {}
  FormatReference(std::string name_, int size_) : name(name_), size(size_) {}
};

// Wrapper class to store information about each component of the format.

class FieldOrFormat {
 public:
  explicit FieldOrFormat(Field* field) : is_field_(true), field_(field) {}
  FieldOrFormat(std::string format_alias, std::string fmt_name, int size,
                antlr4::Token* ctx)
      : is_field_(false),
        format_name_(fmt_name),
        format_alias_(format_alias),
        size_(size),
        ctx_(ctx) {}
  ~FieldOrFormat();

  bool is_field() const { return is_field_; }
  Field* field() const { return field_; }
  int high() const { return high_; }
  void set_high(int value) { high_ = value; }
  const std::string& format_name() const { return format_name_; }
  antlr4::Token* ctx() const { return ctx_; }
  Format* format() const { return format_; }
  absl::string_view format_alias() const { return format_alias_; }
  int size() const { return size_; }
  void set_format(Format* fmt) { format_ = fmt; }

  bool operator==(const FieldOrFormat& rhs) const;
  bool operator!=(const FieldOrFormat& rhs) const;

 private:
  bool is_field_;
  Field* field_ = nullptr;
  std::string format_name_;
  std::string format_alias_;
  int high_ = 0;
  int size_ = 0;
  antlr4::Token* ctx_ = nullptr;
  Format* format_ = nullptr;
};

struct Extractors {
  std::string h_output;
  std::string class_output;
  std::string types_output;
};

class Format {
 public:
  // Layout type of the format.
  enum class Layout {
    kDefault,
    kPackedStruct,
  };

  Format() = delete;
  Format(std::string name, int width, BinEncodingInfo* encoding_info);
  Format(std::string name, int width, std::string base_format_name,
         BinEncodingInfo* encoding_info);
  ~Format();

  // Adds a field (signed or unsigned) of the given width to the format.
  absl::Status AddField(std::string name, bool is_signed, int width);
  // Adds a format reference to the current format. It will be resolved to
  // another format later, or generate an error at that time.
  void AddFormatReferenceField(std::string format_alias,
                               std::string format_name, int size,
                               antlr4::Token* ctx);
  // Adds an overlay to the format. An overlay is an alias to a set of bits
  // in the instruction format.
  absl::StatusOr<Overlay*> AddFieldOverlay(std::string name, bool is_signed,
                                           int width);

  // Returns the named field if it exists in the format. Otherwise it returns
  // nullptr.
  Field* GetField(absl::string_view field_name) const;
  // Returns the named overlay if it exists in the format. Otherwise it returns
  // nullptr.
  Overlay* GetOverlay(absl::string_view overlay_name) const;

  // Performs a consistency check on the format.
  absl::Status ComputeAndCheckFormatWidth();
  // Propagate extractors to the top level in the format inheritance
  // hierarchy.
  void PropagateExtractorsUp();
  void PropagateExtractorsDown();
  // Generates definitions of the field and overlay extractors in the format.
  Extractors GenerateExtractors() const;
  // Generates definitions of the field and overlay inserters in the format.
  std::string GenerateInserters() const;

  // True if the current format is a descendent of format.
  bool IsDerivedFrom(const Format* format);

  // Accessors.
  const std::string& name() const { return name_; }
  // The unsigned integer type name larger or equal to the format width.
  // E.g. uint32_t etc.
  const std::string& uint_type_name() const { return uint_type_name_; }
  int declared_width() const { return declared_width_; }
  int computed_width() const { return computed_width_; }
  Format* base_format() const { return base_format_; }
  // Return pointer to the parent encoding info class.
  BinEncodingInfo* encoding_info() const { return encoding_info_; }
  // Field layout.
  Layout layout() const { return layout_; }
  void set_layout(Layout layout) { layout_ = layout; }

 private:
  bool HasExtract(const std::string& name) const;
  bool HasOverlayExtract(const std::string& name) const;

  // Extractor generators.
  std::string GeneratePackedStructTypes() const;
  std::string GeneratePackedStructFieldExtractor(const Field* field) const;
  std::string GeneratePackedStructFormatExtractor(std::string_view format_alias,
                                                  const Format* format,
                                                  int high, int size) const;
  std::string GeneratePackedStructOverlayExtractor(Overlay* overlay) const;
  std::string GenerateFieldExtractor(const Field* field) const;
  std::string GenerateFormatExtractor(std::string_view format_alias,
                                      const Format* format, int high,
                                      int size) const;
  std::string GenerateOverlayExtractor(Overlay* overlay) const;
  // Inserters.
  std::string GenerateFieldInserter(const Field* field) const;
  std::string GeneratePackedStructFieldInserter(const Field* field) const;
  std::string GenerateFormatInserter(std::string_view format_alias,
                                     const Format* format, int high,
                                     int size) const;
  std::string GeneratePackedStructFormatInserter(std::string_view format_alias,
                                                 const Format* format, int high,
                                                 int size) const;
  std::string GenerateReplicatedFormatInserter(std::string_view format_alias,
                                               const Format* format, int high,
                                               int size) const;
  std::string GenerateSingleFormatInserter(std::string_view format_alias,
                                           const Format* format,
                                           int high) const;
  std::string GenerateOverlayInserter(Overlay* overlay) const;
  // Return string representation of the int type that contains bitwidth bits.
  std::string GetIntType(int bitwidth) const;
  std::string GetUIntType(int bitwidth) const;
  int GetIntTypeBitWidth(int bitwidth) const;

  std::string name_;
  std::string base_format_name_;
  std::string uint_type_name_;
  std::string int_type_name_;
  int declared_width_;
  int computed_width_ = 0;
  Layout layout_ = Layout::kDefault;
  Format* base_format_ = nullptr;
  std::vector<Format*> derived_formats_;
  BinEncodingInfo* encoding_info_;

  absl::btree_map<std::string, Overlay*> overlay_map_;
  absl::btree_map<std::string, Field*> field_map_;
  std::vector<FieldOrFormat*> field_vec_;
  // Using std::map because of sorted traversal and better iterator stability
  // when elements are erased.
  std::map<std::string, FieldOrFormat*> extractors_;
  std::map<std::string, Overlay*> overlay_extractors_;
};

}  // namespace bin_format
}  // namespace decoder
}  // namespace sim
}  // namespace mpact

#endif  // MPACT_SIM_DECODER_FORMAT_H_
