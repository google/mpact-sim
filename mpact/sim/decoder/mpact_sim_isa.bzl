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

# Description:
#   The decoder generator tool for MPACT-Sim. This consists of two rules for
#   to generate C++ code from the antlr4 grammar InstructionSet.g4
#   (antlr4_cc_parser, antlr4_cc_lexer), a cc_library for processing the
#   input file, and a cc_main for the top level executable.

"""Build rules to create C++ code from an Isa.g4 decoder grammar."""

def mpact_cc_library(name, srcs = [], hdrs = [], copts = [], deps = [], features = [], visibility = []):
    native.cc_library(
        name = name,
        srcs = srcs,
        hdrs = hdrs,
        copts = copts + ["-iquote $(BINDIR)/external/org_antlr4_cpp_runtime/antlr4/include/antlr4-runtime"],
        deps = deps + ["@org_antlr4_cpp_runtime//:antlr4"],
        visibility = visibility,
        features = features,
    )

def mpact_cc_binary(name, srcs = [], copts = [], deps = [], features = [], visibility = []):
    native.cc_binary(
        name = name,
        srcs = srcs,
        copts = copts + ["-iquote $(BINDIR)/external/org_antlr4_cpp_runtime/antlr4/include/antlr4-runtime"],
        deps = deps + ["@org_antlr4_cpp_runtime//:antlr4"],
        visibility = visibility,
        features = features,
    )

def mpact_cc_test(name, size = "small", srcs = [], deps = [], copts = [], data = []):
    native.cc_test(
        name = name,
        size = size,
        copts = copts + ["-iquote $(BINDIR)/external/org_antlr4_cpp_runtime/antlr4/include/antlr4-runtime"],
        srcs = srcs,
        deps = deps + ["@org_antlr4_cpp_runtime//:antlr4"],
        data = data,
    )

def mpact_isa_decoder(name, includes, src = "", srcs = [], deps = [], isa_name = "", prefix = ""):
    """Generates the C++ source corresponding to an MPACT Isa decoder definition.

    Args:
      name: The name of the package to use for the cc_library.
      src:  The .isa file containing the Isa rules (or use srcs).
      srcs: The .isa files containing the Isa rules (if src is not specified).
      deps: Dependencies for the cc_library.
      includes: Include .isa files.
      isa_name: Name of isa to generate code for.
      prefix: File prefix for the generated files (otherwise uses base name of src file
    """

    # if src is not empty, prepend it to the srcs list.
    if src != "":
        isa_srcs = [src] + srcs
    else:
        isa_srcs = srcs

    for f in isa_srcs:
        if not f.endswith(".isa"):
            fail("Grammar file '" + f + "' must end with .isa", "src")

    if prefix == "":
        file_prefix = isa_srcs[0][:-4]
        base_file_prefix = _strip_path(file_prefix)
    else:
        base_file_prefix = prefix

    # The names of the files generated by this rule are based on the source .isa file name.
    out_files = [
        "%s_decoder.h" % base_file_prefix,
        "%s_decoder.cc" % base_file_prefix,
        "%s_enums.h" % base_file_prefix,
        "%s_enums.cc" % base_file_prefix,
    ]

    # The command to generate the files.
    command = ";\n".join([
        _make_isa_tool_invocation_command(len(isa_srcs), base_file_prefix, isa_name),
    ])

    # The rule for the generated sources.
    native.genrule(
        name = name + "_source",
        # Add includes to isa_srcs to ensure they are added to the blaze build sandbox where
        # they can be found.
        srcs = isa_srcs + includes,
        outs = out_files,
        cmd = command,
        heuristic_label_expansion = 0,
        tools = ["@@com_google_mpact-sim//mpact/sim/decoder:decoder_gen"],
    )

    # The rule for the lib that is built from the generated sources.
    native.cc_library(
        name = name,
        srcs = [f for f in out_files if f.endswith(".cc")],
        hdrs = [f for f in out_files if f.endswith(".h")],
        deps = [
            "@@com_google_mpact-sim//mpact/sim/generic:arch_state",
            "@@com_google_mpact-sim//mpact/sim/generic:instruction",
            "@com_google_absl//absl/container:flat_hash_map",
            "@com_google_absl//absl/strings:str_format",
        ] + deps,
    )

