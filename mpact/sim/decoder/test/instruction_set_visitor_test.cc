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

#include <fstream>
#include <string>
#include <vector>

#include "googlemock/include/gmock/gmock.h"
#include "absl/flags/flag.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_split.h"

namespace mpact {
namespace sim {
namespace machine_description {
namespace instruction_set {
namespace {

constexpr char kTestUndeclaredOutputsDir[] = "TEST_UNDECLARED_OUTPUTS_DIR";

constexpr char kExampleBaseName[] = "example";
constexpr char kRecursiveExampleBaseName[] = "example_with_recursive_include";
constexpr char kEmptyBaseName[] = "empty_file";

constexpr char kEmptyIsaName[] = "Empty";
constexpr char kExampleIsaName[] = "Example";

// The depot path to the test directory.
constexpr char kDepotPath[] = "mpact/sim/decoder/test/";

std::string OutputDir() {
  return "./";
}

static bool FileExists(const std::string &name) {
  std::ifstream file(name);
  return file.good();
}

class InstructionSetParserTest : public testing::Test {
 protected:
  InstructionSetParserTest() {
  }

  std::vector<std::string> paths_;
};

// An empty file should fail.
TEST_F(InstructionSetParserTest, NullFileParsing) {
  // Set up input and output file paths.
  const std::string input_file = 
      absl::StrCat(kDepotPath, "testfiles/", kEmptyBaseName, ".isa");
  ASSERT_TRUE(FileExists(input_file));
  std::string output_dir = OutputDir();

  InstructionSetVisitor visitor;
  // Parse and process the input file.
  EXPECT_FALSE(visitor
                   .Process(input_file, kEmptyBaseName, kEmptyIsaName, paths_,
                            output_dir)
                   .ok());
}

// Make sure recursive includes cause a failure.
TEST_F(InstructionSetParserTest, RecursiveInclude) {
  // Set up input and output file paths.
  const std::string input_file =
      absl::StrCat(
          kDepotPath, "testfiles/", kRecursiveExampleBaseName, ".isa");
  ASSERT_TRUE(FileExists(input_file));
  std::string output_dir = OutputDir();

  InstructionSetVisitor visitor;
  EXPECT_FALSE(visitor
                   .Process(input_file, kRecursiveExampleBaseName,
                            kExampleIsaName, paths_, output_dir)
                   .ok());
}

// Make sure the visitor can read and parse the input file.
TEST_F(InstructionSetParserTest, BasicParsing) {
  // Set up input and output file paths.
  const std::string input_file = 
      absl::StrCat(kDepotPath, "testfiles/", kExampleBaseName, ".isa");
  ASSERT_TRUE(FileExists(input_file));
  std::string output_dir = getenv(kTestUndeclaredOutputsDir);

  InstructionSetVisitor visitor;
  // Parse and process the input file.
  EXPECT_TRUE(visitor.Process(input_file, kExampleBaseName, kExampleIsaName,
                            paths_, output_dir).ok());
  // Verify that the decoder files _decoder.{.h,.cc} files were generated.
  EXPECT_TRUE(FileExists(
    absl::StrCat(output_dir, "/", kExampleBaseName, "_decoder.h")));

  EXPECT_TRUE(
    FileExists(absl::StrCat(output_dir, "/", kExampleBaseName, 
                            "_decoder.cc")));
}

}  // namespace
}  // namespace instruction_set
}  // namespace machine_description
}  // namespace sim
}  // namespace mpact
