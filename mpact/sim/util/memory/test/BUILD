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

# Test projects for memory.

package(
    default_applicable_licenses = ["//:license"],
)

cc_test(
    name = "flat_memory_test",
    size = "small",
    srcs = ["flat_memory_test.cc"],
    deps = [
        "//mpact/sim/generic:arch_state",
        "//mpact/sim/generic:core",
        "//mpact/sim/generic:instruction",
        "//mpact/sim/util/memory",
        "@com_google_absl//absl/memory",
        "@com_google_absl//absl/strings",
        "@com_google_googletest//:gtest",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_test(
    name = "flat_demand_memory_test",
    size = "small",
    srcs = ["flat_demand_memory_test.cc"],
    deps = [
        "//mpact/sim/generic:arch_state",
        "//mpact/sim/generic:core",
        "//mpact/sim/generic:instruction",
        "//mpact/sim/util/memory",
        "@com_google_absl//absl/memory",
        "@com_google_absl//absl/strings",
        "@com_google_googletest//:gtest",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_test(
    name = "atomic_memory_test",
    size = "small",
    srcs = ["atomic_memory_test.cc"],
    deps = [
        "//mpact/sim/generic:core",
        "//mpact/sim/util/memory",
        "@com_google_absl//absl/memory",
        "@com_google_absl//absl/strings",
        "@com_google_googletest//:gtest",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_test(
    name = "memory_watcher_test",
    size = "small",
    srcs = [
        "memory_watcher_test.cc",
    ],
    deps = [
        "//mpact/sim/generic:core",
        "//mpact/sim/util/memory",
        "@com_google_absl//absl/memory",
        "@com_google_absl//absl/strings",
        "@com_google_googletest//:gtest",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_test(
    name = "tagged_flat_demand_memory_test",
    size = "small",
    srcs = ["tagged_flat_demand_memory_test.cc"],
    deps = [
        "//mpact/sim/generic:arch_state",
        "//mpact/sim/generic:core",
        "//mpact/sim/generic:instruction",
        "//mpact/sim/util/memory",
        "//mpact/sim/util/other:log_sink",
        "@com_google_absl//absl/log:log_sink_registry",
        "@com_google_absl//absl/memory",
        "@com_google_absl//absl/strings",
        "@com_google_googletest//:gtest",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_test(
    name = "atomic_tagged_memory_test",
    size = "small",
    srcs = ["atomic_tagged_memory_test.cc"],
    deps = [
        "//mpact/sim/generic:core",
        "//mpact/sim/util/memory",
        "@com_google_absl//absl/memory",
        "@com_google_absl//absl/strings",
        "@com_google_googletest//:gtest",
        "@com_google_googletest//:gtest_main",
    ],
)
