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

#include "mpact/sim/decoder/instruction_encoding.h"

#include <string>

#include "absl/status/status.h"
#include "googlemock/include/gmock/gmock.h"
#include "googletest/include/gtest/gtest.h"
#include "mpact/sim/decoder/bin_encoding_info.h"
#include "mpact/sim/decoder/bin_format_visitor.h"
#include "mpact/sim/decoder/decoder_error_listener.h"
#include "mpact/sim/decoder/format.h"

namespace {

// This file contains the unit tests for the InstructionEncoding class.

using ::mpact::sim::decoder::DecoderErrorListener;
using ::mpact::sim::decoder::bin_format::BinEncodingInfo;
using ::mpact::sim::decoder::bin_format::ConstraintType;
using ::mpact::sim::decoder::bin_format::Format;
using ::mpact::sim::decoder::bin_format::InstructionEncoding;

constexpr char kITypeEncodingName[] = "i_test_encoding";

constexpr int kFunc3Value = 0b101;
constexpr int kFunc3Mask = 0b111'00000'000'0000;

class InstructionEncodingTest : public ::testing::Test {
 protected:
  InstructionEncodingTest() {
    error_listener_ = new DecoderErrorListener();
    encoding_info_ = new BinEncodingInfo("OpcodeEnumName", error_listener_);
    i_type_fmt_ = new Format("IType", 32, encoding_info_);
    (void)i_type_fmt_->AddField("imm12", /*is_signed*/ true, 12);
    (void)i_type_fmt_->AddField("rs1", /*is_signed*/ false, 5);
    (void)i_type_fmt_->AddField("func3", /*is_signed*/ false, 3);
    (void)i_type_fmt_->AddField("rd", /*is_signed*/ false, 5);
    (void)i_type_fmt_->AddField("opcode", /*is_signed*/ false, 7);
    (void)i_type_fmt_->AddFieldOverlay("uspecial", /*is_signed*/ false, 10);
    auto *overlay = i_type_fmt_->GetOverlay("uspecial");
    (void)overlay->AddFieldReference("rs1");
    (void)overlay->AddFieldReference("rd");
    (void)i_type_fmt_->AddFieldOverlay("sspecial", /*is_signed*/ true, 10);
    overlay = i_type_fmt_->GetOverlay("sspecial");
    (void)overlay->AddFieldReference("rs1");
    (void)overlay->AddFieldReference("rd");
    (void)i_type_fmt_->ComputeAndCheckFormatWidth();
    i_type_ = new InstructionEncoding(kITypeEncodingName, i_type_fmt_);
  }

  ~InstructionEncodingTest() override {
    delete error_listener_;
    delete i_type_fmt_;
    delete encoding_info_;
    delete i_type_;
  }

