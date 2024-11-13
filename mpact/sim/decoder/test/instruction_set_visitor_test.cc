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

#include "mpact/sim/decoder/instruction_set_visitor.h"

#include <cstdlib>
#include <fstream>
#include <istream>
#include <iterator>
#include <string>
#include <vector>

#include "absl/log/check.h"
#include "absl/log/log_sink_registry.h"
#include "absl/strings/str_cat.h"
#include "googlemock/include/gmock/gmock.h"
#include "googletest/include/gtest/gtest.h"
#include "mpact/sim/decoder/test/log_sink.h"
#include "re2/re2.h"

namespace mpact {
namespace sim {
namespace machine_description {
namespace instruction_set {
namespace {

using mpact::sim::test::LogSink;

constexpr char kTestUndeclaredOutputsDir[] = "TEST_UNDECLARED_OUTPUTS_DIR";

constexpr char kExampleBaseName[] = "example";
constexpr char kRecursiveExampleBaseName[] = "example_with_recursive_include";
constexpr char kEmptyBaseName[] = "empty_file";
constexpr char kGeneratorBaseName[] = "generator";
constexpr char kUndefinedErrorsBaseName[] = "undefined_errors";
constexpr char kDisasmFormatsBaseName[] = "disasm_formats";
constexpr char kResourceBaseName[] = "resource";

constexpr char kEmptyIsaName[] = "Empty";
constexpr char kExampleIsaName[] = "Example";
constexpr char kGeneratorIsaName[] = "Generator";
constexpr char kUndefinedErrorsIsaName[] = "UndefinedErrors";
constexpr char kDisasmFormatsIsaName[] = "DisasmFormats";
constexpr char kResourceIsaName[] = "Resource";

// The depot path to the test directory.
constexpr char kDepotPath[] = "mpact/sim/decoder/test";

// Include file to add on command line.
constexpr char kBundleBFile[] = "mpact/sim/decoder/test/testfiles/bundle_b.isa";

// isa file fragments.
constexpr char kIsaPrefix[] = R"pre(disasm widths = {-18};

isa Generator {
  namespace sim::generator::isa;
  slots { branches; }
}

slot branches {
  default size = 4;
  default latency = 0;
  default opcode =
    disasm: "Illegal instruction at 0x%(@:08x)",
    semfunc: "[](Instruction *) {}";
  opcodes {
)pre";

constexpr char kIsaSuffix[] = R"post(
  }
}
)post";

std::string OutputDir() { return "./"; }

static bool FileExists(const std::string &name) {
  std::ifstream file(name);
  return file.good();
}

class InstructionSetParserTest : public testing::Test {
 protected:
  InstructionSetParserTest() {
    paths_.push_back(kDepotPath);
    paths_.push_back(kDepotPath + std::string("/testfiles"));
  }

