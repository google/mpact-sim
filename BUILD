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

# Top level BUILD file for mpact_sim

load("//tools/build_defs/build_test:build_test.bzl", "build_test")
load("@rules_license//rules:license.bzl", "license")

package(
    default_applicable_licenses = [":license"],
    default_visibility = ["//visibility:public"],
)

license(
    name = "license",
    package_name = "mpact-sim",
)

licenses(["notice"])

exports_files(["LICENSE"])

build_test(
    name = "kelvin_sim_build",
    targets = [
        "//learning/brain/research/kelvin/sim:kelvin_sim",
    ],
)
