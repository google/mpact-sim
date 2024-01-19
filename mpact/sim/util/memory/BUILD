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

# Convenient memory interface and implementation classes.

package(
    default_applicable_licenses = ["//:license"],
    default_visibility = ["//visibility:public"],
)

cc_library(
    name = "memory",
    srcs = [
        "atomic_memory.cc",
        "flat_demand_memory.cc",
        "flat_memory.cc",
        "memory_watcher.cc",
        "tagged_flat_demand_memory.cc",
    ],
    hdrs = [
        "atomic_memory.h",
        "flat_demand_memory.h",
        "flat_memory.h",
        "memory_interface.h",
        "memory_watcher.h",
        "tagged_flat_demand_memory.h",
        "tagged_memory_interface.h",
    ],
    copts = ["-O3"],
    deps = [
        "//mpact/sim/generic:core",
        "//mpact/sim/generic:instruction",
        "@com_google_absl//absl/base:core_headers",
        "@com_google_absl//absl/container:btree",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/container:flat_hash_set",
        "@com_google_absl//absl/functional:any_invocable",
        "@com_google_absl//absl/log",
        "@com_google_absl//absl/numeric:bits",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
    ],
)
