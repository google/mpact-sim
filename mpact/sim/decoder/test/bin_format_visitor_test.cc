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

#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "absl/log/check.h"
#include "absl/log/log_sink_registry.h"
#include "absl/strings/str_cat.h"
#include "googletest/include/gtest/gtest.h"
#include "mpact/sim/decoder/test/log_sink.h"
#include "re2/re2.h"

namespace {

using mpact::sim::test::LogSink;

constexpr char kTestUndeclaredOutputsDir[] = "TEST_UNDECLARED_OUTPUTS_DIR";

constexpr char kEmptyDecoderName[] = "Empty";
constexpr char kEmptyBaseName[] = "empty_file";
constexpr char kRiscVDecoderName[] = "RiscV32G";
constexpr char kRiscVBaseName[] = "riscv32";
constexpr char kRiscVTopName[] = "riscv32_top.bin_fmt";
constexpr char kRiscV32GName[] = "riscv32g.bin_fmt";
constexpr char kRiscV32CName[] = "riscv32c.bin_fmt";
constexpr char kGeneratorBaseName[] = "generator";
constexpr char kGeneratorDecoderName[] = "Generator";

using ::mpact::sim::decoder::bin_format::BinFormatVisitor;

constexpr char kBinFmtPrefix[] = R"pre(
decoder Generator {
  namespace sim::generator::encoding;
  opcode_enum = "isa::OpcodeEnum";
  RiscVGInst32;
};

format Inst32Format[32] {
  fields:
    unsigned bits[25];
    unsigned opcode[7];
};

format BType[32] : Inst32Format {
  fields:
    unsigned imm7[7];
    unsigned rs2[5];
    unsigned rs1[5];
    unsigned func3[3];
    unsigned imm5[5];
    unsigned opcode[7];
  overlays:
    signed b_imm[13] = imm7[6], imm5[0], imm7[5..0], imm5[4..1], 0b0;
};

instruction group RiscVGInst32[32] : Inst32Format {
)pre";

constexpr char kBinFmtSuffix[] = R"post(
};
)post";

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

TEST_F(BinFormatParserTest, Generator) {
  std::string input_file =
      absl::StrCat(kDepotPath, "testfiles/", kGeneratorBaseName, ".bin_fmt");
  ASSERT_TRUE(FileExists(input_file));
  std::string output_dir = getenv(kTestUndeclaredOutputsDir);

  BinFormatVisitor visitor;
  // Parse and process the input file, capturing the log.
  LogSink log_sink;
  absl::AddLogSink(&log_sink);
  auto success = visitor
                     .Process({input_file}, kGeneratorDecoderName,
                              kGeneratorBaseName, paths_, output_dir)
                     .ok();
  absl::RemoveLogSink(&log_sink);
  EXPECT_TRUE(success);
  // Make sure the warning about unreferenced binding variable is found.
  auto ptr =
      log_sink.warning_log().find("Unreferenced binding variable 'unused'");
  EXPECT_TRUE(ptr != std::string::npos);
  // Verify that the decoder files _bom+decoder.{.h,.cc} files were generated.
  EXPECT_TRUE(FileExists(
      absl::StrCat(output_dir, "/", kGeneratorBaseName, "_bin_decoder.h")));
  EXPECT_TRUE(FileExists(
      absl::StrCat(output_dir, "/", kGeneratorBaseName, "_bin_decoder.cc")));

  std::ifstream decoder_file(
      absl::StrCat(output_dir, "/", kGeneratorBaseName, "_bin_decoder.cc"));
  CHECK(decoder_file.good());
  // Verify that decoder entries include the instructions inside the GENERATE()
  // construct.
  std::string decoder_str((std::istreambuf_iterator<char>(decoder_file)),
                          (std::istreambuf_iterator<char>()));
  for (auto name : {"kBeq", "kBne", "kBlt", "kBltu", "kBge", "kBgeu"}) {
    RE2 re(name);
    EXPECT_TRUE(RE2::PartialMatch(decoder_str, re)) << name;
  }
}