  std::vector<std::string> paths_;
};

TEST_F(InstructionSetParserTest, EmptyIsaName) {
  // Set up input and output file paths.
  std::vector<std::string> input_files = {
      absl::StrCat(kDepotPath, "/testfiles/", kEmptyBaseName, ".isa")};
  ASSERT_TRUE(FileExists(input_files[0]));
  std::string output_dir = OutputDir();
  InstructionSetVisitor visitor;
  EXPECT_FALSE(
      visitor.Process({}, kEmptyBaseName, "", paths_, output_dir).ok());
}

// An empty file should fail.
TEST_F(InstructionSetParserTest, NullFileParsing) {
  // Set up input and output file paths.
  std::vector<std::string> input_files = {
      absl::StrCat(kDepotPath, "/testfiles/", kEmptyBaseName, ".isa")};
  ASSERT_TRUE(FileExists(input_files[0]));
  std::string output_dir = OutputDir();

  InstructionSetVisitor visitor;
  // Parse and process the input file.
  EXPECT_FALSE(visitor
                   .Process(input_files, kEmptyBaseName, kEmptyIsaName, paths_,
                            output_dir)
                   .ok());
}

// Make sure recursive includes cause a failure.
TEST_F(InstructionSetParserTest, RecursiveInclude) {
  // Set up input and output file paths.
  std::vector<std::string> input_files = {absl::StrCat(
      kDepotPath, "/testfiles/", kRecursiveExampleBaseName, ".isa")};
  ASSERT_TRUE(FileExists(input_files[0]));
  std::string output_dir = OutputDir();

  InstructionSetVisitor visitor;
  EXPECT_FALSE(visitor
                   .Process(input_files, kRecursiveExampleBaseName,
                            kExampleIsaName, paths_, output_dir)
                   .ok());
}

// Make sure the visitor can read and parse the input file.
TEST_F(InstructionSetParserTest, BasicParsing) {
  // Set up input and output file paths.
  std::vector<std::string> input_files = {
      absl::StrCat(kDepotPath, "/testfiles/", kExampleBaseName, ".isa"),
      kBundleBFile};
  ASSERT_TRUE(FileExists(input_files[0]));
  std::string output_dir = getenv(kTestUndeclaredOutputsDir);

  InstructionSetVisitor visitor;
  // Parse and process the input file.
  EXPECT_TRUE(visitor
                  .Process(input_files, kExampleBaseName, kExampleIsaName,
                           paths_, output_dir)
                  .ok());
  // Verify that the decoder files _decoder.{.h,.cc} files were generated.
  EXPECT_TRUE(FileExists(
      absl::StrCat(output_dir, "/", kExampleBaseName, "_decoder.h")));

  EXPECT_TRUE(FileExists(
      absl::StrCat(output_dir, "/", kExampleBaseName, "_decoder.cc")));
}

TEST_F(InstructionSetParserTest, Generator) {
  // Set up input and output file paths.
  std::vector<std::string> input_files = {
      absl::StrCat(kDepotPath, "/testfiles/", kGeneratorBaseName, ".isa")};
  ASSERT_TRUE(FileExists(input_files[0]));
  std::string output_dir = getenv(kTestUndeclaredOutputsDir);

  InstructionSetVisitor visitor;
  // Parse and process the input file, capturing the log.
  LogSink log_sink;
  absl::AddLogSink(&log_sink);
  auto success = visitor
                     .Process(input_files, kGeneratorBaseName,
                              kGeneratorIsaName, paths_, output_dir)
                     .ok();
  absl::RemoveLogSink(&log_sink);
  EXPECT_TRUE(success);
  // Make sure the warning about unreferenced binding variable is found.
  auto ptr =
      log_sink.warning_log().find("Unreferenced binding variable 'unused'");
  EXPECT_TRUE(ptr != std::string::npos);
  // Verify that the decoder files _decoder.{.h,.cc} files were generated.
  EXPECT_TRUE(FileExists(
      absl::StrCat(output_dir, "/", kGeneratorBaseName, "_decoder.h")));

  EXPECT_TRUE(FileExists(
      absl::StrCat(output_dir, "/", kGeneratorBaseName, "_decoder.cc")));

  // Verify that the instruction enums and decoder entries include the
  // instructions inside the GENERATE() construct.
  std::ifstream enum_file(
      absl::StrCat(output_dir, "/", kGeneratorBaseName, "_enums.h"));
  CHECK(enum_file.good());
  std::string enum_str((std::istreambuf_iterator<char>(enum_file)),
                       (std::istreambuf_iterator<char>()));
  std::ifstream decoder_file(
      absl::StrCat(output_dir, "/", kGeneratorBaseName, "_decoder.cc"));
  CHECK(decoder_file.good());
  std::string decoder_str((std::istreambuf_iterator<char>(decoder_file)),
                          (std::istreambuf_iterator<char>()));
  for (auto name : {"kBeq", "kBeqW", "kBne", "kBneW", "kBlt", "kBltW", "kBltu",
                    "kBltuW", "kBge", "kBgeW", "kBgeu", "kBgeuW"}) {
    RE2 re(name);
    EXPECT_TRUE(RE2::PartialMatch(enum_str, re)) << name;
    EXPECT_TRUE(RE2::PartialMatch(decoder_str, re)) << name;
  }
}

TEST_F(InstructionSetParserTest, GeneratorErrorDuplicateVariable) {
  // Create the file.
  std::string file_name = testing::TempDir() + "/" + "duplicate_variable.isa";
  std::ofstream output_file(file_name, std::ofstream::out);
  CHECK(output_file.good());
  output_file << kIsaPrefix;
  // The GENERATE block reuses 'btype' in the second range specification.
  output_file << R"mid(
    GENERATE( btype = [ "eq", "ne", "lt", ltu, ge, geu],
             [w, fcn_w, btype] = [{"", "", 1}, {".w", _w, 2}] ) {
      b$(btype)$(fcn_w){: rs1, rs2, B_imm12 : next_pc},
        resources: { next_pc, rs1, rs2 : next_pc[0..]},
        disasm: "b$(btype)$(w)", "%rs1, %rs2, %(@+B_imm12:08x)",
        semfunc: "&sem_func_b$(btype)$(fcn_w)";
    };
)mid";
  output_file << kIsaSuffix;
  output_file.close();

