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

#include "mpact/sim/decoder/proto_format_visitor.h"

#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/strings/str_cat.h"
#include "googletest/include/gtest/gtest.h"

// This file contains unit tests for the ProtoFormatVisitor class.

namespace {

using mpact::sim::decoder::proto_fmt::ProtoFormatVisitor;

constexpr char kTestUndeclaredOutputsDir[] = "TEST_UNDECLARED_OUTPUTS_DIR";

constexpr char kRiscV32Isa[] = "testfiles/riscv32i.proto";
constexpr char kEmptyBaseName[] = "empty_file";
constexpr char kRiscV32BaseName[] = "riscv32i";

constexpr char kEmptyIsaName[] = "Empty";
constexpr char kRiscV32IsaName[] = "RiscV32IProto";

// The depot path to the test directory.
constexpr char kDepotPath[] = "mpact/sim/decoder/test";

static bool FileExists(const std::string &name) {
  std::ifstream file(name);
  return file.good();
}

class ProtoFormatVisitorTest : public testing::Test {
 protected:
  ProtoFormatVisitorTest() {
    paths_.push_back("./");
    paths_.push_back(kDepotPath);
    paths_.push_back(kDepotPath + std::string("/testfiles"));
  }

  std::vector<std::string> paths_;
};

// Verify that the parser behaves properly when the file is empty.
TEST_F(ProtoFormatVisitorTest, EmptyProto) {
  // Set up input and output file paths.
  std::vector<std::string> input_files = {
      absl::StrCat(kDepotPath, "/testfiles/", kEmptyBaseName, ".proto_fmt")};
  ASSERT_TRUE(FileExists(input_files[0]));
  std::string output_dir = getenv(kTestUndeclaredOutputsDir);
  ProtoFormatVisitor visitor;
  EXPECT_FALSE(
      visitor
          .Process(input_files, kEmptyIsaName, {}, {paths_}, {}, {}, output_dir)
          .ok());
}

// Verify that the input file is parsed with no errors.
TEST_F(ProtoFormatVisitorTest, ExampleProto) {
  // Set up input and output file paths.
  std::vector<std::string> input_files = {
      absl::StrCat(kDepotPath, "/testfiles/", kRiscV32BaseName, ".proto_fmt")};
  ASSERT_TRUE(FileExists(input_files[0]));
  std::string output_dir = getenv(kTestUndeclaredOutputsDir);
  ProtoFormatVisitor visitor;
  auto status = visitor.Process(input_files, kRiscV32IsaName, "", {}, {paths_},
                                {kRiscV32Isa}, output_dir);
  EXPECT_TRUE(status.ok()) << status.message();
}

}  // namespace
