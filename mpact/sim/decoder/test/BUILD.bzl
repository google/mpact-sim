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

# This contains the test projects for the decoder.

load("//mpact/sim/decoder:mpact_sim_isa.bzl", "mpact_bin_fmt_decoder", "mpact_isa_decoder", "mpact_cc_library", "mpact_cc_test")
load("@bazel_skylib//rules:build_test.bzl", "build_test")

mpact_cc_test(
    name = "instruction_set_test",
    size = "small",
    srcs = [
        "instruction_set_test.cc",
    ],
    deps = [
        "//mpact/sim/decoder:isa_parser",
        "@com_google_googletest//:gtest",
        "@com_google_googletest//:gtest_main",
        "@com_google_absl//absl/memory",
        "@com_google_absl//absl/strings",
    ],
)

mpact_cc_test(
    name = "bundle_test",
    size = "small",
    srcs = [
        "bundle_test.cc",
    ],
    deps = [
        "//mpact/sim/decoder:isa_parser",
        "@com_google_googletest//:gtest",
        "@com_google_googletest//:gtest_main",
        "@com_google_absl//absl/memory",
        "@com_google_absl//absl/strings",
    ],
)

mpact_cc_test(
    name = "slot_test",
    size = "small",
    srcs = [
        "slot_test.cc",
    ],
    deps = [
        "//mpact/sim/decoder:isa_parser",
        "@com_google_googletest//:gtest",
        "@com_google_googletest//:gtest_main",
        "@com_google_absl//absl/memory",
        "@com_google_absl//absl/strings",
    ],
)

mpact_cc_test(
    name = "opcode_test",
    size = "small",
    srcs = [
        "opcode_test.cc",
    ],
    deps = [
        "//mpact/sim/decoder:isa_parser",
        "@com_google_googletest//:gtest",
        "@com_google_googletest//:gtest_main",
        "@com_google_absl//absl/memory",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
    ],
)

mpact_cc_test(
    name = "template_expression_test",
    size = "small",
    srcs = [
        "template_expression_test.cc",
    ],
    deps = [
        "//mpact/sim/decoder:isa_parser",
        "@com_google_googletest//:gtest",
        "@com_google_googletest//:gtest_main",
        "@com_google_absl//absl/memory",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/types:variant",
    ],
)

mpact_cc_test(
    name = "instruction_set_visitor_test",
    size = "small",
    srcs = [
        "instruction_set_visitor_test.cc",
        #"testfiles/sparsecore_vex_opcode_enum.h",
    ],
    data = [
        # Local include files.
        "testfiles/empty_file.isa",
        "testfiles/example.isa",
        "testfiles/bundle_b.isa",
        "testfiles/example_with_recursive_include.isa",
        "testfiles/recursive_include.isa",
        # Project exported include files.
        "//mpact/sim/decoder/test/testfiles/include:empty_include.isa",
        "//mpact/sim/decoder/test/testfiles/include:bundle_a.isa",
    ],
    deps = [
        "//mpact/sim/decoder:isa_parser",
        "@com_google_googletest//:gtest",
        "@com_google_googletest//:gtest_main",
        "@com_google_absl//absl/flags:flag",
        "@com_google_absl//absl/memory",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
    ],
)

mpact_isa_decoder(
    name = "example_isa",
    src = "testfiles/example.isa",
    includes = [
        ":testfiles/bundle_b.isa",
        "//mpact/sim/decoder/test/testfiles/include:empty_include.isa",
        "//mpact/sim/decoder/test/testfiles/include:bundle_a.isa",
    ],
    isa_name = "Example",
)

mpact_cc_library(
    name = "riscv32i",
    hdrs = ["testfiles/riscv32i.h"],
    deps = [
        "//mpact/sim/generic:instruction",
    ],
)

mpact_isa_decoder(
    name = "derived_isa",
    src = "testfiles/derived.isa",
    includes = [
        ":testfiles/base.isa",
    ],
    isa_name = "RiscV32I",
    deps = ["riscv32i"],
)

# The point of this test is to actually run the generator on the testfiles/example.isa
# grammar file, generate source files, and then compile the generated files and
# link into this test case. Provided that succeeds, this test case succeeds. See the
# :example_isa build rule for build options.
mpact_cc_test(
    name = "example_decoder_test",
    size = "small",
    srcs = [
        "example_decoder_test.cc",
    ],
    deps = [
        ":example_isa",
        "//mpact/sim/generic:arch_state",
        "//mpact/sim/generic:instruction",
        "@com_google_googletest//:gtest",
        "@com_google_googletest//:gtest_main",
        "@com_google_absl//absl/container:flat_hash_map",
    ],
)

mpact_cc_library(
    name = "sparsecore_vex_opcode_enum",
    hdrs = [
        "testfiles/sparsecore_vex_opcode_enum.h",
    ],
)

mpact_cc_test(
    name = "extract_bits_test",
    size = "small",
    srcs = [
        "extract_bits_test.cc",
    ],
    deps = [
        "@com_google_absl//absl/numeric:bits",
        "@com_google_googletest//:gtest",
        "@com_google_googletest//:gtest_main",
    ],
)

mpact_cc_test(
    name = "extract_test",
    size = "small",
    srcs = [
        "extract_test.cc",
    ],
    deps = [
        "//mpact/sim/decoder:bin_format_visitor",
        "@com_google_absl//absl/strings",
        "@com_google_googletest//:gtest",
        "@com_google_googletest//:gtest_main",
    ],
)

mpact_cc_test(
    name = "overlay_test",
    size = "small",
    srcs = [
        "overlay_test.cc",
    ],
    deps = [
        "//mpact/sim/decoder:bin_format_visitor",
        "@com_google_googletest//:gtest",
        "@com_google_googletest//:gtest_main",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
    ],
)

mpact_cc_test(
    name = "format_test",
    size = "small",
    srcs = [
        "format_test.cc",
    ],
    deps = [
        "//mpact/sim/decoder:bin_format_visitor",
        "//mpact/sim/decoder:decoder_error_listener",
        "@com_google_googletest//:gtest",
        "@com_google_googletest//:gtest_main",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
    ],
)
