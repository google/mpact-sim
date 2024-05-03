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

package(
    default_applicable_licenses = ["//:license"],
    default_visibility = ["//visibility:public"],
)

# Core piece of the simulator.
cc_library(
    name = "core",
    srcs = [
        "data_buffer.cc",
        "resource_bitset.cc",
        "simple_resource.cc",
        "state_item_base.cc",
    ],
    hdrs = [
        "core_debug_interface.h",
        "data_buffer.h",
        "decoder_interface.h",
        "delay_line.h",
        "delay_line_interface.h",
        "function_delay_line.h",
        "immediate_operand.h",
        "literal_operand.h",
        "operand_interface.h",
        "ref_count.h",
        "resource_bitset.h",
        "resource_operand_interface.h",
        "simple_resource.h",
        "simple_resource_operand.h",
        "state_item.h",
        "state_item_base.h",
    ],
    copts = ["-O3"],
    deps = [
        "//util/symbolize",
        "@com_google_absl//absl/base:core_headers",
        "@com_google_absl//absl/container:btree",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/log",
        "@com_google_absl//absl/numeric:bits",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/types:any",
        "@com_google_absl//absl/types:span",
    ],
)

cc_library(
    name = "arch_state",
    srcs = [
        "arch_state.cc",
        "complex_resource.cc",
        "complex_resource_operand.cc",
        "control_register.cc",
        "fifo.cc",
        "fifo_with_notify.cc",
        "register.cc",
        "token_fifo.cc",
    ],
    hdrs = [
        "arch_state.h",
        "complex_resource.h",
        "complex_resource_operand.h",
        "control_register.h",
        "devnull_operand.h",
        "fifo.h",
        "fifo_with_notify.h",
        "register.h",
        "status_register.h",
        "token_fifo.h",
    ],
    copts = ["-O3"],
    deps = [
        ":component",
        ":config",
        ":core",
        ":internal",
        ":program_error",
        "//mpact/sim/proto:component_data_cc_proto",
        "@com_google_absl//absl/base:core_headers",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/functional:any_invocable",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/types:span",
    ],
)

cc_library(
    name = "core_debug_interface",
    srcs = [],
    hdrs = [
        "core_debug_interface.h",
    ],
    deps = [
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
    ],
)

cc_library(
    name = "instruction",
    srcs = [
        "instruction.cc",
    ],
    hdrs = [
        "instruction.h",
        "instruction_helpers.h",
    ],
    copts = ["-O3"],
    deps = [
        ":arch_state",
        ":core",
        "@com_google_absl//absl/numeric:int128",
        "@com_google_absl//absl/types:span",
    ],
)

cc_library(
    name = "decode_cache",
    srcs = [
        "decode_cache.cc",
    ],
    hdrs = [
        "decode_cache.h",
    ],
    copts = ["-O3"],
    deps = [
        ":core",
        ":instruction",
        "@com_google_absl//absl/numeric:bits",
    ],
)

cc_library(
    name = "program_error",
    srcs = [
        "program_error.cc",
    ],
    hdrs = [
        "program_error.h",
    ],
    copts = ["-O3"],
    deps = [
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/container:flat_hash_set",
        "@com_google_absl//absl/strings",
    ],
)

cc_library(
    name = "config",
    hdrs = [
        "config.h",
    ],
    copts = ["-O3"],
    deps = [
        ":internal",
        "//mpact/sim/proto:component_data_cc_proto",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/types:variant",
    ],
)

cc_library(
    name = "counters",
    srcs = [
    ],
    hdrs = [
        "counters.h",
        "counters_base.h",
    ],
    copts = ["-O3"],
    deps = [
        ":internal",
        "//mpact/sim/proto:component_data_cc_proto",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/types:variant",
    ],
)

cc_library(
    name = "component",
    srcs = [
        "component.cc",
    ],
    hdrs = [
        "component.h",
    ],
    copts = ["-O3"],
    deps = [
        ":config",
        ":counters",
        "//mpact/sim/proto:component_data_cc_proto",
        "@com_google_absl//absl/container:btree",
        "@com_google_absl//absl/log",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
    ],
)

cc_library(
    name = "internal",
    hdrs = [
        "signed_type.h",
        "variant_helper.h",
    ],
    copts = ["-O3"],
    visibility = ["//visibility:private"],
    deps = [
        "@com_google_absl//absl/types:variant",
    ],
)

cc_library(
    name = "type_helpers",
    hdrs = [
        "type_helpers.h",
    ],
    deps = [
        "@com_google_absl//absl/numeric:int128",
    ],
)