  DecoderErrorListener *error_listener_;
  BinEncodingInfo *encoding_info_;
  Format *i_type_fmt_;
  InstructionEncoding *i_type_;
};

TEST_F(InstructionEncodingTest, Basic) {
  EXPECT_EQ(i_type_->name(), kITypeEncodingName);
  EXPECT_EQ(i_type_->GetValue(), 0);
  EXPECT_EQ(i_type_->GetMask(), 0);
  EXPECT_EQ(i_type_->GetCombinedMask(), 0);
  EXPECT_TRUE(i_type_->equal_constraints().empty());
  EXPECT_TRUE(i_type_->equal_extracted_constraints().empty());
  EXPECT_TRUE(i_type_->other_constraints().empty());
}

TEST_F(InstructionEncodingTest, BadConstraintName) {
  // Wrong field names.
  auto status = i_type_->AddEqualConstraint("NotAName", 0);
  // Verify error message, and that nothing has changed.
  EXPECT_TRUE(absl::IsNotFound(status));
  EXPECT_EQ(i_type_->GetValue(), 0);
  EXPECT_EQ(i_type_->GetMask(), 0);
  EXPECT_EQ(i_type_->GetCombinedMask(), 0);
  EXPECT_TRUE(i_type_->equal_constraints().empty());
  EXPECT_TRUE(i_type_->equal_extracted_constraints().empty());
  EXPECT_TRUE(i_type_->other_constraints().empty());

  // Check for other constraint type.
  status = i_type_->AddOtherConstraint(ConstraintType::kNe, "NotAName", 0);
  EXPECT_TRUE(absl::IsNotFound(status));
  EXPECT_EQ(i_type_->GetValue(), 0);
  EXPECT_EQ(i_type_->GetMask(), 0);
  EXPECT_EQ(i_type_->GetCombinedMask(), 0);
  EXPECT_TRUE(i_type_->equal_constraints().empty());
  EXPECT_TRUE(i_type_->equal_extracted_constraints().empty());
  EXPECT_TRUE(i_type_->other_constraints().empty());
}

TEST_F(InstructionEncodingTest, OutOfRangeUnsignedField) {
  // Correct field name, but value out of range.
  auto status = i_type_->AddEqualConstraint("func3", 8);
  EXPECT_TRUE(absl::IsOutOfRange(status));
  status = i_type_->AddEqualConstraint("func3", -5);
  EXPECT_TRUE(absl::IsOutOfRange(status));
  status = i_type_->AddOtherConstraint(ConstraintType::kLt, "func3", 8);
  EXPECT_TRUE(absl::IsOutOfRange(status));
  status = i_type_->AddOtherConstraint(ConstraintType::kLe, "func3", -5);
  EXPECT_TRUE(absl::IsOutOfRange(status));
}

TEST_F(InstructionEncodingTest, OutOfRangeSignedField) {
  auto status = i_type_->AddEqualConstraint("imm12", 1 << 11);
  EXPECT_TRUE(absl::IsOutOfRange(status));
  status = i_type_->AddEqualConstraint("imm12", -(1 << 11) - 1);
  EXPECT_TRUE(absl::IsOutOfRange(status));
  status = i_type_->AddOtherConstraint(ConstraintType::kNe, "imm12", 1 << 11);
  EXPECT_TRUE(absl::IsOutOfRange(status));
  status =
      i_type_->AddOtherConstraint(ConstraintType::kNe, "imm12", -(1 << 11) - 1);
  EXPECT_TRUE(absl::IsOutOfRange(status));
}

TEST_F(InstructionEncodingTest, OutOfRangeUnsignedOverlay) {
  // Correct field name, but value out of range.
  auto status = i_type_->AddEqualConstraint("uspecial", 1024);
  EXPECT_TRUE(absl::IsOutOfRange(status));
  status = i_type_->AddEqualConstraint("uspecial", -5);
  EXPECT_TRUE(absl::IsOutOfRange(status));
  status = i_type_->AddOtherConstraint(ConstraintType::kNe, "uspecial", 1024);
  EXPECT_TRUE(absl::IsOutOfRange(status));
  status = i_type_->AddOtherConstraint(ConstraintType::kNe, "uspecial", -5);
  EXPECT_TRUE(absl::IsOutOfRange(status));
}

TEST_F(InstructionEncodingTest, OutOfRangeSignedOverlay) {
  auto status = i_type_->AddEqualConstraint("sspecial", 1 << 10);
  EXPECT_TRUE(absl::IsOutOfRange(status));
  status = i_type_->AddEqualConstraint("sspecial", -(1 << 10) - 1);
  EXPECT_TRUE(absl::IsOutOfRange(status));
  status =
      i_type_->AddOtherConstraint(ConstraintType::kNe, "sspecial", 1 << 10);
  EXPECT_TRUE(absl::IsOutOfRange(status));
  status = i_type_->AddOtherConstraint(ConstraintType::kNe, "sspecial",
                                       -(1 << 10) - 1);
  EXPECT_TRUE(absl::IsOutOfRange(status));
}

TEST_F(InstructionEncodingTest, IllegalSignedConstraints) {
  // Field.
  auto status = i_type_->AddOtherConstraint(ConstraintType::kLt, "imm12", 5);
  EXPECT_TRUE(absl::IsInvalidArgument(status));
  status = i_type_->AddOtherConstraint(ConstraintType::kLe, "imm12", 5);
  EXPECT_TRUE(absl::IsInvalidArgument(status));
  status = i_type_->AddOtherConstraint(ConstraintType::kGt, "imm12", 5);
  EXPECT_TRUE(absl::IsInvalidArgument(status));
  status = i_type_->AddOtherConstraint(ConstraintType::kGe, "imm12", 5);
  EXPECT_TRUE(absl::IsInvalidArgument(status));
  // Overlay.
  status = i_type_->AddOtherConstraint(ConstraintType::kLt, "sspecial", 5);
  EXPECT_TRUE(absl::IsInvalidArgument(status));
  status = i_type_->AddOtherConstraint(ConstraintType::kLe, "sspecial", 5);
  EXPECT_TRUE(absl::IsInvalidArgument(status));
  status = i_type_->AddOtherConstraint(ConstraintType::kGt, "sspecial", 5);
  EXPECT_TRUE(absl::IsInvalidArgument(status));
  status = i_type_->AddOtherConstraint(ConstraintType::kGe, "sspecial", 5);
  EXPECT_TRUE(absl::IsInvalidArgument(status));
}

TEST_F(InstructionEncodingTest, AddEqualUnsignedConstraint) {
  auto status = i_type_->AddEqualConstraint("func3", kFunc3Value);
  EXPECT_TRUE(status.ok());
  EXPECT_EQ(i_type_->GetValue(), kFunc3Value << 12);
  EXPECT_EQ(i_type_->GetMask(), kFunc3Mask);
  EXPECT_EQ(i_type_->GetCombinedMask(), kFunc3Mask);
  EXPECT_EQ(i_type_->equal_constraints().size(), 1);
  auto *constraint = i_type_->equal_constraints()[0];
  EXPECT_EQ(constraint->type, ConstraintType::kEq);
  EXPECT_EQ(constraint->field, i_type_fmt_->GetField("func3"));
  EXPECT_EQ(constraint->value, kFunc3Value);
  EXPECT_EQ(constraint->overlay, nullptr);
  EXPECT_EQ(constraint->can_ignore, false);

  // Other constraints are unaffected.
  EXPECT_TRUE(i_type_->equal_extracted_constraints().empty());
  EXPECT_TRUE(i_type_->other_constraints().empty());
}

TEST_F(InstructionEncodingTest, AddOtherConstraints) {
  auto status =
      i_type_->AddOtherConstraint(ConstraintType::kGe, "func3", kFunc3Value);
  EXPECT_TRUE(status.ok());
  EXPECT_EQ(i_type_->GetValue(), 0);
  EXPECT_EQ(i_type_->GetMask(), 0);
  EXPECT_EQ(i_type_->GetCombinedMask(), kFunc3Mask);
  EXPECT_EQ(i_type_->other_constraints().size(), 1);
  auto *constraint = i_type_->other_constraints()[0];
  EXPECT_EQ(constraint->type, ConstraintType::kGe);
  EXPECT_EQ(constraint->field, i_type_fmt_->GetField("func3"));
  EXPECT_EQ(constraint->value, kFunc3Value);
  EXPECT_EQ(constraint->overlay, nullptr);
  EXPECT_EQ(constraint->can_ignore, false);

  // Other constraints are unaffected.
  EXPECT_TRUE(i_type_->equal_extracted_constraints().empty());
  EXPECT_TRUE(i_type_->equal_constraints().empty());
}

}  // namespace
