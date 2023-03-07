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

"""Build rules to create C++ code from an Antlr4 grammar."""

def antlr4_cc_lexer(
        name,
        src,
        namespaces = None,
        deps = None):
    """Generates the C++ source corresponding to an antlr4 lexer definition.

    Args:
      name: The name of the package to use for the cc_library.
      src: The antlr4 g4 file containing the lexer rules.
      namespaces: The namespace used by the generated files. Uses an array to
        support nested namespaces. Defaults to [name].
      deps: Dependencies for the generated code.
    """
    namespaces = namespaces or [name]
    deps = deps or []
    if not src.endswith(".g4"):
        fail("Grammar must end with .g4", "src")
    file_prefix = _label_basename(src[:-3])
    base_file_prefix = _strip_end(file_prefix, "Lexer")
    out_files = [
        "%sLexer.h" % base_file_prefix,
        "%sLexer.cpp" % base_file_prefix,
    ]
    command = ";\n".join([
        # Use the first namespace, we'll add the others afterwards.
        _make_tool_invocation_command(namespaces[0]),
        _make_namespace_adjustment_command(namespaces, out_files),
        _make_google3_cleanup_command(out_files),
    ])
    native.genrule(
        name = name + "_source",
        srcs = [src],
        outs = out_files,
        cmd = command,
        heuristic_label_expansion = 0,
        tools = [Label("@org_antlr_tool//file")],
    )
    native.cc_library(
        name = name,
        srcs = [f for f in out_files if f.endswith(".cpp")],
        hdrs = [f for f in out_files if f.endswith(".h")],
        deps = [Label("@org_antlr4_cpp_runtime//:antlr4")] + deps,
        copts = [
            "-fexceptions",
            "-iquote $(BINDIR)/external/org_antlr4_cpp_runtime/antlr4/include/antlr4-runtime",
        ],
        features = ["-use_header_modules"],  # Incompatible with -fexceptions.
    )

def antlr4_cc_parser(
        name,
        src,
        namespaces = None,
        listener = True,
        visitor = False,
        deps = None):
    """Generates the C++ source corresponding to an antlr4 parser definition.

    Args:
      name: The name of the package to use for the cc_library.
      src: The antlr4 g4 file containing the parser rules.
      namespaces: The namespace used by the generated files. Uses an array to
        support nested namespaces. Defaults to [name].
      listener: Whether or not to include listener generated files.
      visitor: Whether or not to include visitor generated files.
      deps: Dependencies for the generated code.
    """
    suffixes = ()
    if listener:
        suffixes += (
            "%sBaseListener.cpp",
            "%sListener.cpp",
            "%sBaseListener.h",
            "%sListener.h",
        )
    if visitor:
        suffixes += (
            "%sBaseVisitor.cpp",
            "%sVisitor.cpp",
            "%sBaseVisitor.h",
            "%sVisitor.h",
        )
    namespaces = namespaces or [name]
    deps = deps or []
    if not src.endswith(".g4"):
        fail("Grammar must end with .g4", "src")
    file_prefix = _label_basename(src[:-3])
    base_file_prefix = _strip_end(file_prefix, "Parser")
    out_files = [
        "%sParser.h" % base_file_prefix,
        "%sParser.cpp" % base_file_prefix,
    ] + _make_outs(file_prefix, suffixes)
    command = ";\n".join([
        # Use the first namespace, we'll add the others afterward.
        _make_tool_invocation_command(namespaces[0], listener, visitor),
        _make_namespace_adjustment_command(namespaces, out_files),
        _make_google3_cleanup_command(out_files),
    ])
    native.genrule(
        name = name + "_source",
        srcs = [src],
        outs = out_files,
        cmd = command,
        heuristic_label_expansion = 0,
        tools = [Label("@org_antlr_tool//file")],
    )
    native.cc_library(
        name = name,
        srcs = [f for f in out_files if f.endswith(".cpp")],
        hdrs = [f for f in out_files if f.endswith(".h")],
        deps = [Label("@org_antlr4_cpp_runtime//:antlr4")] + deps,
        copts = [
            "-fexceptions",
            "-Wno-nonnull",
            "-iquote $(BINDIR)/external/org_antlr4_cpp_runtime/antlr4/include/antlr4-runtime",
        ],
        features = ["-use_header_modules"],
    )

def _make_tool_invocation_command(package, listener = False, visitor = False):
    return "java -jar $(location @org_antlr_tool//file) " + \
           "$(SRCS)" + \
           (" -visitor" if visitor else " -no-visitor") + \
           (" -listener" if listener else " -no-listener") + \
           " -Dlanguage=Cpp" + \
           " -package " + package + \
           " -o $(@D)" + \
           " -Xexact-output-dir"

def _make_google3_cleanup_command(out_files):
    # Clean up imports and usage of the #pragma once directive.
    return ";\n".join([
        ";\n".join([
            "sed -i '0,/antlr4-runtime.h/s//antlr4-runtime\\/antlr4-runtime.h/' $(@D)/%s" % filepath,
            "grep -q '^#pragma once' $(@D)/%s && echo '\n#endif  // %s\n' >> $(@D)/%s" % (filepath, _to_c_macro_name(filepath), filepath),
            "sed -i '0,/#pragma once/s//#ifndef %s\\n#define %s/' $(@D)/%s" % (_to_c_macro_name(filepath), _to_c_macro_name(filepath), filepath),
        ])
        for filepath in out_files
    ])

def _make_namespace_adjustment_command(namespaces, out_files):
    if len(namespaces) == 1:
        return "true"
    commands = []
    extra_header_namespaces = "\\\n".join(["namespace %s {" % namespace for namespace in namespaces[1:]])
    for filepath in out_files:
        if filepath.endswith(".h"):
            commands.append("sed -i '/namespace %s {/ a%s' $(@D)/%s" % (namespaces[0], extra_header_namespaces, filepath))
            for namespace in namespaces[1:]:
                commands.append("sed -i '/}  \\/\\/ namespace %s/i}  \\/\\/ namespace %s' $(@D)/%s" % (namespaces[0], namespace, filepath))
        else:
            commands.append("sed -i 's/using namespace %s;/using namespace %s;/' $(@D)/%s" % (namespaces[0], "::".join(namespaces), filepath))
    return ";\n".join(commands)

def _make_outs(file_prefix, suffixes):
    return [file_suffix % file_prefix for file_suffix in suffixes]

def _strip_end(text, suffix):
    if not text.endswith(suffix):
        return text
    return text[:len(text) - len(suffix)]

def _to_c_macro_name(filename):
    # Convert the filenames to a format suitable for C preprocessor definitions.
    char_list = [filename[i].upper() for i in range(len(filename))]
    return "ANTLR4_GEN_" + "".join(
        [a if (("A" <= a) and (a <= "Z")) else "_" for a in char_list],
    )

def _label_basename(label):
    """Returns the basename of a Blaze label."""
    return label.split(":")[-1]
