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

# Setup dependent repositories.
load("//:repos.bzl", "mpact_sim_repos")

mpact_sim_repos()

load("//:deps.bzl", "mpact_sim_deps")

mpact_sim_deps()
