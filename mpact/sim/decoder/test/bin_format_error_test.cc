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

#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

#include "absl/log/check.h"
#include "absl/log/log_sink_registry.h"
#include "absl/strings/str_cat.h"
#include "googletest/include/gtest/gtest.h"
#include "mpact/sim/decoder/bin_format_visitor.h"
#include "mpact/sim/decoder/test/log_sink.h"

namespace {

using mpact::sim::test::LogSink;

constexpr char kTestUndeclaredOutputsDir[] = "TEST_UNDECLARED_OUTPUTS_DIR";

constexpr char kBinFmtSyntaxErrorBaseName[] = "syntax_error";
constexpr char kBinFmtFormatErrorBaseName[] = "format_error";
constexpr char kBinFmtFormatErrorUndefBaseName[] = "format_error_undef";

constexpr char kDecoderName[] = "ErrorTest";
constexpr char kBaseName[] = "error_test";

using ::mpact::sim::decoder::bin_format::BinFormatVisitor;

// The depot path to the test directory.
constexpr char kDepotPath[] = "mpact/sim/decoder/test/";

static bool FileExists(const std::string &name) {
  std::ifstream file(name);
  return file.good();
}

class BinFormatErrorTest : public testing::Test {
 protected:
  BinFormatErrorTest() {}

  std::vector<std::string> paths_;
};

TEST_F(BinFormatErrorTest, SyntaxError) {
  std::vector<std::string> input_files = {absl::StrCat(
      kDepotPath, "testfiles/", kBinFmtSyntaxErrorBaseName, ".bin_fmt")};

  ASSERT_TRUE(FileExists(input_files[0]));
  std::string output_dir = getenv(kTestUndeclaredOutputsDir);

  BinFormatVisitor visitor;
  LogSink log_sink;
  absl::AddLogSink(&log_sink);
  EXPECT_FALSE(
      visitor.Process(input_files, kDecoderName, kBaseName, paths_, output_dir)
          .ok());
  absl::RemoveLogSink(&log_sink);

  // Verify that there was an error message about mismatched input.
  auto ptr = log_sink.error_log().find("mismatched input '");
  EXPECT_TRUE(ptr != std::string::npos);
  ptr = log_sink.error_log().find(" expecting ");
  EXPECT_TRUE(ptr != std::string::npos);
}

TEST_F(BinFormatErrorTest, InstDefFormatError) {
  std::vector<std::string> input_files = {absl::StrCat(
      kDepotPath, "testfiles/", kBinFmtFormatErrorBaseName, ".bin_fmt")};

  ASSERT_TRUE(FileExists(input_files[0]));
  std::string output_dir = getenv(kTestUndeclaredOutputsDir);

  BinFormatVisitor visitor;
  LogSink log_sink;
  absl::AddLogSink(&log_sink);
  EXPECT_FALSE(
      visitor.Process(input_files, kDecoderName, kBaseName, paths_, output_dir)
          .ok());
  absl::RemoveLogSink(&log_sink);

  // There should be a number of error messages:
  //   * Multiple definitions of format 'ZFormat'.
  //   * Multiple definitions of instruction group 'RiscVGInst32'.
  //   * Multiple definitions of decoder 'ErrorTest'.
  //   * Field 'rs2' already defined.
  //   * Overlay 'imm7' already defined as a field.
  //   * Overlay 'b_imm' reference to 'immX' does not name a field in 'BType'.
  //   * Overlay 'b_imm' declared width (13) differs from computed width (9).
  //   * Overlay 'overlay0' already defined as an overlay.
  //   * Format 'BType' declared width (32) differs from declared width (27).
  //   * YFormat being of different width than the format it inherits from.
  //   * XFormat not deriving from Inst32Format.

  auto ptr = log_sink.error_log().find(
      "Error: Multiple definitions of format 'ZFormat' first defined at ");
  EXPECT_TRUE(ptr != std::string::npos);
  ptr = log_sink.error_log().find(
      "Error: Multiple definitions of instruction group 'RiscVGInst32' first "
      "defined at");
  EXPECT_TRUE(ptr != std::string::npos);
  ptr = log_sink.error_log().find(
      "Error: Multiple definitions of decoder 'ErrorTest' first defined at");
  EXPECT_TRUE(ptr != std::string::npos);
  ptr = log_sink.error_log().find("Error: Field 'rs2' already defined");
  EXPECT_TRUE(ptr != std::string::npos);
  ptr = log_sink.error_log().find(
      "Error: Overlay 'imm7' already defined as a "
      "field");
  EXPECT_TRUE(ptr != std::string::npos);
  ptr = log_sink.error_log().find(
      "Error: Overlay 'b_imm' reference to 'immX' does not name a field in "
      "'BType'");
  EXPECT_TRUE(ptr != std::string::npos);
  ptr = log_sink.error_log().find(
      "Error: Overlay 'b_imm' declared width (13) differs from computed width "
      "(9)");
  ptr = log_sink.error_log().find(
      "Error: Overlay 'overlay0' already defined as an "
      "overlay");
  EXPECT_TRUE(ptr != std::string::npos);
  ptr = log_sink.error_log().find(
      "Error: Format 'BType' declared width (32) differs from computed width "
      "(27)");
  EXPECT_TRUE(ptr != std::string::npos);
  ptr = log_sink.error_log().find(
      "Error: Format 'XFormat' used by instruction encoding 'none_0' is not "
      "derived from 'Inst32Format'");
  EXPECT_TRUE(ptr != std::string::npos);
  ptr = log_sink.error_log().find(
      "Error: Format 'YFormat' declared width (36) differs from width "
      "inherited from 'Inst32Format' (32)");
  EXPECT_TRUE(ptr != std::string::npos);
}

TEST_F(BinFormatErrorTest, InstDefFormatErrorUndef) {
  std::vector<std::string> input_files = {absl::StrCat(
      kDepotPath, "testfiles/", kBinFmtFormatErrorUndefBaseName, ".bin_fmt")};

  ASSERT_TRUE(FileExists(input_files[0]));
  std::string output_dir = getenv(kTestUndeclaredOutputsDir);

  BinFormatVisitor visitor;
  LogSink log_sink;
  absl::AddLogSink(&log_sink);
  EXPECT_FALSE(
      visitor.Process(input_files, kDecoderName, kBaseName, paths_, output_dir)
          .ok());
  absl::RemoveLogSink(&log_sink);
  auto ptr = log_sink.error_log().find(
      "Error: Undefined format 'NoneSuch' used by instruction group "
      "'RiscVGInst32'");
  EXPECT_TRUE(ptr != std::string::npos);
}

}  // namespace
