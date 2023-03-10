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

#include "mpact/sim/decoder/overlay.h"

#include "googlemock/include/gmock/gmock.h"
#include "googletest/include/gtest/gtest.h"
#include "mpact/sim/decoder/bin_format_visitor.h"
#include "mpact/sim/decoder/format.h"

namespace {

using ::mpact::sim::decoder::bin_format::BinaryNum;
using ::mpact::sim::decoder::bin_format::BitRange;
using ::mpact::sim::decoder::bin_format::Format;
using ::mpact::sim::decoder::bin_format::Overlay;

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

constexpr int kImm3Width = 3;
constexpr char kImm3Name[] = "imm3";
constexpr uint64_t kImm3Mask = 0b000'111'000'00'000'00;

constexpr char kImm2Name[] = "imm2";
constexpr uint64_t kImm2Mask0 = 0b000'000'000'01'000'00;

constexpr int kOverlayWidth = 7;
constexpr char kOverlayName[] = "imm_w";

class OverlayTest : public testing::Test {
 protected:
  OverlayTest() {
    // First create a format for the overlay to refer to.
    format_ = new Format("test", 16, nullptr);
    (void)format_->AddField("func3", /*is_signed=*/false, /*width=*/3);
    (void)format_->AddField(kImm3Name, /*is_signed=*/false, kImm3Width);
    (void)format_->AddField("rs1p", /*is_signed=*/false, /*width=*/3);
    (void)format_->AddField("imm2", /*is_signed=*/false, /*width=*/2);
    (void)format_->AddField("rdp", /*is_signed=*/false, /*width=*/3);
    (void)format_->AddField("op", /*is_signed=*/false, /*width=*/2);
    (void)format_->ComputeAndCheckFormatWidth();
  }

  ~OverlayTest() override { delete format_; }

