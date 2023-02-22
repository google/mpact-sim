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

# Contains the test cases for the sim/generic directory.

cc_test(
    name = "data_buffer_test",
    size = "small",
    srcs = ["data_buffer_test.cc"],
    deps = [
        "//mpact/sim/generic:core",
        "@com_google_googletest//:gtest",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_test(
    name = "delay_line_test",
    size = "small",
    srcs = ["delay_line_test.cc"],
    deps = [
        "//mpact/sim/generic:core",
        "@com_google_googletest//:gtest",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_test(
    name = "ref_count_test",
    size = "small",
    srcs = ["ref_count_test.cc"],
    deps = [
        "//mpact/sim/generic:core",
        "@com_google_googletest//:gtest",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_test(
    name = "state_item_base_test",
    size = "small",
    srcs = ["state_item_base_test.cc"],
    deps = [
        "//mpact/sim/generic:core",
        "@com_google_googletest//:gtest",
        "@com_google_googletest//:gtest_main",
        "@com_google_absl//absl/memory",
        "@com_google_absl//absl/strings",
    ],
)

cc_test(
    name = "register_test",
    size = "small",
    srcs = ["register_test.cc"],
    deps = [
        "//mpact/sim/generic:arch_state",
        "//mpact/sim/generic:core",
        "@com_google_googletest//:gtest",
        "@com_google_googletest//:gtest_main",
        "@com_google_absl//absl/memory",
    ],
)

cc_test(
    name = "control_register_test",
    size = "small",
    srcs = ["control_register_test.cc"],
    deps = [
        "//mpact/sim/generic:arch_state",
        "//mpact/sim/generic:core",
        "@com_google_googletest//:gtest",
        "@com_google_googletest//:gtest_main",
        "@com_google_absl//absl/memory",
    ],
)

cc_test(
    name = "status_register_test",
    size = "small",
    srcs = ["status_register_test.cc"],
    deps = [
        "//mpact/sim/generic:arch_state",
        "//mpact/sim/generic:core",
        "@com_google_googletest//:gtest",
        "@com_google_googletest//:gtest_main",
        "@com_google_absl//absl/functional:any_invocable",
    ],
)

cc_test(
    name = "fifo_test",
    size = "small",
    srcs = ["fifo_test.cc"],
    deps = [
        "//mpact/sim/generic:arch_state",
        "//mpact/sim/generic:config",
        "//mpact/sim/generic:core",
        "//mpact/sim/generic:program_error",
        "//mpact/sim/proto:component_data_cc_proto",
        "@com_google_protobuf//:protobuf",
        "@com_google_googletest//:gtest",
        "@com_google_googletest//:gtest_main",
        "@com_google_absl//absl/memory",
    ],
)

cc_test(
    name = "fifo_with_notify_test",
    size = "small",
    srcs = ["fifo_with_notify_test.cc"],
    deps = [
        "//mpact/sim/generic:arch_state",
        "//mpact/sim/generic:config",
        "//mpact/sim/generic:core",
        "//mpact/sim/generic:program_error",
        "//mpact/sim/proto:component_data_cc_proto",
        "@com_google_protobuf//:protobuf",
        "@com_google_googletest//:gtest",
        "@com_google_googletest//:gtest_main",
        "@com_google_absl//absl/memory",
    ],
)

cc_test(
    name = "token_fifo_test",
    size = "small",
    srcs = ["token_fifo_test.cc"],
    deps = [
        "//mpact/sim/generic:arch_state",
        "//mpact/sim/generic:core",
        "//mpact/sim/generic:program_error",
        "@com_google_googletest//:gtest",
        "@com_google_googletest//:gtest_main",
        "@com_google_absl//absl/memory",
    ],
)

cc_test(
    name = "immediate_operand_test",
    size = "small",
    srcs = ["immediate_operand_test.cc"],
    deps = [
        "//mpact/sim/generic:core",
        "@com_google_googletest//:gtest",
        "@com_google_googletest//:gtest_main",
        "@com_google_absl//absl/base:core_headers",
        "@com_google_absl//absl/memory",
    ],
)

cc_test(
    name = "literal_operand_test",
    size = "small",
    srcs = ["literal_operand_test.cc"],
    deps = [
        "//mpact/sim/generic:core",
        "@com_google_googletest//:gtest",
        "@com_google_googletest//:gtest_main",
        "@com_google_absl//absl/base:core_headers",
        "@com_google_absl//absl/memory",
    ],
)

cc_test(
    name = "devnull_operand_test",
    size = "small",
    srcs = ["devnull_operand_test.cc"],
    deps = [
        "//mpact/sim/generic:arch_state",
        "//mpact/sim/generic:core",
        "@com_google_googletest//:gtest",
        "@com_google_googletest//:gtest_main",
        "@com_google_absl//absl/base:core_headers",
        "@com_google_absl//absl/memory",
    ],
)

cc_test(
    name = "resource_bitset_test",
    size = "small",
    srcs = ["resource_bitset_test.cc"],
    deps = [
        "//mpact/sim/generic:core",
        "@com_google_googletest//:gtest",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_test(
    name = "register_operand_test",
    size = "small",
    srcs = ["register_operand_test.cc"],
    deps = [
        "//mpact/sim/generic:arch_state",
        "//mpact/sim/generic:core",
        "@com_google_googletest//:gtest",
        "@com_google_googletest//:gtest_main",
        "@com_google_absl//absl/base:core_headers",
        "@com_google_absl//absl/memory",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/types:any",
    ],
)

cc_test(
    name = "fifo_operand_test",
    size = "small",
    srcs = ["fifo_operand_test.cc"],
    deps = [
        "//mpact/sim/generic:arch_state",
        "//mpact/sim/generic:core",
        "@com_google_googletest//:gtest",
        "@com_google_googletest//:gtest_main",
        "@com_google_absl//absl/base:core_headers",
        "@com_google_absl//absl/memory",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/types:any",
    ],
)

cc_test(
    name = "arch_state_test",
    size = "small",
    srcs = ["arch_state_test.cc"],
    deps = [
        "//mpact/sim/generic:arch_state",
        "//mpact/sim/generic:core",
        "@com_google_googletest//:gtest",
        "@com_google_googletest//:gtest_main",
        "@com_google_absl//absl/strings",
    ],
)

cc_test(
    name = "instruction_test",
    size = "small",
    srcs = ["instruction_test.cc"],
    deps = [
        "//mpact/sim/generic:arch_state",
        "//mpact/sim/generic:core",
        "//mpact/sim/generic:instruction",
        "@com_google_googletest//:gtest",
        "@com_google_googletest//:gtest_main",
        "@com_google_absl//absl/memory",
        "@com_google_absl//absl/types:span",
    ],
)

cc_test(
    name = "decode_cache_test",
    size = "small",
    srcs = ["decode_cache_test.cc"],
    deps = [
        "//mpact/sim/generic:core",
        "//mpact/sim/generic:decode_cache",
        "//mpact/sim/generic:instruction",
        "@com_google_googletest//:gtest",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_test(
    name = "program_error_test",
    size = "small",
    srcs = ["program_error_test.cc"],
    deps = [
        "//mpact/sim/generic:program_error",
        "@com_google_googletest//:gtest",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_test(
    name = "simple_resource_test",
    size = "small",
    srcs = ["simple_resource_test.cc"],
    deps = [
        "//mpact/sim/generic:core",
        "@com_google_googletest//:gtest",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_test(
    name = "complex_resource_test",
    size = "small",
    srcs = ["complex_resource_test.cc"],
    deps = [
        "//mpact/sim/generic:arch_state",
        "//mpact/sim/generic:core",
        "@com_google_googletest//:gtest",
        "@com_google_googletest//:gtest_main",
        "@com_google_absl//absl/status",
    ],
)

cc_test(
    name = "complex_resource_operand_test",
    size = "small",
    srcs = ["complex_resource_operand_test.cc"],
    deps = [
        "//mpact/sim/generic:arch_state",
        "//mpact/sim/generic:core",
        "@com_google_googletest//:gtest",
        "@com_google_googletest//:gtest_main",
        "@com_google_absl//absl/status",
    ],
)

cc_test(
    name = "config_test",
    size = "small",
    srcs = ["config_test.cc"],
    deps = [
        "//mpact/sim/generic:config",
        "//mpact/sim/proto:component_data_cc_proto",
        "@com_google_protobuf//:protobuf",
        "@com_google_googletest//:gtest",
        "@com_google_googletest//:gtest_main",
        "@com_google_absl//absl/container:btree",
        "@com_google_absl//absl/memory",
        "@com_google_absl//absl/status",
    ],
)

cc_test(
    name = "component_test",
    size = "small",
    srcs = ["component_test.cc"],
    deps = [
        "//mpact/sim/generic:component",
        "//mpact/sim/generic:config",
        "//mpact/sim/generic:counters",
        "//mpact/sim/proto:component_data_cc_proto",
        "@com_google_googletest//:gtest",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_test(
    name = "counters_test",
    size = "small",
    srcs = [
        "counters_test.cc",
    ],
    deps = [
        "//mpact/sim/generic:counters",
        "//mpact/sim/proto:component_data_cc_proto",
        "@com_google_googletest//:gtest",
        "@com_google_googletest//:gtest_main",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/types:optional",
        "@com_google_absl//absl/types:variant",
    ],
)

