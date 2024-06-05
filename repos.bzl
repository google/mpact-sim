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

"""Load dependent repositories"""

load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive", "http_file")

def mpact_sim_repos():
    """ Load dependencies needed to use mpact-sim as a 3rd-party consumer"""

    # Google Absail.
    if not native.existing_rule("com_google_absl"):
        http_archive(
            name = "com_google_absl",
            sha256 = "3c743204df78366ad2eaf236d6631d83f6bc928d1705dd0000b872e53b73dc6a",
            strip_prefix = "abseil-cpp-20240116.1",
            urls = ["https://github.com/abseil/abseil-cpp/archive/refs/tags/20240116.1.tar.gz"],
        )

    # Google protobuf.
    if not native.existing_rule("com_google_protobuf"):
        http_archive(
            name = "com_google_protobuf",
            sha256 = "da288bf1daa6c04d03a9051781caa52aceb9163586bff9aa6cfb12f69b9395aa",
            strip_prefix = "protobuf-27.0",
            urls = ["https://github.com/protocolbuffers/protobuf/releases/download/v27.0/protobuf-27.0.tar.gz"],
        )

    # Google test.
    if not native.existing_rule("com_google_googletest"):
        http_archive(
            name = "com_google_googletest",
            sha256 = "8ad598c73ad796e0d8280b082cebd82a630d73e73cd3c70057938a6501bba5d7",
            strip_prefix = "googletest-1.14.0",
            urls = ["https://github.com/google/googletest/archive/refs/tags/v1.14.0.tar.gz"],
        )

    # Transitive dependencies of googletest. Need to list explicitly for v1.13.0.
    # Can load as deps in a later version.
    if not native.existing_rule("com_googlesource_code_re2"):
        http_archive(
            name = "com_googlesource_code_re2",  # 2023-11-01
            sha256 = "4e6593ac3c71de1c0f322735bc8b0492a72f66ffccfad76e259fa21c41d27d8a",
            strip_prefix = "re2-2023-11-01",
            urls = ["https://github.com/google/re2/archive/refs/tags/2023-11-01/re2-2023-11-01.tar.gz"],
        )

    # Additional rules for licenses
    if not native.existing_rule("rules_license"):
        http_archive(
            name = "rules_license",
            sha256 = "6157e1e68378532d0241ecd15d3c45f6e5cfd98fc10846045509fb2a7cc9e381",
            url = "https://github.com/bazelbuild/rules_license/releases/download/0.0.4/rules_license-0.0.4.tar.gz",
        )

    # ELFIO header based library.
    http_archive(
        name = "com_github_serge1_elfio",
        build_file = "@com_google_mpact-sim//:external/BUILD.elfio",
        sha256 = "767b269063fc35aba6d361139f830aa91c45dc6b77942f082666876c1aa0be0f",
        strip_prefix = "elfio-3.9",
        urls = ["https://github.com/serge1/ELFIO/releases/download/Release_3.9/elfio-3.9.tar.gz"],
    )

    # Antlr4 tool (java).
    http_file(
        name = "org_antlr_tool",
        sha256 = "bc13a9c57a8dd7d5196888211e5ede657cb64a3ce968608697e4f668251a8487",
        url = "https://www.antlr.org/download/antlr-4.13.1-complete.jar",
    )

    # Antlr4 c++ runtime.
    http_archive(
        name = "org_antlr4_cpp_runtime",
        add_prefix = "antlr4-runtime",
        build_file = "@com_google_mpact-sim//:external/BUILD.antlr4",
        sha256 = "d350e09917a633b738c68e1d6dc7d7710e91f4d6543e154a78bb964cfd8eb4de",
        strip_prefix = "runtime/src",
        urls = ["https://www.antlr.org/download/antlr4-cpp-runtime-4.13.1-source.zip"],
    )