  Format *format_;
};

// Test construction and initial state of an overlay.
TEST_F(OverlayTest, Constructor) {
  auto *overlay =
      new Overlay(kOverlayName, /*is_signed=*/false, kOverlayWidth, format_);
  EXPECT_EQ(overlay->name(), kOverlayName);
  EXPECT_FALSE(overlay->is_signed());
  EXPECT_EQ(overlay->declared_width(), 7);
  EXPECT_EQ(overlay->computed_width(), 0);
  EXPECT_EQ(overlay->mask(), 0);
  EXPECT_TRUE(overlay->component_vec().empty());
  EXPECT_FALSE(overlay->must_be_extracted());
  delete overlay;
}

// Add a full field reference.
TEST_F(OverlayTest, AddFieldReference) {
  auto *overlay =
      new Overlay(kOverlayName, /*is_signed=*/false, kOverlayWidth, format_);
  // Fail to add a reference to an unknown field.
  EXPECT_EQ(overlay->AddFieldReference("immXYZ").code(),
            absl::StatusCode::kInternal);
  ASSERT_TRUE(overlay->AddFieldReference(kImm3Name).ok());
  ASSERT_TRUE(overlay->ComputeHighLow().ok());
  EXPECT_EQ(overlay->computed_width(), kImm3Width);
  EXPECT_EQ(overlay->mask(), kImm3Mask);
  EXPECT_EQ(overlay->component_vec().size(), 1);
  EXPECT_FALSE(overlay->must_be_extracted());
  delete overlay;
}

// Add a field range reference.
TEST_F(OverlayTest, AddFieldRangeReference) {
  auto *overlay =
      new Overlay(kOverlayName, /*is_signed=*/false, kOverlayWidth, format_);
  // Fail to add a reference to an unknown field.
  EXPECT_EQ(
      overlay
          ->AddFieldReference("immXYZ", std::vector<BitRange>{BitRange{3, 2}})
          .code(),
      absl::StatusCode::kInternal);
  ASSERT_TRUE(
      overlay
          ->AddFieldReference(kImm2Name, std::vector<BitRange>{BitRange{0, 0}})
          .ok());
  ASSERT_TRUE(overlay->ComputeHighLow().ok());
  EXPECT_EQ(overlay->computed_width(), 1);
  EXPECT_EQ(overlay->mask(), kImm2Mask0);
  EXPECT_EQ(overlay->component_vec().size(), 1);
  EXPECT_FALSE(overlay->must_be_extracted());
  delete overlay;
}

// Add a format range reference.
TEST_F(OverlayTest, AddFormatReference) {
  auto *overlay =
      new Overlay(kOverlayName, /*is_signed=*/false, kOverlayWidth, format_);
  // Fail to add a reference to an unknown field.
  EXPECT_EQ(overlay->AddFormatReference(std::vector<BitRange>{BitRange{18, 16}})
                .code(),
            absl::StatusCode::kInternal);
  ASSERT_TRUE(
      overlay->AddFormatReference(std::vector<BitRange>{BitRange{12, 10}})
          .ok());
  ASSERT_TRUE(overlay->ComputeHighLow().ok());
  EXPECT_EQ(overlay->computed_width(), kImm3Width);
  EXPECT_EQ(overlay->mask(), kImm3Mask);
  EXPECT_EQ(overlay->component_vec().size(), 1);
  EXPECT_FALSE(overlay->must_be_extracted());
  delete overlay;
}

// Add a bit constant.
TEST_F(OverlayTest, AddBitConstant) {
  auto *overlay =
      new Overlay(kOverlayName, /*is_signed=*/false, kOverlayWidth, format_);
  overlay->AddBitConstant(BinaryNum{0b00, 2});
  ASSERT_TRUE(overlay->ComputeHighLow().ok());
  EXPECT_EQ(overlay->computed_width(), 2);
  EXPECT_EQ(overlay->mask(), 0);
  EXPECT_EQ(overlay->component_vec().size(), 1);
  EXPECT_TRUE(overlay->must_be_extracted());
  delete overlay;
}

// Full overlay test with value extraction.
TEST_F(OverlayTest, FullOverlay) {
  auto *overlay =
      new Overlay(kOverlayName, /*is_signed=*/false, kOverlayWidth, format_);
  ASSERT_TRUE(
      overlay
          ->AddFieldReference(kImm2Name, std::vector<BitRange>{BitRange{0, 0}})
          .ok());
  ASSERT_TRUE(overlay->AddFieldReference(kImm3Name).ok());
  ASSERT_TRUE(
      overlay
          ->AddFieldReference(kImm2Name, std::vector<BitRange>{BitRange{1, 1}})
          .ok());
  overlay->AddBitConstant(BinaryNum{0b00, 2});
  ASSERT_TRUE(overlay->ComputeHighLow().ok());
  EXPECT_EQ(overlay->computed_width(), overlay->declared_width());
  auto result = overlay->GetValue(0b000'000'000'01'000'00);
  ASSERT_TRUE(result.status().ok());
  EXPECT_EQ(result.value(), 0b1'000'0'00);
  result = overlay->GetValue(0b000'000'000'10'000'00);
  ASSERT_TRUE(result.status().ok());
  EXPECT_EQ(result.value(), 0b0'000'1'00);
  result = overlay->GetValue(0b000'001'000'00'000'00);
  ASSERT_TRUE(result.status().ok());
  EXPECT_EQ(result.value(), 0b0'001'0'00);
  delete overlay;
}

// Extraction code.
TEST_F(OverlayTest, WriteSimpleExtractor) {
  auto *overlay =
      new Overlay(kOverlayName, /*is_signed=*/false, kOverlayWidth, format_);
  ASSERT_TRUE(
      overlay
          ->AddFieldReference(kImm2Name, std::vector<BitRange>{BitRange{0, 0}})
          .ok());
  ASSERT_TRUE(overlay->AddFieldReference(kImm3Name).ok());
  ASSERT_TRUE(
      overlay
          ->AddFieldReference(kImm2Name, std::vector<BitRange>{BitRange{1, 1}})
          .ok());
  overlay->AddBitConstant(BinaryNum{0b00, 2});

  ASSERT_TRUE(overlay->ComputeHighLow().ok());

  auto c_code = overlay->WriteSimpleValueExtractor("value", "result");
  EXPECT_THAT(c_code, testing::HasSubstr(
                          absl::StrCat("result = (value & 0x20) << 1;")));
  EXPECT_THAT(c_code, testing::HasSubstr(
                          absl::StrCat("result |= (value & 0x40) >> 4;")));
  EXPECT_THAT(c_code, testing::HasSubstr(
                          absl::StrCat("result |= (value & 0x1c00) >> 7;")));
  delete overlay;
}

}  // namespace
