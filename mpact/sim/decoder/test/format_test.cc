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

#include "mpact/sim/decoder/format.h"

#include "googlemock/include/gmock/gmock.h"
#include "googletest/include/gtest/gtest.h"
#include "mpact/sim/decoder/bin_encoding_info.h"
#include "mpact/sim/decoder/decoder_error_listener.h"
#include "mpact/sim/decoder/overlay.h"

namespace {

using ::mpact::sim::decoder::DecoderErrorListener;
using ::mpact::sim::decoder::bin_format::BinEncodingInfo;
using ::mpact::sim::decoder::bin_format::Format;

/* The test format is defined as:
 *
 *  fields:
 *    unsigned func3[3];
 *    unsigned imm3[3];
 *    unsigned rs1p[3];
 *    unsigned imm2[2];
 *    unsigned rdp[3];
 *    unsigned op[2];
 *  overlays:
 *    unsigned imm_w[7] = imm2[0], imm3, imm2[1], 0b00;
 */

class FormatTest : public testing::Test {
 protected:
  FormatTest() {
    error_listener_ = new DecoderErrorListener();
    encoding_info_ = new BinEncodingInfo("OpcodeEnum", error_listener_);
  }

  ~FormatTest() override {
    delete error_listener_;
    delete encoding_info_;
  }

  DecoderErrorListener *error_listener_;
  BinEncodingInfo *encoding_info_;
};

TEST_F(FormatTest, Constructor) {
  auto *format = new Format("format_name", 16, encoding_info_);
  EXPECT_EQ(format->name(), "format_name");
  EXPECT_EQ(format->declared_width(), 16);
  EXPECT_EQ(format->computed_width(), 0);
  EXPECT_EQ(format->ComputeAndCheckFormatWidth().code(),
            absl::StatusCode::kInternal);
  EXPECT_EQ(format->encoding_info(), encoding_info_);
  delete format;

  format = new Format("derived_format", 16, "base_format", encoding_info_);
  EXPECT_EQ(format->name(), "derived_format");
  EXPECT_EQ(format->declared_width(), 16);
  EXPECT_EQ(format->computed_width(), 0);
  EXPECT_EQ(format->ComputeAndCheckFormatWidth().code(),
            absl::StatusCode::kInternal);
  EXPECT_EQ(format->encoding_info(), encoding_info_);
  delete format;
}

TEST_F(FormatTest, AddFields) {
  auto *format = new Format("format", 16, encoding_info_);
  ASSERT_TRUE(format->AddField("func3", /*is_signed=*/false, 3).ok());
  ASSERT_TRUE(format->AddField("imm3", /*is_signed=*/false, 3).ok());
  ASSERT_TRUE(format->AddField("rs1p", /*is_signed=*/false, 3).ok());
  ASSERT_TRUE(format->AddField("imm2", /*is_signed=*/false, 2).ok());
  ASSERT_TRUE(format->AddField("rdp", /*is_signed=*/false, 3).ok());
  ASSERT_TRUE(format->AddField("op", /*is_signed=*/false, 2).ok());
  ASSERT_TRUE(format->ComputeAndCheckFormatWidth().ok());
  EXPECT_EQ(format->declared_width(), format->computed_width());
  for (auto &name : {"func3", "imm3", "rs1p", "imm2", "rdp", "op"}) {
    auto *field = format->GetField(name);
    ASSERT_NE(field, nullptr);
    EXPECT_EQ(field->name, name);
  }
  EXPECT_EQ(format->GetField("NotAField"), nullptr);
  delete format;
}

TEST_F(FormatTest, AddOverlay) {
  auto *format = new Format("format", 16, encoding_info_);
  ASSERT_TRUE(format->AddField("func3", /*is_signed=*/false, 3).ok());
  ASSERT_TRUE(format->AddField("imm3", /*is_signed=*/false, 3).ok());
  ASSERT_TRUE(format->AddField("rs1p", /*is_signed=*/false, 3).ok());
  ASSERT_TRUE(format->AddField("imm2", /*is_signed=*/false, 2).ok());
  ASSERT_TRUE(format->AddField("rdp", /*is_signed=*/false, 3).ok());
  ASSERT_TRUE(format->AddField("op", /*is_signed=*/false, 2).ok());
  auto result = format->AddFieldOverlay("imm_w", /*is_signed=*/false, 7);
  ASSERT_TRUE(result.status().ok());
  EXPECT_NE(result.value(), nullptr);
  EXPECT_EQ(result.value()->name(), "imm_w");
  EXPECT_EQ(result.value()->declared_width(), 7);
  ASSERT_TRUE(format->ComputeAndCheckFormatWidth().ok());
  delete format;
}

// The GenerateExtractors is not tested in this unit test. It will be tested
// during integration.

}  // namespace
