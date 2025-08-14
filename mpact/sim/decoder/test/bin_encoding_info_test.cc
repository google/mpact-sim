#include "mpact/sim/decoder/bin_encoding_info.h"

#include "absl/log/check.h"
#include "absl/status/status.h"
#include "googletest/include/gtest/gtest.h"
#include "mpact/sim/decoder/decoder_error_listener.h"
#include "mpact/sim/decoder/format.h"
#include "mpact/sim/decoder/instruction_group.h"

// This file provides for testing of the BinEncodingInfo class interfaces, with
// the exception of PropagateExtractors, as that cannot be tested in isolation.

namespace {

using ::mpact::sim::decoder::DecoderErrorListener;
using ::mpact::sim::decoder::bin_format::BinEncodingInfo;

constexpr char kOpcodeEnumName[] = "OpcodeEnumName";
constexpr char kIncludeFile0[] = "IncludeFile0";
constexpr char kIncludeFile1[] = "IncludeFile1";
constexpr char kIncludeFile2[] = "IncludeFile2";
constexpr const char* kIncludeFiles[] = {kIncludeFile0, kIncludeFile1,
                                         kIncludeFile2};
constexpr char kFormat0[] = "Format0";
constexpr char kFormat1[] = "Format1";
constexpr char kFormat2[] = "Format2";
constexpr char kGroup0[] = "Group0";
constexpr char kBinDecoder[] = "BinDecoder";
constexpr int kFormatWidth32 = 32;
constexpr int kFormatWidth16 = 16;

class BinEncodingInfoTest : public ::testing::Test {
 protected:
  BinEncodingInfoTest() {
    error_listener_ = new DecoderErrorListener();
    bin_encoding_info_ = new BinEncodingInfo(kOpcodeEnumName, error_listener_);
  }
  ~BinEncodingInfoTest() override {
    delete bin_encoding_info_;
    delete error_listener_;
  }

  DecoderErrorListener* error_listener_ = nullptr;
  BinEncodingInfo* bin_encoding_info_ = nullptr;
};

// Test for proper initialization of object.
TEST_F(BinEncodingInfoTest, Constructed) {
  EXPECT_FALSE(error_listener_->HasError());
  EXPECT_TRUE(bin_encoding_info_->format_map().empty());
  EXPECT_TRUE(bin_encoding_info_->include_files().empty());
  EXPECT_TRUE(bin_encoding_info_->instruction_group_map().empty());
  EXPECT_EQ(bin_encoding_info_->decoder(), nullptr);
  EXPECT_EQ(bin_encoding_info_->error_listener(), error_listener_);
}

// Test that include files are properly added/kept.
TEST_F(BinEncodingInfoTest, AddIncludeFile) {
  EXPECT_TRUE(bin_encoding_info_->include_files().empty());
  for (auto const& include_file : kIncludeFiles) {
    bin_encoding_info_->AddIncludeFile(include_file);
  }
  EXPECT_FALSE(bin_encoding_info_->include_files().empty());
  for (auto const& include_file : kIncludeFiles) {
    EXPECT_TRUE(bin_encoding_info_->include_files().contains(include_file));
  }
  EXPECT_FALSE(bin_encoding_info_->include_files().contains("NoIncludeFile"));
}

// Test that formats are properly added/kept.
TEST_F(BinEncodingInfoTest, AddFormat) {
  // Adding a new format should work.
  auto res0 = bin_encoding_info_->AddFormat(kFormat0, kFormatWidth32);
  EXPECT_TRUE(res0.status().ok());
  auto* format = res0.value();
  EXPECT_EQ(format->name(), kFormat0);
  EXPECT_EQ(format->declared_width(), kFormatWidth32);

  // Make sure we get the format back when calling GetFormat.
  auto* get_format = bin_encoding_info_->GetFormat(kFormat0);
  EXPECT_EQ(get_format, format);
  // Adding the same format again should fail.
  res0 = bin_encoding_info_->AddFormat(kFormat0, kFormatWidth32);
  EXPECT_EQ(absl::StatusCode::kAlreadyExists, res0.status().code());

  // Add a different format should work.
  auto res1 = bin_encoding_info_->AddFormat(kFormat1, kFormatWidth16);
  EXPECT_TRUE(res1.status().ok());
  format = res1.value();
  EXPECT_EQ(format->name(), kFormat1);
  EXPECT_EQ(format->declared_width(), kFormatWidth16);

  // Add format with parent.
  auto res2 = bin_encoding_info_->AddFormat(kFormat2, kFormatWidth32, kFormat0);
  EXPECT_TRUE(res2.status().ok());
  format = res2.value();
  EXPECT_EQ(format->name(), kFormat2);
  EXPECT_EQ(format->declared_width(), kFormatWidth32);

  // Can't add the same format twice.
  res2 = bin_encoding_info_->AddFormat(kFormat2, kFormatWidth32, kFormat0);
  EXPECT_EQ(absl::StatusCode::kAlreadyExists, res2.status().code());

  // Format map. Verify that the formats are in the map.
  auto& format_map = bin_encoding_info_->format_map();
  EXPECT_EQ(format_map.size(), 3);
  EXPECT_NE(format_map.find(kFormat0), format_map.end());
  EXPECT_NE(format_map.find(kFormat1), format_map.end());
  EXPECT_NE(format_map.find(kFormat2), format_map.end());
}

// Instruction groups.
TEST_F(BinEncodingInfoTest, AddInstructionGroup) {
  EXPECT_TRUE(bin_encoding_info_->instruction_group_map().empty());

  // Add an instruction group.
  auto res = bin_encoding_info_->AddInstructionGroup(kGroup0, kFormatWidth32,
                                                     kFormat0);
  EXPECT_TRUE(res.status().ok());
  auto* instruction_group = res.value();
  EXPECT_EQ(instruction_group->name(), kGroup0);
  EXPECT_EQ(instruction_group->width(), kFormatWidth32);
  EXPECT_EQ(instruction_group->format_name(), kFormat0);
  EXPECT_EQ(instruction_group->opcode_enum(), kOpcodeEnumName);

  // Adding it a second time doesn't work.
  res = bin_encoding_info_->AddInstructionGroup(kGroup0, kFormatWidth32,
                                                kFormat0);
  EXPECT_EQ(absl::StatusCode::kAlreadyExists, res.status().code());
}

// Bin decoder.
TEST_F(BinEncodingInfoTest, AddDecoder) {
  // Add BinDecoder.
  auto* bin_dec = bin_encoding_info_->AddBinDecoder(kBinDecoder);
  EXPECT_NE(bin_dec, nullptr);
  EXPECT_FALSE(error_listener_->HasError());
  // Try adding it again.
  bin_dec = bin_encoding_info_->AddBinDecoder(kBinDecoder);
  EXPECT_EQ(bin_dec, nullptr);
  EXPECT_TRUE(error_listener_->HasError());
}

}  // namespace
