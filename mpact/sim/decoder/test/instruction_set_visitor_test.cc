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
#include "absl/strings/str_cat.h"
#include "googlemock/include/gmock/gmock.h"
#include "googletest/include/gtest/gtest.h"
#include "re2/re2.h"

namespace mpact {
namespace sim {
namespace machine_description {
namespace instruction_set {
namespace {

constexpr char kTestUndeclaredOutputsDir[] = "TEST_UNDECLARED_OUTPUTS_DIR";

constexpr char kExampleBaseName[] = "example";
constexpr char kRecursiveExampleBaseName[] = "example_with_recursive_include";
constexpr char kEmptyBaseName[] = "empty_file";
constexpr char kGeneratorBaseName[] = "generator";

constexpr char kEmptyIsaName[] = "Empty";
constexpr char kExampleIsaName[] = "Example";
constexpr char kGeneratorIsaName[] = "Generator";

// The depot path to the test directory.
constexpr char kDepotPath[] = "mpact/sim/decoder/test/";

std::string OutputDir() { return "./"; }

static bool FileExists(const std::string &name) {
  std::ifstream file(name);
  return file.good();
}

class InstructionSetParserTest : public testing::Test {
 protected:
  InstructionSetParserTest() {}

  std::vector<std::string> paths_;
};

// An empty file should fail.
TEST_F(InstructionSetParserTest, NullFileParsing) {
  // Set up input and output file paths.
  std::vector<std::string> input_files = {
      absl::StrCat(kDepotPath, "testfiles/", kEmptyBaseName, ".isa")};
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
      kDepotPath, "testfiles/", kRecursiveExampleBaseName, ".isa")};
  ASSERT_TRUE(FileExists(input_files[0]));
  std::string output_dir = OutputDir();

  InstructionSetVisitor visitor;
  EXPECT_FALSE(visitor
                   .Process(input_files, kRecursiveExampleBaseName,
                            kExampleIsaName, paths_, output_dir)
                   .ok());
}  // namespace

// Make sure the visitor can read and parse the input file.
TEST_F(InstructionSetParserTest, BasicParsing) {
  // Set up input and output file paths.
  std::vector<std::string> input_files = {
      absl::StrCat(kDepotPath, "testfiles/", kExampleBaseName, ".isa")};
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
      absl::StrCat(kDepotPath, "testfiles/", kGeneratorBaseName, ".isa")};
  ASSERT_TRUE(FileExists(input_files[0]));
  std::string output_dir = getenv(kTestUndeclaredOutputsDir);

  InstructionSetVisitor visitor;
  // Parse and process the input file.
  EXPECT_TRUE(visitor
                  .Process(input_files, kGeneratorBaseName, kGeneratorIsaName,
                           paths_, output_dir)
                  .ok());
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

}  // namespace
}  // namespace instruction_set
}  // namespace machine_description
}  // namespace sim
}  // namespace mpact
