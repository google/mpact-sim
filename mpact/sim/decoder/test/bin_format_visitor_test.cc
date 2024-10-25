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

constexpr char kBinFmtSyntaxErrorBaseName[] = "syntax_error";
constexpr char kBinFmtFormatErrorBaseName[] = "format_error";
constexpr char kBinFmtFormatErrorUndefBaseName[] = "format_error_undef";
constexpr char kConstraintsBaseName[] = "constraints";
constexpr char kConstraintsDecoderName[] = "Constraints";
constexpr char kDecoderName[] = "ErrorTest";
constexpr char kBaseName[] = "error_test";
constexpr char kEmptyDecoderName[] = "Empty";
constexpr char kEmptyBaseName[] = "empty_file";
constexpr char kRiscVDecoderName[] = "RiscV32G";
constexpr char kRiscVBaseName[] = "riscv32";
constexpr char kRiscVTopName[] = "riscv32_top.bin_fmt";
constexpr char kRiscV32GName[] = "riscv32g.bin_fmt";
constexpr char kRiscV32CName[] = "riscv32c.bin_fmt";
constexpr char kGeneratorBaseName[] = "generator";
constexpr char kGeneratorDecoderName[] = "Generator";
constexpr char kVliwBaseName[] = "vliw";
constexpr char kVliwDecoderName[] = "Vliw24";
constexpr char kInstructionGroupBaseName[] = "instruction_group";
constexpr char kInstructionGroupDecoderName[] = "InstructionGroup";
constexpr char kRecursiveExampleBaseName[] = "example_with_recursive_include";
constexpr char kInstructionGroupErrorsBaseName[] = "instruction_group_errors";
constexpr char kInstructionGroupErrorsDecoderName[] = "InstructionGroupErrors";

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

class BinFormatVisitorTest : public testing::Test {
 protected:
  BinFormatVisitorTest() {}

  std::vector<std::string> paths_;
};