def mpact_bin_fmt_decoder(name, includes, src = "", srcs = [], deps = [], decoder_name = "", prefix = ""):
    """Generates the C++ source corresponding to an MPACT Bin Format decoder definition.

    Args:
      name: The name of the package to use for the cc_library.
      includes: Include .bin_fmt files.
      src:  The .bin_fmt file containing the Bin Format rules.
      srcs: List of .bin_fmt files containing the Bin Format rules.
      deps: Dependencies for the cc_library
      decoder_name: Name of decoder from .bin_fmt file to generate
      prefix: File prefix for the generated files (otherwise uses base name of src file
    """

    if src != "":
        bin_srcs = [src] + srcs
    else:
        bin_srcs = srcs

    for f in bin_srcs:
        if not f.endswith(".bin_fmt"):
            fail("Grammar file '" + f + "' must end with .bin_fmt", "srcs")

    if prefix == "":
        file_prefix = src[:-8]
        base_file_prefix = _strip_path(file_prefix)
    else:
        base_file_prefix = prefix

    # The names of the files generated by this rule are based on the source .isa file name.
    out_files = [
        "%s_bin_decoder.h" % base_file_prefix,
        "%s_bin_decoder.cc" % base_file_prefix,
    ]

    # The command to generate the files.
    command = ";\n".join([
        _make_bin_tool_invocation_command(len(bin_srcs), base_file_prefix, decoder_name),
    ])

    # The rule for the generated sources.
    native.genrule(
        name = name + "_source",
        # Add includes to srcs to ensure they are added to the blaze build sandbox where
        # they can be found.
        srcs = [src] + includes,
        outs = out_files,
        cmd = command,
        heuristic_label_expansion = 0,
        tools = ["@@com_google_mpact-sim//mpact/sim/decoder:bin_format_gen"],
    )

    # The rule for the lib that is built from the generated sources.
    native.cc_library(
        name = name,
        srcs = [f for f in out_files if f.endswith(".cc")],
        hdrs = [f for f in out_files if f.endswith(".h")],
        deps = [
            "@@com_google_mpact-sim//mpact/sim/generic:arch_state",
            "@@com_google_mpact-sim//mpact/sim/generic:instruction",
            "@com_google_absl//absl/container:flat_hash_map",
            "@com_google_absl//absl/functional:any_invocable",
            "@com_google_absl//absl/strings:str_format",
        ] + deps,
    )

# Strip any path component from text. Return only the string that follows the last "/".
def _strip_path(text):
    pos = text.rfind("/")
    if pos == -1:
        return text
    return text[pos + 1:]

# Create the decoder_gen command with arguments, Since the srcs had all the
# files, including those that will be included, the command includes creating
# a bash array from $(SRCS), then instead of using $(SRCS) in the command, it
# uses only the first element of that array.
def _make_isa_tool_invocation_command(num_srcs, prefix, isa_name):
    cmd = "ARR=($(SRCS)); $(location //external:decoder_gen) "

    # Add the sources that are not in includes.
    for i in range(0, num_srcs):
        cmd += "$${ARR[" + str(i) + "]} "
    cmd += "--prefix " + prefix + " --output_dir $(@D)"
    if isa_name != "":
        cmd += " --isa_name " + isa_name

    return cmd

# Create the bin_format_gen command with arguments, Since the srcs had all the files, including
# those that will be included, the command includes creating a bash array from $(SRCS), then
# instead of using $(SRCS) in the command, it uses only the first element of that array.
def _make_bin_tool_invocation_command(num_srcs, prefix, decoder_name):
    cmd = "ARR=($(SRCS)); $(location //external:bin_format_gen) "

    # Add the sources that are not in includes.
    for i in range(0, num_srcs):
        cmd += "$${ARR[" + str(i) + "]} "
    cmd += " --prefix " + prefix + " --output_dir $(@D)"
    if decoder_name != "":
        cmd += " --decoder_name " + decoder_name
    return cmd
