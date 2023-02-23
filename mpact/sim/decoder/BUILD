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

# Description:
#   The decoder generator tool for MPACT-Sim. This consists of two rules for
#   to generate C++ code from the antlr4 grammar InstructionSet.g4
#   (antlr4_cc_parser, antlr4_cc_lexer), a cc_library for processing the
#   input file, and a cc_main for the top level executable.

load("@bazel_skylib//:bzl_library.bzl", "bzl_library")
load("//mpact/sim/decoder:antlr_cc.bzl", "antlr4_cc_lexer", "antlr4_cc_parser")
load("//mpact/sim/decoder:mpact_sim_isa.bzl", "mpact_cc_library", "mpact_cc_binary")

package(default_visibility = ["//visibility:public"])

antlr4_cc_parser(
    name = "antlr_isa_parser",
    src = "InstructionSet.g4",
    listener = True,
    namespaces = [
        "sim",
        "machine_description",
        "instruction_set",
        "generated",
    ],
    visitor = True,
)

antlr4_cc_lexer(
    name = "antlr_isa_lexer",
    src = "InstructionSet.g4",
    namespaces = [
        "sim",
        "machine_description",
        "instruction_set",
        "generated",
    ],
    deps = [
    ],
)

mpact_cc_library(
    name = "decoder_error_listener",
    srcs = [
        "decoder_error_listener.cc",
    ],
    hdrs = [
        "decoder_error_listener.h",
    ],
    deps = [
        "@com_google_absl//absl/log",
        "@com_google_absl//absl/strings",
        "@org_antlr4_cpp_runtime//:antlr4",
    ],
    visibility = [":__subpackages__"],
)

mpact_cc_library(
    name = "antlr_parser_wrapper",
    hdrs = ["antlr_parser_wrapper.h"],
    deps = [
        "@org_antlr4_cpp_runtime//:antlr4",
    ],
)

mpact_cc_library(
    name = "isa_parser",
    srcs = [
        "bundle.cc",
        "format_name.cc",
        "instruction.cc",
        "instruction_set.cc",
        "instruction_set_visitor.cc",
        "opcode.cc",
        "resource.cc",
        "slot.cc",
        "template_expression.cc",
    ],
    hdrs = [
        "base_class.h",
        "bundle.h",
        "format_name.h",
        "instruction.h",
        "instruction_set.h",
        "instruction_set_visitor.h",
        "opcode.h",
        "resource.h",
        "slot.h",
        "template_expression.h",
    ],
    copts = [
        "-fexceptions",
    ],
    features = [
        "-use_header_modules",
    ],
    visibility = [":__subpackages__"],
    deps = [
        ":antlr_isa_lexer",
        ":antlr_isa_parser",
        ":antlr_parser_wrapper",
        ":decoder_error_listener",
        "@com_google_absl//absl/container:btree",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/container:flat_hash_set",
        "@com_google_absl//absl/log",
        "@com_google_absl//absl/memory",
        "@com_google_absl//absl/numeric:bits",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@org_antlr4_cpp_runtime//:antlr4",
    ],
)

mpact_cc_binary(
    name = "decoder_gen",
    srcs = [
        "decoder_gen_main.cc",
    ],
    copts = [
        "-fexceptions",
    ],
    features = [
        "-use_header_modules",
    ],
    visibility = ["//visibility:public"],
    deps = [
        ":isa_parser",
        "@com_google_absl//absl/flags:flag",
        "@com_google_absl//absl/flags:parse",
        "@com_google_absl//absl/log",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
    ],
)

antlr4_cc_parser(
    name = "bin_format_parser",
    src = "BinFormat.g4",
    listener = True,
    namespaces = [
        "mpact",
        "sim",
        "decoder",
        "bin_format",
        "generated",
    ],
    visitor = True,
)

antlr4_cc_lexer(
    name = "bin_format_lexer",
    src = "BinFormat.g4",
    namespaces = [
        "mpact",
        "sim",
        "decoder",
        "bin_format",
        "generated",
    ],
    deps = [
    ],
)

mpact_cc_library(
    name = "bin_format_visitor",
    srcs = [
        "bin_decoder.cc",
        "bin_encoding_info.cc",
        "bin_format_visitor.cc",
        "encoding_group.cc",
        "extract.cc",
        "format.cc",
        "format_name.cc",
        "instruction_encoding.cc",
        "instruction_group.cc",
        "overlay.cc",
    ],
    hdrs = [
        "bin_decoder.h",
        "bin_encoding_info.h",
        "bin_format_contexts.h",
        "bin_format_visitor.h",
        "encoding_group.h",
        "extract.h",
        "format.h",
        "format_name.h",
        "instruction_encoding.h",
        "instruction_group.h",
        "overlay.h",
    ],
    copts = [
        "-fexceptions",
    ],
    features = [
        "-use_header_modules",
    ],
    visibility = [":__subpackages__"],
    deps = [
        ":antlr_parser_wrapper",
        ":bin_format_lexer",
        ":bin_format_parser",
        ":decoder_error_listener",
        "@com_google_absl//absl/container:btree",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/container:flat_hash_set",
        "@com_google_absl//absl/functional:bind_front",
        "@com_google_absl//absl/log",
        "@com_google_absl//absl/numeric:bits",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@org_antlr4_cpp_runtime//:antlr4",
    ],
)

mpact_cc_binary(
    name = "bin_format_gen",
    srcs = [
        "bin_format_gen_main.cc",
    ],
    copts = [
        "-fexceptions",
    ],
    features = [
        "-use_header_modules",
    ],
    visibility = ["//visibility:public"],
    deps = [
        ":bin_format_visitor",
        "@com_google_absl//absl/flags:flag",
        "@com_google_absl//absl/flags:parse",
        "@com_google_absl//absl/log",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
    ],
)

bzl_library(
    name = "mact_sim_isa_bzl",
    srcs = ["mpact_sim_isa.bzl"],
    visibility = ["//visibility:public"],
)

