# Copyright 2024 Google LLC
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

# This file contains the build rules for assembler related libraries.

load("@rules_cc//cc:cc_library.bzl", "cc_library")

package(
    default_applicable_licenses = ["//:license"],
    default_visibility = ["//visibility:public"],
)

cc_library(
    name = "asm",
    srcs = ["resolver.cc"],
    hdrs = [
        "opcode_assembler_interface.h",
        "resolver.h",
        "resolver_interface.h",
    ],
    deps = [
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/functional:any_invocable",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@com_github_serge1_elfio//:elfio",
    ],
)

cc_library(
    name = "simple_assembler",
    srcs = ["simple_assembler.cc"],
    hdrs = ["simple_assembler.h"],
    deps = [
        ":asm",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@abseil-cpp//absl/functional:bind_front",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/types:span",
        "@com_github_serge1_elfio//:elfio",
        "@com_googlesource_code_re2//:re2",
    ],
)