  std::vector<std::string> input_files = {file_name};
  ASSERT_TRUE(FileExists(input_files[0]));
  std::string output_dir = getenv(kTestUndeclaredOutputsDir);
  InstructionSetVisitor visitor;
  // Parse and process the input file, capturing the log.
  LogSink log_sink;
  absl::AddLogSink(&log_sink);
  auto success = visitor
                     .Process(input_files, kGeneratorBaseName,
                              kGeneratorIsaName, paths_, output_dir)
                     .ok();
  absl::RemoveLogSink(&log_sink);
  EXPECT_FALSE(success);

  // Make sure the error about duplicate binding variable 'btype' is found.
  auto ptr =
      log_sink.error_log().find("Duplicate binding variable name 'btype'");
  EXPECT_TRUE(ptr != std::string::npos);
}

TEST_F(InstructionSetParserTest, GeneratorErrorTuplesError) {
  // Create the file.
  std::string file_name = testing::TempDir() + "/" + "tuples_required.isa";
  std::ofstream output_file(file_name, std::ofstream::out);
  CHECK(output_file.good());
  output_file << kIsaPrefix;
  // The GENERATE block lacks a value in the first tuple.
  output_file << R"mid(
    GENERATE( btype = [ "eq", "ne", "lt", ltu, ge, geu],
             [w, fcn_w] = [{""}, {".w", _w}] ) {
      b$(btype)$(fcn_w){: rs1, rs2, B_imm12 : next_pc},
        resources: { next_pc, rs1, rs2 : next_pc[0..]},
        disasm: "b$(btype)$(w)", "%rs1, %rs2, %(@+B_imm12:08x)",
        semfunc: "&sem_func_b$(btype)$(fcn_w)";
    };
)mid";
  output_file << kIsaSuffix;
  output_file.close();

  std::vector<std::string> input_files = {file_name};
  ASSERT_TRUE(FileExists(input_files[0]));
  std::string output_dir = getenv(kTestUndeclaredOutputsDir);
  InstructionSetVisitor visitor;
  // Parse and process the input file, capturing the log.
  LogSink log_sink;
  absl::AddLogSink(&log_sink);
  auto success = visitor
                     .Process(input_files, kGeneratorBaseName,
                              kGeneratorIsaName, paths_, output_dir)
                     .ok();
  absl::RemoveLogSink(&log_sink);
  EXPECT_FALSE(success);

  // Make sure the error about duplicate binding variable 'btype' is found.
  auto ptr = log_sink.error_log().find(
      "Number of values differs from number of identifiers");
  EXPECT_TRUE(ptr != std::string::npos);
}

TEST_F(InstructionSetParserTest, GeneratorErrorUndefinedBindingVariable) {
  // Create the file.
  std::string file_name = testing::TempDir() + "/" + "undefined_variable.isa";
  std::ofstream output_file(file_name, std::ofstream::out);
  CHECK(output_file.good());
  output_file << kIsaPrefix;
  // The GENERATE block references 'btype2'.
  output_file << R"mid(
    GENERATE( btype = [ "eq", "ne", "lt", ltu, ge, geu],
             [w, fcn_w] = [{"", ""}, {".w", _w}] ) {
      b$(btype)$(fcn_w){: rs1, rs2, B_imm12 : next_pc},
        resources: { next_pc, rs1, rs2 : next_pc[0..]},
        disasm: "b$(btype2)$(w)", "%rs1, %rs2, %(@+B_imm12:08x)",
        semfunc: "&sem_func_b$(btype)$(fcn_w)";
    };
)mid";
  output_file << kIsaSuffix;
  output_file.close();

