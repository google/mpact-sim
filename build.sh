#!/bin/bash

# Fail on any error
set -e

# Display commands being run
# Note, only use if necessary for debugging, and be careful about handling of
# any credentials.
# set -x

# Build all targets
CC=clang BAZEL_CXXOPTS=-std=c++17 bazel build ...:all

# Test all targets
CC=clang BAZEL_CXXOPTS=-std=c++17 bazel test ...:all
