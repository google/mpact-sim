#!/bin/bash

# Fail on any error
set -e

cd "${KOKORO_ARTIFACTS_DIR}/git/mpact-sim"
./build.sh
