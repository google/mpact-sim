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

load("//mpact/sim/decoder:mpact_sim_isa.bzl", "mpact_cc_library", "mpact_cc_test", "mpact_isa_decoder", "mpact_proto_fmt_decoder")
load("//tools/build_defs/proto/cpp:cc_proto_library.bzl", "cc_proto_library")

package(
    default_applicable_licenses = ["//:license"],
)

cc_library(
    name = "log_sink",
    testonly = True,
    hdrs = ["log_sink.h"],
    deps = [
        "@com_google_absl//absl/base:log_severity",
        "@com_google_absl//absl/log:log_entry",
        "@com_google_absl//absl/log:log_sink",
        "@com_google_absl//absl/strings",
    ],
)

mpact_cc_test(
    name = "instruction_set_test",
    size = "small",
    srcs = [
        "instruction_set_test.cc",
    ],
    deps = [
        "//mpact/sim/decoder:isa_parser",
        "@com_google_absl//absl/memory",
        "@com_google_absl//absl/strings",
        "@com_google_googletest//:gtest",
        "@com_google_googletest//:gtest_main",
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
        "@com_google_absl//absl/memory",
        "@com_google_absl//absl/strings",
        "@com_google_googletest//:gtest",
        "@com_google_googletest//:gtest_main",
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
        "@com_google_absl//absl/memory",
        "@com_google_absl//absl/strings",
        "@com_google_googletest//:gtest",
        "@com_google_googletest//:gtest_main",
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
        "@com_google_absl//absl/memory",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_googletest//:gtest",
        "@com_google_googletest//:gtest_main",
    ],
)

mpact_cc_test(
    name = "resource_test",
    size = "small",
    srcs = [
        "resource_test.cc",
    ],
    deps = [
        "//mpact/sim/decoder:isa_parser",
        "@com_google_absl//absl/container:btree",
        "@com_google_absl//absl/status:statusor",
        "@com_google_googletest//:gtest",
        "@com_google_googletest//:gtest_main",
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
        "@com_google_absl//absl/memory",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/types:variant",
        "@com_google_googletest//:gtest",
        "@com_google_googletest//:gtest_main",
    ],
)

mpact_cc_test(
    name = "instruction_set_visitor_test",
    size = "small",
    srcs = [
        "instruction_set_visitor_test.cc",
    ],
    data = [
        # Local include files.
        "testfiles/bundle_b.isa",
        "testfiles/empty_file.isa",
        "testfiles/example.isa",
        "testfiles/example_with_recursive_include.isa",
        "testfiles/generator.isa",
        "testfiles/recursive_include.isa",
        "testfiles/resource.isa",
        "testfiles/undefined_errors.isa",
        "testfiles/disasm_formats.isa",
        # Project exported include files.
        "//mpact/sim/decoder/test/testfiles/include:bundle_a.isa",
        "//mpact/sim/decoder/test/testfiles/include:empty_include.isa",
    ],
    deps = [
        ":log_sink",
        "//mpact/sim/decoder:isa_parser",
        "@com_google_absl//absl/flags:flag",
        "@com_google_absl//absl/log:check",
        "@com_google_absl//absl/log:log_sink_registry",
        "@com_google_absl//absl/memory",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_googletest//:gtest",
        "@com_google_googletest//:gtest_main",
        "@com_googlesource_code_re2//:re2",
    ],
)

mpact_isa_decoder(
    name = "example_isa",
    srcs = [
        "testfiles/bundle_b.isa",
        "testfiles/example.isa",
    ],
    includes = [
        #":testfiles/bundle_b.isa",
        "//mpact/sim/decoder/test/testfiles/include:bundle_a.isa",
        "//mpact/sim/decoder/test/testfiles/include:empty_include.isa",
    ],
    isa_name = "Example",
    prefix = "example_isa",
)

mpact_isa_decoder(
    name = "combined_isa",
    srcs = [
        "testfiles/bundle_b.isa",
        "testfiles/part1.isa",
    ],
    includes = [
        "//mpact/sim/decoder/test/testfiles/include:bundle_a.isa",
        "//mpact/sim/decoder/test/testfiles/include:empty_include.isa",
    ],
    isa_name = "Example",
    prefix = "combined_isa",
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

# The point of this test is to actually run the generator on the
# testfiles/example.isa grammar file, generate source files, and then compile
# the generated files and link into this test case. Provided that succeeds,
# this test case succeeds. See the :example_isa build rule for build options.
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
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_googletest//:gtest",
        "@com_google_googletest//:gtest_main",
    ],
)

