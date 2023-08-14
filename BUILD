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
