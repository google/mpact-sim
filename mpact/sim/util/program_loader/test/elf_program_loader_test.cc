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

#include "mpact/sim/util/program_loader/elf_program_loader.h"

#include <ios>
#include <string>

#include "absl/status/status.h"
#include "googlemock/include/gmock/gmock.h"
#include "mpact/sim/generic/data_buffer.h"
#include "mpact/sim/util/memory/flat_demand_memory.h"

namespace {

constexpr char kFileName[] = "hello_world.elf";
constexpr char kFileName64[] = "hello_world_64.elf";
constexpr char kNotFound[] = "not_found_file";
constexpr char kNotAnElfFile[] = "not_an_elf_file";

constexpr uint64_t kStackSize = 0x24680;

// The depot path to the test directory.
constexpr char kDepotPath[] = "mpact/sim/util/program_loader/test/";

// The test fixture instantiates a an elfio object to read the elf file, a
// memory object, and the elf program loader that is tested.
class ElfLoaderTest : public testing::Test {
 protected:
  ElfLoaderTest() : memory_(0), loader_(&memory_) {}

  mpact::sim::util::FlatDemandMemory memory_;
  mpact::sim::util::ElfProgramLoader loader_;
  ELFIO::elfio elf_reader_;
  mpact::sim::generic::DataBufferFactory db_factory_;
};

bool DoesFileExist(const std::string fname) {
  std::ifstream f(fname);
  return f.good();
}
// First pass a name of a file that does not exist.
TEST_F(ElfLoaderTest, FileNotFound) {
  auto result = loader_.LoadProgram(kNotFound);
  EXPECT_EQ(result.status().code(), absl::StatusCode::kNotFound);
}

// Try a text file.
TEST_F(ElfLoaderTest, NotAnElfFile) {
  const std::string input_file =
      absl::StrCat(kDepotPath, "testfiles/", kNotAnElfFile);

  ASSERT_TRUE(DoesFileExist(input_file));
  auto result = loader_.LoadProgram(input_file);
  EXPECT_EQ(result.status().code(), absl::StatusCode::kInternal);
}

// Passing in a real elf file.
TEST_F(ElfLoaderTest, LoadExecutable) {
  const std::string input_file =
      absl::StrCat(kDepotPath, "testfiles/", kFileName);

  ASSERT_TRUE(DoesFileExist(input_file));
  ASSERT_TRUE(elf_reader_.load(input_file));
  ASSERT_THAT(elf_reader_.validate(), testing::StrEq(""));
  auto result = loader_.LoadProgram(input_file);
  ASSERT_TRUE(result.status().ok());
  for (auto const &segment : elf_reader_.segments) {
    if (segment->get_type() != PT_LOAD) continue;
    if (segment->get_file_size() == 0) continue;

    int size = segment->get_file_size();
    auto *db = db_factory_.Allocate(size);
    memory_.Load(segment->get_virtual_address(), db, nullptr, nullptr);
    EXPECT_EQ(memcmp(db->raw_ptr(), segment->get_data(), size), 0);
    db->DecRef();
  }
  EXPECT_EQ(result.value(), elf_reader_.get_entry());
  EXPECT_TRUE(absl::IsNotFound(loader_.GetStackSize().status()));
}

// Verify that the stack size is recognized and read correctly.
TEST_F(ElfLoaderTest, LoadExecutableWithStackSize) {
  const std::string input_file =
      absl::StrCat(kDepotPath, "testfiles/", kFileName64);

  ASSERT_TRUE(DoesFileExist(input_file));
  ASSERT_TRUE(elf_reader_.load(input_file));
  ASSERT_THAT(elf_reader_.validate(), testing::StrEq(""));
  auto result = loader_.LoadProgram(input_file);
  ASSERT_TRUE(result.status().ok());
  auto stack_result = loader_.GetStackSize();
  ASSERT_TRUE(stack_result.status().ok());
  EXPECT_EQ(stack_result.value(), kStackSize);
}

}  // namespace