TEST_F(BinFormatParserTest, GeneratorErrorDuplicateVariable) {
  // Create the file.
  std::string file_name =
      testing::TempDir() + "/" + "duplicate_variable.bin_fmt";
  std::ofstream output_file(file_name, std::ofstream::out);
  CHECK(output_file.good());
  output_file << kBinFmtPrefix;
  // The GENERATE block reuses 'btype' in the range specification.
  output_file << R"mid(
  GENERATE( [ btype, func3, btype] = [{"eq", 0b000, 1}, {"ne", 0b001, 2},
      {lt, 0b100, 3}, {ge, 0b101, 4}, {ltu, 0b110, 5}, {geu, 0b111, 6}]) {
    b$(btype) : BType : func3 == $(func3), opcode == 0b110'0011;
  };
)mid";
  output_file << kBinFmtSuffix;
  output_file.close();

  std::vector<std::string> input_files = {file_name};
  ASSERT_TRUE(FileExists(input_files[0]));
  std::string output_dir = getenv(kTestUndeclaredOutputsDir);
  BinFormatVisitor visitor;
  // Parse and process the input file, capturing the log.
  LogSink log_sink;
  absl::AddLogSink(&log_sink);
  auto success = visitor
                     .Process(input_files, kGeneratorDecoderName,
                              kGeneratorBaseName, paths_, output_dir)
                     .ok();
  absl::RemoveLogSink(&log_sink);
  EXPECT_FALSE(success);

  // Make sure the error about duplicate binding variable 'btype' is found.
  auto ptr =
      log_sink.error_log().find("Duplicate binding variable name 'btype'");
  EXPECT_TRUE(ptr != std::string::npos);
}

TEST_F(BinFormatParserTest, GeneratorErrorTuplesError) {
  // Create the file.
  std::string file_name = testing::TempDir() + "/" + "tuples_required.bin_fmt";
  std::ofstream output_file(file_name, std::ofstream::out);
  CHECK(output_file.good());
  output_file << kBinFmtPrefix;
  // The GENERATE block adds an extra value in the first tuple.
  output_file << R"mid(

  GENERATE( [ btype, func3, unused] = [{"eq", 0b000, 1, 3}, {"ne", 0b001, 2},
      {lt, 0b100, 3}, {ge, 0b101, 4}, {ltu, 0b110, 5}, {geu, 0b111, 6}]) {
    b$(btype) : BType : func3 == $(func3), opcode == 0b110'0011;
  };
)mid";
  output_file << kBinFmtSuffix;
  output_file.close();

  std::vector<std::string> input_files = {file_name};
  ASSERT_TRUE(FileExists(input_files[0]));
  std::string output_dir = getenv(kTestUndeclaredOutputsDir);
  BinFormatVisitor visitor;
  // Parse and process the input file, capturing the log.
  LogSink log_sink;
  absl::AddLogSink(&log_sink);
  auto success = visitor
                     .Process(input_files, kGeneratorDecoderName,
                              kGeneratorBaseName, paths_, output_dir)
                     .ok();
  absl::RemoveLogSink(&log_sink);
  EXPECT_FALSE(success);

  // Make sure the error about duplicate binding variable 'btype' is found.
  auto ptr = log_sink.error_log().find(
      "Number of values differs from number of identifiers");
  EXPECT_TRUE(ptr != std::string::npos);
}

TEST_F(BinFormatParserTest, GeneratorErrorUndefinedBindingVariable) {
  // Create the file.
  std::string file_name =
      testing::TempDir() + "/" + "undefined_variable.bin_fmt";
  std::ofstream output_file(file_name, std::ofstream::out);
  CHECK(output_file.good());
  output_file << kBinFmtPrefix;
  // The GENERATE block references 'funcX'.
  output_file << R"mid(
  GENERATE( [ btype, func3] = [{"eq", 0b000}, {"ne", 0b001},
      {lt, 0b100}, {ge, 0b101}, {ltu, 0b110}, {geu, 0b111}]) {
    b$(btype) : BType : func3 == $(funcX), opcode == 0b110'0011;
  };
)mid";
  output_file << kBinFmtSuffix;
  output_file.close();

  std::vector<std::string> input_files = {file_name};
  ASSERT_TRUE(FileExists(input_files[0]));
  std::string output_dir = getenv(kTestUndeclaredOutputsDir);
  BinFormatVisitor visitor;
  // Parse and process the input file, capturing the log.
  LogSink log_sink;
  absl::AddLogSink(&log_sink);
  auto success = visitor
                     .Process(input_files, kGeneratorDecoderName,
                              kGeneratorBaseName, paths_, output_dir)
                     .ok();
  absl::RemoveLogSink(&log_sink);
  EXPECT_FALSE(success);

  // Make sure the error about duplicate binding variable 'btype' is found.
  auto ptr = log_sink.error_log().find("Undefined binding variable 'funcX'");
  EXPECT_TRUE(ptr != std::string::npos);
}

}  // namespace
