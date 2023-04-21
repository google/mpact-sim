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

workspace(name = "com_google_mpact-sim")

load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive", "http_file")

# Additional bazel rules.
http_archive(
    name = "bazel_skylib",
    sha256 = "b8a1527901774180afc798aeb28c4634bdccf19c4d98e7bdd1ce79d1fe9aaad7",
    urls = ["https://github.com/bazelbuild/bazel-skylib/releases/download/1.4.1/bazel-skylib-1.4.1.tar.gz"],
)

load("@bazel_skylib//:workspace.bzl", "bazel_skylib_workspace")

bazel_skylib_workspace()

load("//:repos.bzl", "mpact_sim_repos")

mpact_sim_repos()

load("//:deps.bzl", "mpact_sim_deps")

mpact_sim_deps()
