# MPACT-Sim - Retargetable Instruction Set Simulator Infrastructure

This repository contains the C++ code for MPACT-Sim, tools and
library support for making it simple to create easily retargetable
simulators for a wide range of instruction set architectures.

There are three major components. The first is the isa and binary decoder
generators, that take in descriptors of the isa and the binary encoding
of instructions and produce C++ code used in writing a decoder. The
second is the generic classes that implement the instruction objects,
registers, and other simulated state objects, and the code necessary to
manipulate them, as well as supporting writing the semantics of the
instructions. Lastly, there are utility classes that make it easier to
put together a finished simulator, including memory classes and an ELF
loader.

## Directory Listing

*   mpact/sim/decoder <br />
    This directory contains the code that implements the tools
    that parse `.isa` and `.bin_fmt` files, which define the
    instructions and instruction encodings of an ISA, and generate C++
    code for the instruction decoder for an MPACT-Sim simulator.

    The tools are built automatically and the process is largly invisible
    when using bazel with the appropriate build targets - see the tests
    in mpact/sim/decoder/test for examples.

*   mpact/sim/generic <br />
    This directory contains alll the generic classes provided for use
    in writing MPACT-Sim instruction set simulators.

*   mpact/sim/util <br />
    This directory contains code for a generic ELF program loader, and
    for a set of classes that can be used to implement memories used in
    an instruction set simulator.  