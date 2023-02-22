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

# This is the base library for the fundamental interfaces and data structures
# used in the simulator. All pieces are meant to be generic. If any
# specialization is required, architecture dependent projects should define
# appropriate (possibly derived) supplementary structures.

workspace(name = "com_google_mpact-sim")

load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_file", "http_archive")

# Additional bazel rules.
http_archive(
    name = "bazel_skylib",
    sha256 = "b8a1527901774180afc798aeb28c4634bdccf19c4d98e7bdd1ce79d1fe9aaad7",
    urls = ["https://github.com/bazelbuild/bazel-skylib/releases/download/1.4.1/bazel-skylib-1.4.1.tar.gz"],
)

load("@bazel_skylib//:workspace.bzl", "bazel_skylib_workspace")
bazel_skylib_workspace()

# Google Absail.
http_archive(
    name = "com_google_absl",
    sha256 = "3ea49a7d97421b88a8c48a0de16c16048e17725c7ec0f1d3ea2683a2a75adc21",
    urls = ["https://github.com/abseil/abseil-cpp/archive/refs/tags/20230125.0.tar.gz"],
    strip_prefix="abseil-cpp-20230125.0",
)

# Google protobuf.
http_archive(
    name = "com_google_protobuf",
    sha256 = "4eab9b524aa5913c6fffb20b2a8abf5ef7f95a80bc0701f3a6dbb4c607f73460",
    urls = ["https://github.com/protocolbuffers/protobuf/releases/download/v21.12/protobuf-cpp-3.21.12.tar.gz"],
    strip_prefix="protobuf-3.21.12",
)

# Bazel rules for proto.
http_archive(
    name = "rules_proto_grpc",
    sha256 = "fb7fc7a3c19a92b2f15ed7c4ffb2983e956625c1436f57a3430b897ba9864059",
    strip_prefix = "rules_proto_grpc-4.3.0",
    urls = ["https://github.com/rules-proto-grpc/rules_proto_grpc/archive/4.3.0.tar.gz"],
)

# Google test.
http_archive(
    name = "com_google_googletest",
    strip_prefix = "googletest-1.13.0",
    sha256 = "ad7fdba11ea011c1d925b3289cf4af2c66a352e18d4c7264392fead75e919363",
    urls = ["https://github.com/google/googletest/archive/refs/tags/v1.13.0.tar.gz"],
)

# ELFIO header based library.
http_archive(
    build_file = "BUILD.elfio",
    name = "com_github_serge1_elfio",
    strip_prefix = "elfio-3.11",
    sha256 = "3307b104c205399786edbba203906de9517e36297709fe747faf9478d55fbb91",
    urls = ["https://github.com/serge1/ELFIO/releases/download/Release_3.11/elfio-3.11.tar.gz"],
)

_ALL_CONTENT = """\
filegroup(
  name = "all_srcs",
  srcs = glob(["**"]),
  visibility = ["//visibility:public"],
)
"""

# Antlr4 c++ runtime.
http_archive(
    name = "org_antlr4_cpp_runtime",
    build_file = "BUILD.antlr4",
    sha256 = "8018c335316e61bb768e5bd4a743a9303070af4e1a8577fa902cd053c17249da",
    urls = ["https://www.antlr.org/download/antlr4-cpp-runtime-4.11.1-source.zip"],
)

# Antlr4 tool (java).
http_file(
    name = "org_antlr_tool",
    sha256 = "62975e192b4af2622b72b5f0131553ee3cbce97f76dc2a41632dcc55e25473e1",
    url = "https://www.antlr.org/download/antlr-4.11.1-complete.jar",
)

# Additional rules for building non-bazel projects. Antlr4 builds using cmake.
http_archive(
    name = "rules_foreign_cc",
    sha256 = "2a4d07cd64b0719b39a7c12218a3e507672b82a97b98c6a89d38565894cf7c51",
    strip_prefix = "rules_foreign_cc-0.9.0",
    url = "https://github.com/bazelbuild/rules_foreign_cc/archive/refs/tags/0.9.0.tar.gz",
)

load("@rules_foreign_cc//foreign_cc:repositories.bzl", "rules_foreign_cc_dependencies")
rules_foreign_cc_dependencies()

load("@rules_proto_grpc//:repositories.bzl", "rules_proto_grpc_toolchains", "rules_proto_grpc_repos")
rules_proto_grpc_toolchains()
rules_proto_grpc_repos()

load("@rules_proto//proto:repositories.bzl", "rules_proto_dependencies", "rules_proto_toolchains")
rules_proto_dependencies()
rules_proto_toolchains()
