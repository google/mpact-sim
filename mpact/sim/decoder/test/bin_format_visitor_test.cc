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

#include "mpact/sim/decoder/bin_format_visitor.h"

#include <string>

#include "absl/strings/str_cat.h"
#include "googletest/include/gtest/gtest.h"

namespace {

constexpr char kTestUndeclaredOutputsDir[] = "TEST_UNDECLARED_OUTPUTS_DIR";

constexpr char kEmptyDecoderName[] = "Empty";
constexpr char kEmptyBaseName[] = "empty_file";
constexpr char kRiscVDecoderName[] = "RiscV32G";
constexpr char kRiscVBaseName[] = "riscv32";
constexpr char kRiscVTopName[] = "riscv32_top.bin_fmt";
constexpr char kRiscV32GName[] = "riscv32g.bin_fmt";
constexpr char kRiscV32CName[] = "riscv32c.bin_fmt";

using ::mpact::sim::decoder::bin_format::BinFormatVisitor;

// The depot path to the test directory.
constexpr char kDepotPath[] = "mpact/sim/decoder/test/";

std::string OutputDir() { return "./"; }

static bool FileExists(const std::string &name) {
  std::ifstream file(name);
  return file.good();
}

class BinFormatParserTest : public testing::Test {
 protected:
  BinFormatParserTest() {}

  std::vector<std::string> paths_;
};

TEST_F(BinFormatParserTest, NullFileParsing) {
  // Set up input and output file paths.
  std::vector<std::string> input_files = {
      absl::StrCat(kDepotPath, "testfiles/", kEmptyBaseName, ".bin_fmt")};
  ASSERT_TRUE(FileExists(input_files[0]));
  std::string output_dir = OutputDir();

  BinFormatVisitor visitor;
  // Parse and process the input file.
  EXPECT_FALSE(visitor
                   .Process(input_files, kEmptyBaseName, kEmptyDecoderName,
                            paths_, output_dir)
                   .ok());
}

TEST_F(BinFormatParserTest, BasicParsing) {
  // Make sure the visitor can read and parse the input file.
  // Set up input and output file paths.
  std::vector<std::string> input_files = {
      absl::StrCat(kDepotPath, "testfiles/", kRiscVTopName),
      absl::StrCat(kDepotPath, "testfiles/", kRiscV32GName),
      absl::StrCat(kDepotPath, "testfiles/", kRiscV32CName),
  };
  ASSERT_TRUE(FileExists(input_files[0]));
  ASSERT_TRUE(FileExists(input_files[1]));
  ASSERT_TRUE(FileExists(input_files[2]));
  std::string output_dir = getenv(kTestUndeclaredOutputsDir);

  BinFormatVisitor visitor;
  // Parse and process the input file.
  EXPECT_TRUE(visitor
                  .Process(input_files, kRiscVDecoderName, kRiscVBaseName,
                           paths_, output_dir)
                  .ok());
  // Verify that the decoder files _decoder.{.h,.cc} files were generated.
  EXPECT_TRUE(FileExists(
      absl::StrCat(output_dir, "/", kRiscVBaseName, "_bin_decoder.h")));

  EXPECT_TRUE(FileExists(
      absl::StrCat(output_dir, "/", kRiscVBaseName, "_bin_decoder.cc")));
}

}  // namespace