TEST_F(BinFormatVisitorTest, NullFileParsing) {
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

// Make sure recursive includes cause a failure.
TEST_F(BinFormatVisitorTest, RecursiveInclude) {
  // Set up input and output file paths.
  std::vector<std::string> input_files = {absl::StrCat(
      kDepotPath, "testfiles/", kRecursiveExampleBaseName, ".bin_fmt")};
  ASSERT_TRUE(FileExists(input_files[0]));
  std::string output_dir = OutputDir();
  // Parse and process the input file, capturing the log.
  LogSink log_sink;
  absl::AddLogSink(&log_sink);
  BinFormatVisitor visitor;
  auto success = visitor
                     .Process(input_files, kRiscVDecoderName,
                              kRecursiveExampleBaseName, paths_, output_dir)
                     .ok();
  absl::RemoveLogSink(&log_sink);
  EXPECT_FALSE(success);

  // Make sure the requisite error messageßs are present.
  auto ptr = log_sink.error_log().find("Error: Recursive include of '");
  EXPECT_TRUE(ptr != std::string::npos);
}

TEST_F(BinFormatVisitorTest, BasicParsing) {
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

// This tests the generator using non-tuple values.
TEST_F(BinFormatVisitorTest, SimpleGenerator) {
  // Create the file.
  std::string file_name =
      testing::TempDir() + "/" + "duplicate_variable.bin_fmt";
  std::ofstream output_file(file_name, std::ofstream::out);
  CHECK(output_file.good());
  output_file << kBinFmtPrefix;
  output_file << R"mid(
  GENERATE( btype = [0, 1, 2, 3, 4, "5", "6"]) {
    b$(btype) : BType : func3 == $(btype), opcode == 0b110'0011;
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
  EXPECT_TRUE(success);

  // Verify that the decoder files _bin_decoder.{.h,.cc} files were generated.
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
  for (auto name : {"kB0", "kB1", "kB2", "kB3", "kB4", "kB5", "kB6"}) {
    RE2 re(name);
    EXPECT_TRUE(RE2::PartialMatch(decoder_str, re)) << name;
  }
}

TEST_F(BinFormatVisitorTest, Generator) {
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
  // Verify that the decoder files _bin_decoder.{.h,.cc} files were generated.
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

TEST_F(BinFormatVisitorTest, GeneratorErrorDuplicateVariable) {
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

TEST_F(BinFormatVisitorTest, GeneratorErrorTuplesError) {
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

TEST_F(BinFormatVisitorTest, GeneratorErrorUndefinedBindingVariable) {
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

  // Make sure the error about duplicate binding variable 'funcX' is found.
  auto ptr = log_sink.error_log().find("Undefined binding variable 'funcX'");
  EXPECT_TRUE(ptr != std::string::npos);
}

TEST_F(BinFormatVisitorTest, Vliw) {
  // Set up input and output file paths.
  std::vector<std::string> input_files = {
      absl::StrCat(kDepotPath, "testfiles/", kVliwBaseName, ".bin_fmt")};
  ASSERT_TRUE(FileExists(input_files[0]));
  std::string output_dir = getenv(kTestUndeclaredOutputsDir);
  BinFormatVisitor visitor;
  // Parse and process the input file, capturing the log.
  LogSink log_sink;
  absl::AddLogSink(&log_sink);
  auto success = visitor
                     .Process(input_files, kVliwDecoderName, kVliwBaseName,
                              paths_, output_dir)
                     .ok();
  absl::RemoveLogSink(&log_sink);
  EXPECT_TRUE(success);

  // Verify that the extraction functions for the vliw format were generated.
  std::ifstream decoder_file(
      absl::StrCat(output_dir, "/", kVliwBaseName, "_bin_decoder.h"));
  CHECK(decoder_file.good());
  // Verify that decoder entries include extraction functions for the vliw
  // format.
  std::string decoder_str((std::istreambuf_iterator<char>(decoder_file)),
                          (std::istreambuf_iterator<char>()));
  for (auto name : {"ExtractI0", "ExtractI1", "ExtractI2"}) {
    RE2 re(name);
    EXPECT_TRUE(RE2::PartialMatch(decoder_str, re)) << name;
  }
}

TEST_F(BinFormatVisitorTest, InstructionGroupGrouping) {
  // Set up input and output file paths.
  std::vector<std::string> input_files = {absl::StrCat(
      kDepotPath, "testfiles/", kInstructionGroupBaseName, ".bin_fmt")};
  ASSERT_TRUE(FileExists(input_files[0]));
  std::string output_dir = getenv(kTestUndeclaredOutputsDir);
  BinFormatVisitor visitor;
  // Parse and process the input file, capturing the log.
  LogSink log_sink;
  absl::AddLogSink(&log_sink);
  auto success = visitor
                     .Process(input_files, kInstructionGroupDecoderName,
                              kInstructionGroupBaseName, paths_, output_dir)
                     .ok();
  absl::RemoveLogSink(&log_sink);
  EXPECT_TRUE(success);
}

TEST_F(BinFormatVisitorTest, SyntaxError) {
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

TEST_F(BinFormatVisitorTest, InstDefFormatError) {
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
      "derived from");
  EXPECT_TRUE(ptr != std::string::npos);
  ptr = log_sink.error_log().find(
      "Error: Format 'YFormat' declared width (36) differs from width "
      "inherited from 'Inst32Format' (32)");
  EXPECT_TRUE(ptr != std::string::npos);
  ptr = log_sink.error_log().find(
      "Error: Only overlays <= 64 bits are supported for now");
  EXPECT_TRUE(ptr != std::string::npos);
  ptr = log_sink.error_log().find(
      "Error: Instruction group 'X': width must be <= 64 bits");
  EXPECT_TRUE(ptr != std::string::npos);
  ptr = log_sink.error_log().find(
      "Error: Width of format 'Inst32Format' (32) differs from the declared "
      "width of instruction group 'Z' (34)");
  EXPECT_TRUE(ptr != std::string::npos);
  ptr = log_sink.error_log().find(
      "Error: Length of format 'Format33' (33) differs from the declared width "
      "of the instruction group (32)");
  EXPECT_TRUE(ptr != std::string::npos);
  ptr = log_sink.error_log().find(
      "Error: Format 'None' referenced by instruction 'none_2' not defined");
  EXPECT_TRUE(ptr != std::string::npos);
  ptr = log_sink.error_log().find("Error: ZFormat: illegal use of format name");
  EXPECT_TRUE(ptr != std::string::npos);
  ptr =
      log_sink.error_log().find("Error: More than one opcode enum declaration");
  EXPECT_TRUE(ptr != std::string::npos);
  ptr = log_sink.error_log().find(
      "Error: Instruction group 'RiscVGInst32' listed twice");
  EXPECT_TRUE(ptr != std::string::npos);
  ptr = log_sink.error_log().find("Error: More than one namespace declaration");
  EXPECT_TRUE(ptr != std::string::npos);
  ptr = log_sink.error_log().find("Error: Parent format 'TypeX' not defined");
  EXPECT_TRUE(ptr != std::string::npos);
  ptr = log_sink.error_log().find(
      "Error: Format 'TypeA': must specify a width or inherited format");
  EXPECT_TRUE(ptr != std::string::npos);
}

TEST_F(BinFormatVisitorTest, InstDefFormatErrorUndef) {
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

TEST_F(BinFormatVisitorTest, Constraints) {
  std::vector<std::string> input_files = {
      absl::StrCat(kDepotPath, "testfiles/", kConstraintsBaseName, ".bin_fmt")};

  ASSERT_TRUE(FileExists(input_files[0]));
  std::string output_dir = getenv(kTestUndeclaredOutputsDir);

  BinFormatVisitor visitor;
  LogSink log_sink;
  absl::AddLogSink(&log_sink);
  EXPECT_TRUE(visitor
                  .Process(input_files, kConstraintsDecoderName,
                           kConstraintsBaseName, paths_, output_dir)
                  .ok());
  absl::RemoveLogSink(&log_sink);

  // Verify that the decoder files _bin_decoder.{.h,.cc} files were generated.
  EXPECT_TRUE(FileExists(
      absl::StrCat(output_dir, "/", kConstraintsBaseName, "_bin_decoder.h")));
  EXPECT_TRUE(FileExists(
      absl::StrCat(output_dir, "/", kConstraintsBaseName, "_bin_decoder.cc")));

  std::ifstream decoder_file(
      absl::StrCat(output_dir, "/", kConstraintsBaseName, "_bin_decoder.cc"));
  CHECK(decoder_file.good());
  // Verify that decoder entries use the different constraint types.
  std::string decoder_str((std::istreambuf_iterator<char>(decoder_file)),
                          (std::istreambuf_iterator<char>()));
  for (auto const &fragment :
       {"field1_value != 0x1", "field2_value > 0x2", "field3_value >= 0x3",
        "field4_value < 0x4", "field5_value <= 0x5"}) {
    RE2 re(fragment);
    EXPECT_TRUE(RE2::PartialMatch(decoder_str, re)) << fragment;
  }
}

TEST_F(BinFormatVisitorTest, InstructionGroupErrors) {
  std::vector<std::string> input_files = {absl::StrCat(
      kDepotPath, "testfiles/", kInstructionGroupErrorsBaseName, ".bin_fmt")};

  ASSERT_TRUE(FileExists(input_files[0]));
  std::string output_dir = getenv(kTestUndeclaredOutputsDir);

  BinFormatVisitor visitor;
  LogSink log_sink;
  absl::AddLogSink(&log_sink);
  EXPECT_FALSE(visitor
                   .Process(input_files, kInstructionGroupErrorsDecoderName,
                            kInstructionGroupErrorsBaseName, paths_, output_dir)
                   .ok());
  absl::RemoveLogSink(&log_sink);
  auto ptr = log_sink.error_log().find(
      "Error: No such instruction group: 'InstGroup'");
  EXPECT_TRUE(ptr != std::string::npos);
  ptr = log_sink.error_log().find(
      "Error: Instruction group added twice: 'inst32a' - ignored");
  EXPECT_TRUE(ptr != std::string::npos);
  ptr = log_sink.error_log().find(
      "Error: Instruction group 'InstGroup2' listed twice");
  EXPECT_TRUE(ptr != std::string::npos);
  ptr = log_sink.error_log().find(
      "Error: Instruction group added twice: 'inst32c' - ignored");
  EXPECT_TRUE(ptr != std::string::npos);
  ptr =
      log_sink.error_log().find("Error: Instruction group 'inst32d' not found");
  EXPECT_TRUE(ptr != std::string::npos);
  ptr = log_sink.error_log().find(
      "Error: Instruction group 'inst32b' must use format 'Inst32Format, to be "
      "merged into group 'InstGroup1'");
  EXPECT_TRUE(ptr != std::string::npos);
  ptr = log_sink.error_log().find("Error: No child groups");
  EXPECT_TRUE(ptr != std::string::npos);
}

}  // namespace
