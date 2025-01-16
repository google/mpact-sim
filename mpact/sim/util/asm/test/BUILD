# Copyright 2025 Google LLC
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

# This file contains the build rules for tests related to assembler related libraries.

load("//mpact/sim/decoder:mpact_sim_isa.bzl", "mpact_bin_fmt_decoder", "mpact_isa_decoder")

package(
    default_applicable_licenses = ["//:license"],
    default_visibility = ["//visibility:public"],
)

cc_library(
    name = "riscv64x_instructions",
    testonly = True,
    srcs = ["riscv64x_instructions.cc"],
    hdrs = ["riscv64x_instructions.h"],
    deps = [
        "//mpact/sim/generic:instruction",
    ],
)

mpact_isa_decoder(
    name = "riscv64x_isa",
    testonly = True,
    src = "riscv64x.isa",
    includes = [],
    isa_name = "RiscV64X",
    deps = [
        ":riscv64x_instructions",
    ],
)

mpact_bin_fmt_decoder(
    name = "riscv64x_bin_fmt",
    testonly = True,
    src = "riscv64x.bin_fmt",
    decoder_name = "RiscV64X",
    includes = [
    ],
    prefix = "riscv64x",
    deps = [
        ":riscv64x_isa",
    ],
)

cc_library(
    name = "riscv64x_encoder",
    testonly = True,
    srcs = [
        "riscv64x_bin_encoder_interface.cc",
        "riscv_bin_setters.cc",
    ],
    hdrs = [
        "riscv64x_bin_encoder_interface.h",
        "riscv_bin_setters.h",
        "riscv_getter_helpers.h",
    ],
    deps = [
        ":riscv64x_bin_fmt",
        ":riscv64x_isa",
        "//mpact/sim/generic:type_helpers",
        "//mpact/sim/util/asm",
        "@com_google_absl//absl/base:no_destructor",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/functional:any_invocable",
        "@com_google_absl//absl/log",
        "@com_google_absl//absl/log:check",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_googlesource_code_re2//:re2",
    ],
)

cc_test(
    name = "riscv64x_asm_test",
    size = "small",
    srcs = ["riscv64x_asm_test.cc"],
    deps = [
        ":riscv64x_bin_fmt",
        ":riscv64x_encoder",
        ":riscv64x_isa",
        "//mpact/sim/util/asm",
        "//mpact/sim/util/asm:simple_assembler",
        "@com_github_serge1_elfio//:elfio",
        "@com_google_absl//absl/base:no_destructor",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/log",
        "@com_google_absl//absl/log:check",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/types:span",
        "@com_google_googletest//:gtest_main",
        "@com_googlesource_code_re2//:re2",
    ],
)