  std::vector<std::string> input_files = {file_name};
  ASSERT_TRUE(FileExists(input_files[0]));
  std::string output_dir = getenv(kTestUndeclaredOutputsDir);
  InstructionSetVisitor visitor;
  // Parse and process the input file, capturing the log.
  LogSink log_sink;
  absl::AddLogSink(&log_sink);
  auto success = visitor
                     .Process(input_files, kGeneratorBaseName,
                              kGeneratorIsaName, paths_, output_dir)
                     .ok();
  absl::RemoveLogSink(&log_sink);
  EXPECT_FALSE(success);

  // Make sure the error about duplicate binding variable 'btype' is found.
  auto ptr = log_sink.error_log().find("Undefined binding variable 'btype2'");
  EXPECT_TRUE(ptr != std::string::npos);
}

TEST_F(InstructionSetParserTest, Undefined) {
  std::string file_name =
      absl::StrCat(kDepotPath, "/testfiles/", kUndefinedErrorsBaseName, ".isa");
  std::vector<std::string> input_files = {file_name};
  ASSERT_TRUE(FileExists(input_files[0]));
  std::string output_dir = getenv(kTestUndeclaredOutputsDir);
  InstructionSetVisitor visitor;
  // Parse and process the input file, capturing the log.
  LogSink log_sink;
  absl::AddLogSink(&log_sink);
  auto success = visitor
                     .Process(input_files, kUndefinedErrorsBaseName,
                              kUndefinedErrorsIsaName, paths_, output_dir)
                     .ok();
  absl::RemoveLogSink(&log_sink);
  EXPECT_FALSE(success);

  // Make sure the requisite error messages are present.
  auto ptr =
      log_sink.error_log().find("Error: Reference to undefined slot: 'slot_x'");
  EXPECT_TRUE(ptr != std::string::npos);
  ptr = log_sink.error_log().find(
      "Error: Reference to undefined bundle: 'bundle_x'");
  EXPECT_TRUE(ptr != std::string::npos);
  ptr = log_sink.error_log().find(
      "Error: Index 3 out of range for slot slot_a' referenced in bundle "
      "'bundle_a'");
  EXPECT_TRUE(ptr != std::string::npos);
  ptr = log_sink.error_log().find(
      "Error: Slot 'slot_a' lacks a default semantic action");
  EXPECT_TRUE(ptr != std::string::npos);
  ptr = log_sink.error_log().find(
      "Error: Only one `disasm width` declaration allowed - previous "
      "declaration on line:");
  EXPECT_TRUE(ptr != std::string::npos);
  ptr = log_sink.error_log().find(
      "Error: Isa 'UndefinedErrors' already declared - previous declaration on "
      "line:");
  EXPECT_TRUE(ptr != std::string::npos);
  ptr = log_sink.error_log().find(
      "Error: Bundle 'bundle_a' already declared - previous declaration on "
      "line:");
  EXPECT_TRUE(ptr != std::string::npos);
  ptr = log_sink.error_log().find(
      "Error: Slot 'slot_a' already declared - previous declaration on line:");
  EXPECT_TRUE(ptr != std::string::npos);
  ptr = log_sink.error_log().find("Error: Undefined base slot: slot_x");
  EXPECT_TRUE(ptr != std::string::npos);
  ptr = log_sink.error_log().find("Error: Opcode 'add' already declared");
  EXPECT_TRUE(ptr != std::string::npos);
  ptr = log_sink.error_log().find("Error: 'slot_a' is not a templated slot");
  EXPECT_TRUE(ptr != std::string::npos);
  ptr = log_sink.error_log().find("Error: Duplicate parameter name 'a'");
  EXPECT_TRUE(ptr != std::string::npos);
  ptr = log_sink.error_log().find(
      "Error: Wrong number of arguments: 2 were expected, 3 were provided");
  EXPECT_TRUE(ptr != std::string::npos);
  ptr = log_sink.error_log().find(
      "Error: Missing template arguments for slot 'slot_base_x'");
  EXPECT_TRUE(ptr != std::string::npos);
  ptr = log_sink.error_log().find("Error: Unable to evaluate expression: 'y'");
  EXPECT_TRUE(ptr != std::string::npos);
  ptr = log_sink.error_log().find("Error: Expression must be constant");
  EXPECT_TRUE(ptr != std::string::npos);
  ptr = log_sink.error_log().find("Error: Constant redefinition of 'x'");
  EXPECT_TRUE(ptr != std::string::npos);
  ptr = log_sink.error_log().find(
      "Error: Slot constant 'a' conflicts with template formal with same name");
  EXPECT_TRUE(ptr != std::string::npos);
  ptr = log_sink.error_log().find("Error: Redefinition of slot constant 'a'");
  EXPECT_TRUE(ptr != std::string::npos);
  ptr = log_sink.error_log().find(
      "Error: Multiple definitions of 'default' opcode");
  EXPECT_TRUE(ptr != std::string::npos);
  ptr = log_sink.error_log().find("Error: Duplicate disasm declaration");
  EXPECT_TRUE(ptr != std::string::npos);
  ptr = log_sink.error_log().find("Error: Duplicate semfunc declaration");
  EXPECT_TRUE(ptr != std::string::npos);
  ptr = log_sink.error_log().find(
      "Error: Only one semfunc specification per default opcode");
  EXPECT_TRUE(ptr != std::string::npos);
  ptr = log_sink.error_log().find(
      "Error: Default opcode lacks mandatory semfunc specification");
  EXPECT_TRUE(ptr != std::string::npos);
  ptr = log_sink.error_log().find(
      "Error: Resources 'res_a': duplicate definition");
  EXPECT_TRUE(ptr != std::string::npos);
  ptr = log_sink.error_log().find("Error: No function 'foo' supported");
  EXPECT_TRUE(ptr != std::string::npos);
  ptr = log_sink.error_log().find(
      "Error: Function 'abs' takes 1 parameters, but 2 were given");
  EXPECT_TRUE(ptr != std::string::npos);
  ptr = log_sink.error_log().find("Error: Multiple disasm specifications");
  EXPECT_TRUE(ptr != std::string::npos);
  ptr = log_sink.error_log().find("Error: Multiple semfunc specifications");
  EXPECT_TRUE(ptr != std::string::npos);
  ptr = log_sink.error_log().find("Error: Multiple resource specifications");
  EXPECT_TRUE(ptr != std::string::npos);
  ptr = log_sink.error_log().find("Error: Multiple attribute specifications");
  EXPECT_TRUE(ptr != std::string::npos);
  ptr = log_sink.error_log().find(
      "Error: 'xyz' is not a valid destination operand for opcode 'add_a'");
  EXPECT_TRUE(ptr != std::string::npos);
  ptr = log_sink.error_log().find(
      "Error: Decode time evaluation of latency expression not supported for "
      "resources");
  EXPECT_TRUE(ptr != std::string::npos);
  ptr = log_sink.error_log().find(
      "Error: Base slot does not define or inherit opcode 'sub_b'");
  EXPECT_TRUE(ptr != std::string::npos);
  ptr = log_sink.error_log().find(
      "Error: Base slot does not define or inherit opcode 'mul_a'");
  EXPECT_TRUE(ptr != std::string::npos);
  ptr = log_sink.error_log().find(
      "Error: Invalid deleted opcode 'xxx', slot 'slot_a' does not inherit "
      "from a base slot");
  EXPECT_TRUE(ptr != std::string::npos);
  ptr = log_sink.warning_log().find("Warning: Ignoring extra semfunc spec");
  EXPECT_TRUE(ptr != std::string::npos);
  ptr = log_sink.error_log().find(
      "Error: Fewer semfunc specifiers than expected for opcode 'ld_x'");
  EXPECT_TRUE(ptr != std::string::npos);
  ptr = log_sink.error_log().find(
      "Error: Resource reference can only reference a single destination "
      "operand");
  EXPECT_TRUE(ptr != std::string::npos);
  ptr = log_sink.error_log().find(
      "Error: Duplicate attribute name 'attr_a' in list");
  EXPECT_TRUE(ptr != std::string::npos);
  ptr = log_sink.error_log().find("Error: No function 'func' supported");
  EXPECT_TRUE(ptr != std::string::npos);
}

