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
            sha256 = "3ea49a7d97421b88a8c48a0de16c16048e17725c7ec0f1d3ea2683a2a75adc21",
            strip_prefix = "abseil-cpp-20230125.0",
            urls = ["https://github.com/abseil/abseil-cpp/archive/refs/tags/20230125.0.tar.gz"],
        )

    # Google protobuf.
    if not native.existing_rule("com_google_protobuf"):
        http_archive(
            name = "com_google_protobuf",
            sha256 = "4eab9b524aa5913c6fffb20b2a8abf5ef7f95a80bc0701f3a6dbb4c607f73460",
            strip_prefix = "protobuf-3.21.12",
            urls = ["https://github.com/protocolbuffers/protobuf/releases/download/v21.12/protobuf-cpp-3.21.12.tar.gz"],
        )

    # Bazel rules for proto.
    if not native.existing_rule("rules_proto_grpc"):
        http_archive(
            name = "rules_proto_grpc",
            sha256 = "fb7fc7a3c19a92b2f15ed7c4ffb2983e956625c1436f57a3430b897ba9864059",
            strip_prefix = "rules_proto_grpc-4.3.0",
            urls = ["https://github.com/rules-proto-grpc/rules_proto_grpc/archive/4.3.0.tar.gz"],
        )

    # Google test.
    if not native.existing_rule("com_google_googletest"):
        http_archive(
            name = "com_google_googletest",
            sha256 = "ad7fdba11ea011c1d925b3289cf4af2c66a352e18d4c7264392fead75e919363",
            strip_prefix = "googletest-1.13.0",
            urls = ["https://github.com/google/googletest/archive/refs/tags/v1.13.0.tar.gz"],
        )

    # Transitive dependencies of googletest. Need to list explicitly for v1.13.0.
    # Can load as deps in a later version.
    if not native.existing_rule("com_googlesource_code_re2"):
        http_archive(
            name = "com_googlesource_code_re2",  # 2022-12-21T14:29:10Z
            sha256 = "b9ce3a51beebb38534d11d40f8928d40509b9e18a735f6a4a97ad3d014c87cb5",
            strip_prefix = "re2-d0b1f8f2ecc2ea74956c7608b6f915175314ff0e",
            urls = ["https://github.com/google/re2/archive/d0b1f8f2ecc2ea74956c7608b6f915175314ff0e.zip"],
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
        build_file = "@@com_google_mpact-sim//:external/BUILD.elfio",
        sha256 = "767b269063fc35aba6d361139f830aa91c45dc6b77942f082666876c1aa0be0f",
        strip_prefix = "elfio-3.9",
        urls = ["https://github.com/serge1/ELFIO/releases/download/Release_3.9/elfio-3.9.tar.gz"],
    )

    # Antlr4 tool (java).
    http_file(
        name = "org_antlr_tool",
        sha256 = "62975e192b4af2622b72b5f0131553ee3cbce97f76dc2a41632dcc55e25473e1",
        url = "https://www.antlr.org/download/antlr-4.11.1-complete.jar",
    )

    # Antlr4 c++ runtime.
    http_archive(
        name = "org_antlr4_cpp_runtime",
        add_prefix = "antlr4-runtime",
        build_file = "@com_google_mpact-sim//:external/BUILD.antlr4",
        sha256 = "8018c335316e61bb768e5bd4a743a9303070af4e1a8577fa902cd053c17249da",
        strip_prefix = "runtime/src",
        urls = ["https://www.antlr.org/download/antlr4-cpp-runtime-4.11.1-source.zip"],
    )
