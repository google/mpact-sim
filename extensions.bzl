# Copyright 2026 Google LLC
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

"""Extensions for mpact-sim that are not handled by MODULE.bazel."""

load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive", "http_file")

def _non_module_deps_impl(ctx):  # @unused
    # ELFIO header based library
    http_archive(
        name = "com_github_serge1_elfio",
        build_file = "@mpact-sim//:external/BUILD.elfio",
        sha256 = "caf49f3bf55a9c99c98ebea4b05c79281875783802e892729eea0415505f68c4",
        strip_prefix = "elfio-3.12",
        urls = ["https://github.com/serge1/ELFIO/releases/download/Release_3.12/elfio-3.12.tar.gz"],
    )

    # Antlr4 tool (java)
    http_file(
        name = "org_antlr_tool",
        sha256 = "bc13a9c57a8dd7d5196888211e5ede657cb64a3ce968608697e4f668251a8487",
        url = "https://www.antlr.org/download/antlr-4.13.1-complete.jar",
    )

    # Antlr4 c++ runtime
    http_archive(
        name = "org_antlr4_cpp_runtime",
        add_prefix = "antlr4-runtime",
        build_file = "@mpact-sim//:external/BUILD.antlr4",
        sha256 = "d350e09917a633b738c68e1d6dc7d7710e91f4d6543e154a78bb964cfd8eb4de",
        strip_prefix = "runtime/src",
        urls = ["https://www.antlr.org/download/antlr4-cpp-runtime-4.13.1-source.zip"],
    )

non_module_deps = module_extension(implementation = _non_module_deps_impl)