// Test disassembly format expressions.
TEST_F(InstructionSetParserTest, DisasmFormats) {
  std::string file_name =
      absl::StrCat(kDepotPath, "/testfiles/", kDisasmFormatsBaseName, ".isa");
  std::vector<std::string> input_files = {file_name};
  ASSERT_TRUE(FileExists(input_files[0]));
  std::string output_dir = getenv(kTestUndeclaredOutputsDir);
  InstructionSetVisitor visitor;
  // Parse and process the input file, capturing the log.
  LogSink log_sink;
  absl::AddLogSink(&log_sink);
  auto success = visitor
                     .Process(input_files, kDisasmFormatsBaseName,
                              kDisasmFormatsIsaName, paths_, output_dir)
                     .ok();
  absl::RemoveLogSink(&log_sink);
  EXPECT_FALSE(success);

  // Make sure the requisite error messages are present.
  auto ptr = log_sink.error_log().find(
      "Error: Unexpected end of format string in '%(()'");
  EXPECT_TRUE(ptr != std::string::npos);
  ptr = log_sink.error_log().find("Error: Empty format expression");
  EXPECT_TRUE(ptr != std::string::npos);
  ptr = log_sink.error_log().find(
      "Error: @ must be followed by a '+' or a '-' in '@*'");
  EXPECT_TRUE(ptr != std::string::npos);
  ptr = log_sink.error_log().find("Error: Malformed expression '@-'");
  EXPECT_TRUE(ptr != std::string::npos);
  ptr = log_sink.error_log().find(
      "Error: Invalid character in operand name at position 3 in '@+(5a)'");
  EXPECT_TRUE(ptr != std::string::npos);
  ptr = log_sink.error_log().find(
      "Error: Invalid character in operand name at position 2 in '@+5a'");
  EXPECT_TRUE(ptr != std::string::npos);
  ptr = log_sink.error_log().find(
      "Error: Invalid operand 'rs1' used in format for opcode'six'");
  EXPECT_TRUE(ptr != std::string::npos);
  ptr = log_sink.error_log().find("Error: Malformed expression '@+rs1 x'");
  EXPECT_TRUE(ptr != std::string::npos);
  ptr = log_sink.error_log().find(
      "Error: Missing shift in expression '@+(rs1 +-)'");
  EXPECT_TRUE(ptr != std::string::npos);
  ptr = log_sink.error_log().find(
      "Error: Malformed expression - expected ')' '@+(rs1 << 5x)'");
  EXPECT_TRUE(ptr != std::string::npos);
  ptr = log_sink.error_log().find(
      "Error: Malformed expression - no shift amount '@+(rs1 >> )'");
  EXPECT_TRUE(ptr != std::string::npos);
  ptr = log_sink.error_log().find(
      "Error: Malformed expression - expected ')' '@+(rs1 << 5 x)'");
  EXPECT_TRUE(ptr != std::string::npos);
  ptr = log_sink.error_log().find(
      "Error: Malformed expression - extra characters after ')' '@+(rs1 << 5) "
      "x'");
  EXPECT_TRUE(ptr != std::string::npos);
  ptr = log_sink.error_log().find(
      "Error: Format width required when a leading 0 is specified - '0x'");
  EXPECT_TRUE(ptr != std::string::npos);
  ptr = log_sink.error_log().find(
      "Error: Format width > than 3 digits not allowed '0123'");
  EXPECT_TRUE(ptr != std::string::npos);
  ptr =
      log_sink.error_log().find("Error: Illegal format specifier 'Y' in '08Y'");
  EXPECT_TRUE(ptr != std::string::npos);
  ptr = log_sink.error_log().find(
      "Error: Too many characters in format specifier '08xY'");
  EXPECT_TRUE(ptr != std::string::npos);
  ptr = log_sink.error_log().find(
      "Error: Invalid character in operand name at position 1 in '%2rs'");
  EXPECT_TRUE(ptr != std::string::npos);
  ptr = log_sink.error_log().find(
      "Error: Invalid operand 'rs2' used in format '%rs2'");
  EXPECT_TRUE(ptr != std::string::npos);
}

