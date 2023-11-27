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
#include <iostream>
#include <ostream>
#include <string>
#include <vector>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/log/log.h"
#include "absl/strings/str_split.h"
#include "mpact/sim/decoder/proto_format_visitor.h"

// Flag specifying the output directory of the generated code. When building
// using blaze, this is automatically set by the build command in the .bzl file.
ABSL_FLAG(std::string, output_dir, "./", "output file directory");
// The name prefix for the generated files. The output names will be:
//   <prefix>_decoder.h
//   <prefix>_decoder.cc
// When building using blaze, this is set to the to the base name of the input
// grammar (.bin) file.
ABSL_FLAG(std::string, prefix, "", "prefix for generated files");
ABSL_FLAG(std::string, decoder_name, "", "decoder name to generate");
ABSL_FLAG(std::string, include, "", "include file root(s)");
ABSL_FLAG(std::string, proto_include, "", "proto include file root(s)");
ABSL_FLAG(std::string, proto_files, "", "proto file(s)");

int main(int argc, char **argv) {
  // Process the command line options.
  auto arg_vec = absl::ParseCommandLine(argc, argv);

  std::vector<std::string> file_names;

  // The file names are the non-flag values on the command line.
  for (int i = 1; i < arg_vec.size(); ++i) {
    file_names.push_back(arg_vec[i]);
  }

  // Get the flag values.
  std::string output_dir = absl::GetFlag(FLAGS_output_dir);
  std::string prefix = absl::GetFlag(FLAGS_prefix);
  std::string decoder_name = absl::GetFlag(FLAGS_decoder_name);

  auto include_roots_string = absl::GetFlag(FLAGS_include);
  std::vector<std::string> include_roots =
      absl::StrSplit(include_roots_string, ',', absl::SkipWhitespace());

  auto proto_dirs_string = absl::GetFlag(FLAGS_proto_include);
  std::vector<std::string> proto_include =
      absl::StrSplit(proto_dirs_string, ',', absl::SkipWhitespace());
  char cwd[1024];
  getcwd(cwd, sizeof(cwd));
  std::string cwd_str(cwd);
  auto pos = cwd_str.find("google3");
  if (pos != std::string::npos) {
    proto_include.push_back(cwd_str.substr(0, pos + 7));
  }

  auto proto_files_string = absl::GetFlag(FLAGS_proto_files);
  std::vector<std::string> proto_files =
      absl::StrSplit(proto_files_string, ',', absl::SkipWhitespace());

  if (prefix.empty()) {
    std::cerr << "Error: prefix must be specified and non-empty" << std::endl;
    exit(-1);
  }

  // Process the proto_fmt file(s).
  ::mpact::sim::decoder::proto_fmt::ProtoFormatVisitor visitor;
  auto status = visitor.Process(file_names, decoder_name, prefix, include_roots,
                                proto_include, proto_files, output_dir);
  if (!status.ok()) {
    LOG(ERROR) << status.message();
    return -1;
  }
  return 0;
}
