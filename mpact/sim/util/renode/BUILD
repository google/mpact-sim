# Copyright 2024 Google LLC
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

# This package provides libraries to make it easy to integrate mpact_sim
# based simulators with ReNode, a system modeling framework:
# https://renode.io

package(
    default_applicable_licenses = ["//:license"],
    default_visibility = ["//visibility:public"],
)

cc_library(
    name = "renode",
    srcs = [
        "renode_memory_access.cc",
        "renode_mpact.cc",
        "stoull_wrapper.cc",
    ],
    hdrs = [
        "renode_memory_access.h",
        "renode_mpact.h",
        "stoull_wrapper.h",
    ],
    deps = [
        ":renode_debug_interface",
        "//mpact/sim/generic:core",
        "//mpact/sim/generic:core_debug_interface",
        "//mpact/sim/generic:instruction",
        "//mpact/sim/generic:type_helpers",
        "//mpact/sim/util/memory",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/functional:any_invocable",
        "@com_google_absl//absl/log",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
    ],
)

cc_library(
    name = "renode_debug_interface",
    hdrs = [
        "renode_debug_interface.h",
    ],
    deps = [
        "//mpact/sim/generic:core_debug_interface",
        "//mpact/sim/generic:type_helpers",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
    ],
)

cc_library(
    name = "socket_cli",
    srcs = [
        "cli_forwarder.cc",
        "renode_cli_top.cc",
        "socket_cli.cc",
    ],
    hdrs = [
        "cli_forwarder.h",
        "renode_cli_top.h",
        "socket_cli.h",
        "socket_streambuf.h",
    ],
    deps = [
        ":renode_debug_interface",
        "//mpact/sim/generic:core",
        "//mpact/sim/generic:core_debug_interface",
        "//mpact/sim/generic:debug_command_shell_interface",
        "//mpact/sim/generic:instruction",
        "//mpact/sim/generic:type_helpers",
        "//mpact/sim/util/program_loader:elf_loader",
        "@com_google_absl//absl/functional:any_invocable",
        "@com_google_absl//absl/log",
        "@com_google_absl//absl/log:check",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/synchronization",
    ],
)