# The point of this test is to actually run the generator on the
# testfiles/part1.isa grammar file, generate source files, and then compile
# the generated files and link into this test case. Provided that succeeds,
# this test case succeeds. See the :example_isa build rule for build options.
mpact_cc_test(
    name = "combined_decoder_test",
    size = "small",
    srcs = [
        "combined_decoder_test.cc",
    ],
    deps = [
        ":combined_isa",
        "//mpact/sim/generic:arch_state",
        "//mpact/sim/generic:instruction",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_googletest//:gtest",
        "@com_google_googletest//:gtest_main",
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
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
        "@com_google_googletest//:gtest",
        "@com_google_googletest//:gtest_main",
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
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
        "@com_google_googletest//:gtest",
        "@com_google_googletest//:gtest_main",
    ],
)

mpact_cc_test(
    name = "bin_encoding_info_test",
    size = "small",
    srcs = [
        "bin_encoding_info_test.cc",
    ],
    deps = [
        "//mpact/sim/decoder:bin_format_visitor",
        "//mpact/sim/decoder:decoder_error_listener",
        "@com_google_absl//absl/container:btree",
        "@com_google_absl//absl/log:check",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_googletest//:gtest",
        "@com_google_googletest//:gtest_main",
    ],
)

mpact_cc_test(
    name = "instruction_encoding_test",
    size = "small",
    srcs = [
        "instruction_encoding_test.cc",
    ],
    deps = [
        "//mpact/sim/decoder:bin_format_visitor",
        "//mpact/sim/decoder:decoder_error_listener",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_googletest//:gtest",
        "@com_google_googletest//:gtest_main",
    ],
)

mpact_cc_test(
    name = "bin_format_visitor_test",
    size = "small",
    srcs = [
        "bin_format_visitor_test.cc",
    ],
    data = [
        # Local include files.
        "testfiles/constraints.bin_fmt",
        "testfiles/empty_file.bin_fmt",
        "testfiles/example_with_recursive_include.bin_fmt",
        "testfiles/format_error.bin_fmt",
        "testfiles/format_error_undef.bin_fmt",
        "testfiles/generator.bin_fmt",
        "testfiles/instruction_group.bin_fmt",
        "testfiles/instruction_group_errors.bin_fmt",
        "testfiles/recursive_include.bin_fmt",
        "testfiles/riscv32_top.bin_fmt",
        "testfiles/riscv32c.bin_fmt",
        "testfiles/riscv32g.bin_fmt",
        "testfiles/syntax_error.bin_fmt",
        "testfiles/vliw.bin_fmt",
    ],
    deps = [
        ":log_sink",
        "//mpact/sim/decoder:bin_format_visitor",
        "@com_google_absl//absl/flags:flag",
        "@com_google_absl//absl/log:check",
        "@com_google_absl//absl/log:log_sink_registry",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_googletest//:gtest",
        "@com_google_googletest//:gtest_main",
        "@com_googlesource_code_re2//:re2",
    ],
)

mpact_cc_test(
    name = "proto_format_visitor_test",
    size = "small",
    srcs = [
        "proto_format_visitor_test.cc",
    ],
    data = [
        # Local include files.
        "testfiles/empty_file.proto_fmt",
        "testfiles/riscv32g.proto_fmt",
        "testfiles/riscv32g.proto",
    ],
    deps = [
        ":log_sink",
        "//mpact/sim/decoder:proto_format_visitor",
        "@com_google_absl//absl/flags:flag",
        "@com_google_absl//absl/log",
        "@com_google_absl//absl/log:check",
        "@com_google_absl//absl/log:log_sink_registry",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_googletest//:gtest",
        "@com_google_googletest//:gtest_main",
        "@com_googlesource_code_re2//:re2",
    ],
)

mpact_cc_test(
    name = "proto_constraint_expression_test",
    size = "small",
    srcs = [
        "proto_constraint_expression_test.cc",
    ],
    data = [
        "testfiles/riscv32g.proto",
    ],
    deps = [
        "//mpact/sim/decoder:proto_format_visitor",
        "//mpact/sim/generic:type_helpers",
        "@com_google_absl//absl/log",
        "@com_google_absl//absl/log:check",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_googletest//:gtest",
        "@com_google_googletest//:gtest_main",
        "@com_google_protobuf//:protobuf",
        "@com_google_protobuf//:protobuf/compiler:importer",
    ],
)

mpact_proto_fmt_decoder(
    name = "riscv32g_proto_decoder",
    srcs = ["testfiles/riscv32g.proto_fmt"],
    decoder_name = "RiscV32GProto",
    includes = [],
    prefix = "riscv32g_proto",
    proto_files = ["testfiles/riscv32g.proto"],
    deps = [
        ":riscv32g_cc_proto",
        "//third_party/mpact_riscv:riscv32g_isa",
    ],
)

proto_library(
    name = "riscv32g_proto",
    srcs = ["testfiles/riscv32g.proto"],
    cc_api_version = 2,
)

cc_proto_library(
    name = "riscv32g_cc_proto",
    deps = [":riscv32g_proto"],
)
