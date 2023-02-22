# Copyright 2023 Google LLC
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     https://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

# Unit test for program loaders.

cc_test(
    name = "elf_program_loader_test",
    size = "small",
    srcs = [
        "elf_program_loader_test.cc",
    ],
    data = [
        "testfiles/hello_world.elf",
        "testfiles/hello_world_64.elf",
        "testfiles/not_an_elf_file",
    ],
    deps = [
        "//mpact/sim/generic:core",
        "//mpact/sim/util/memory",
        "//mpact/sim/util/program_loader:elf_loader",
        "@com_google_googletest//:gtest",
        "@com_google_googletest//:gtest_main",
        "@com_google_absl//absl/status",
    ],
)