TEST_F(InstructionSetParserTest, Resource) {
  // Set up input and output file paths.
  std::vector<std::string> input_files = {
      absl::StrCat(kDepotPath, "/testfiles/", kResourceBaseName, ".isa")};
  ASSERT_TRUE(FileExists(input_files[0]));
  std::string output_dir = getenv(kTestUndeclaredOutputsDir);

  InstructionSetVisitor visitor;
  // Parse and process the input file, capturing the log.
  LogSink log_sink;
  absl::AddLogSink(&log_sink);
  auto success = visitor
                     .Process(input_files, kResourceBaseName, kResourceIsaName,
                              paths_, output_dir)
                     .ok();
  absl::RemoveLogSink(&log_sink);
  EXPECT_TRUE(success);
  // Verify that the decoder files _decoder.{.h,.cc} files were generated.
  EXPECT_TRUE(FileExists(
      absl::StrCat(output_dir, "/", kResourceBaseName, "_decoder.h")));

  EXPECT_TRUE(FileExists(
      absl::StrCat(output_dir, "/", kResourceBaseName, "_decoder.cc")));

  // Verify that the instruction enums and decoder entries include  the two
  // instructions.
  std::ifstream enum_file(
      absl::StrCat(output_dir, "/", kResourceBaseName, "_enums.h"));
  CHECK(enum_file.good());
  std::string enum_str((std::istreambuf_iterator<char>(enum_file)),
                       (std::istreambuf_iterator<char>()));
  std::ifstream decoder_file(
      absl::StrCat(output_dir, "/", kResourceBaseName, "_decoder.cc"));
  CHECK(decoder_file.good());
  std::string decoder_str((std::istreambuf_iterator<char>(decoder_file)),
                          (std::istreambuf_iterator<char>()));
  // Extract the OpcodeEnum content.
  std::string opcodes;
  RE2 opcodes_re("(?msU:\\s*OpcodeEnum[\\s\\n]*\\{([^\\}]*)\\})");
  EXPECT_TRUE(RE2::PartialMatch(enum_str, opcodes_re, &opcodes));
  for (auto &name : {"kInst0", "kInst1"}) {
    RE2 re(name);
    EXPECT_TRUE(RE2::PartialMatch(opcodes, re)) << name;
    EXPECT_TRUE(RE2::PartialMatch(decoder_str, re)) << name;
  }
  // Complex resource enum.
  std::string complex_resources;
  RE2 complex_resources_re(
      "(?msU:\\s*ComplexResourceEnum[\\s\\n]*\\{([^\\}]*)\\})");
  EXPECT_TRUE(
      RE2::PartialMatch(enum_str, complex_resources_re, &complex_resources));
  EXPECT_TRUE(RE2::PartialMatch(complex_resources, "kDest0"));
  EXPECT_FALSE(RE2::PartialMatch(complex_resources, "kDest1"));
  // Simple resource enum.
  std::string simple_resources;
  RE2 simple_resources_re(
      "(?msU:\\s*SimpleResourceEnum[\\s\\n]*\\{([^\\}]*)\\})");
  EXPECT_TRUE(
      RE2::PartialMatch(enum_str, simple_resources_re, &simple_resources));
  EXPECT_FALSE(RE2::PartialMatch(simple_resources, "kDest0"));
  EXPECT_TRUE(RE2::PartialMatch(simple_resources, "kDest1"));
}

}  // namespace
}  // namespace instruction_set
}  // namespace machine_description
}  // namespace sim
}  // namespace mpact
