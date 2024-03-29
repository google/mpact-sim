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

# Proto's used by the simulator infrastructure to import configuration
# values and export configuration and statistics counter values.

package(
    default_applicable_licenses = ["//:license"],
    default_visibility = ["//visibility:public"],
)

proto_library(
    name = "component_data_proto",
    srcs = [
        "component_data.proto",
    ],
)

cc_proto_library(
    name = "component_data_cc_proto",
    deps = [
        ":component_data_proto",
    ],
)

filegroup(
    name = "component_data_proto_files",
    srcs = ["component_data.proto"],
)
