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

# Miscellaneous utilities.

package(
    default_applicable_licenses = ["//:license"],
    default_visibility = ["//visibility:public"],
)

cc_library(
    name = "log_sink",
    srcs = [],
    hdrs = [
        "log_sink.h",
    ],
    deps = [
        "@com_google_absl//absl/base:log_severity",
        "@com_google_absl//absl/log:log_entry",
        "@com_google_absl//absl/log:log_sink",
    ],
)

cc_library(
    name = "simple_uart",
    srcs = ["simple_uart.cc"],
    hdrs = ["simple_uart.h"],
    deps = [
        "//mpact/sim/generic:arch_state",
        "//mpact/sim/generic:core",
        "//mpact/sim/generic:instruction",
        "//mpact/sim/util/memory",
        "@com_google_absl//absl/log",
    ],
)
