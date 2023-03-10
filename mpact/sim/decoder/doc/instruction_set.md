# MPACT-Sim Representation Independent Decoder Generator

Last updated 11/28/22

# Goals

*   Define a separation of instruction decoding into two parts. One, fully
    independent of the actual instruction encoding and formatting, that
    manipulates the simulator internal instruction representation. The other
    tasked with extracting information from the actual encoding format.
*   Provide a convenient format for describing the structure of the target
    architecture ISA in terms of instruction bundles and slots, where bundles
    are groups of instruction slots that are issued together.
*   Provide a convenient format for describing the set of instructions (opcodes)
    that may be issued in each slot.
*   Provide a convenient format for describing each instruction, its predicate,
    source and destination operands, the semantic function that implements its
    instruction semantics, and its optional disassembly format, without
    requiring description or knowledge of the underlying instruction encoding
    format.
*   Generate C++ source code that implements the representation independent part
    of the decoder and the interface that the encoding specific part of the
    decoder must implement.

# Decoding Instructions

MPACT-Sim uses instances of the Instruction class to represent individual target
architecture instructions. The Instruction class instance contains several key
items. Most importantly it stores the c++ callable for the C++ function (or
method, lambda, or other function object) that implements the semantics of an
instruction, i.e., reads operands, computes result(s), and writes to one or more
of the destination operands. It also includes pointers to source and destination
operand interfaces, through which the source and destination operand values are
read and written. These Instruction class instances are cached in a translation
cache, and reused when the same instruction is re-executed. The caching allows
the simulator to amortize the cost of decoding an instruction across many
executions.

The decoding of a target instruction extracts information from the instruction
encoding to populate fields in the Instruction class instance. Writing this code
by hand is tedious and repetitive. Similar code with slight variations has to be
written for each of the hundred(s) of instructions in typical ISAs. The textual
representation of the C++ code is also a far cry from how the instruction
information is represented in most ISA manuals. This tends to make this code bug
prone, whether it is from mistranslating the information from ISA manuals
(extracting bit-fields for instance), or faulty copy-paste-modify on the many
repeated sequences of code. These bugs are typically tedious to debug, as their
side-effects often manifest themselves as simulated code not executing quite
right, e.g., wrong branch offset, or the wrong branch instruction executes.

A hand written decoder is typically inextricably coupled to the instruction
representation, such as your standard binary instruction encoding. That makes it
more costly to repurpose the simulator to read instructions in different
formats, e.g., textual assembly.

When using a simulator to perform architectural experiments it is desirable to
have a flexible encoding scheme. Often an existing encoding scheme may not have
room for all the architectural variations that need to be explored. A simple
experiment to measure the effect of doubling the number of registers becomes
difficult, as it would likely require a redesign and/or widening of the
instruction encoding. Decoupling parts of the decoder from the instruction
representation makes it easier and cheaper in terms of engineering resources to
use the simulator for architectural exploration.

## Representation Independent vs Dependent Decoding

The instruction instance needs the following information in order to simulate an
instruction:

*   Semantic function - a C callable that implements the instruction semantics.
*   Predicate operand (optional), used to determine if the instruction should
    execute or not.
*   Source operand interfaces used by the semantic function to read instruction
    sources.
*   Destination operand interfaces used by the semantic function to write the
    results of the instruction execution.
*   Instruction size.
*   Instruction address.

The first four of these are all dependent on the type and identity of the
instruction, or the instruction opcode. In the traditional binary encoding
scheme, the opcode of an instruction may be determined from a single field
across the instruction set (rare), or a single field in sub-groups of the
instruction set combined with an instruction format specifier (more common). The
opcode may have additional constraints based on values of operand fields in the
instruction word as well, as is the case in the RiscV architecture.
Additionally, a predicate field may override any other decoding and designate an
instruction statically as a nop. On the other hand, in a proto based encoding
scheme, the opcode may be expressed as a single number (or enumeration type).

Regardless of the representation, the semantic function and the operands all
depend on the opcode. Therefore, the first step in creating a representation
independent decoder is to abstract out the parsing of the opcode to an interface
implemented by a representation specific decoder.

As mentioned above, the number and types of operands, such as register or
immediate, required by an instruction are determined by the opcode. On the other
hand, the exact value of a register number or an immediate, can only be
determined from the instruction representation. If factory methods for creating
instruction operands are implemented in the representation specific decoder, the
representation independent decoder can call these to obtain operand objects to
populate the Instruction instance.

The size of an instruction typically refers to its size in a particular
representation and again is a function of the opcode with no additional
information needed from the instruction representation.

The instruction address is not part of the decoding except as a parameter to
identify the storage location of an instruction.

# Representation Independent Decoder Description

The information necessary to generate the representation independent decoder is
contained in an MPACT-Sim ISA description file. It is processed by
`decoder_gen`, a purpose-built tool based on the [Antlr4](https://www.antlr.org)
parser generator, which reads the description and generates the appropriate
code. BUILD rules have been set up in the `mpact_sim_isa.bzl` file to make it
easy to incorporate the generated code into the simulator project for any target
architecture.

This section gives an overview of the contents of the description file and how
it fits in with MPACT-Sim and the code that gets generated. A detailed
description of the syntax is described in the next section.

## Instruction Description

Each instruction in the ISA is described separately. If the same operation, say
integer ADD, is implemented using multiple instructions, as long as they have
different encodings or different operands (for instance immediates vs registers,
unsigned immediate vs signed immediate), each such variation must have its own
description.

Each instruction description can be divided into three components. The opcode
description, the semantic function specification, and the optional disassembly
format.

### Opcode

#### Name

The opcode description specifies the name, the operands, and any child
instructions (see go/mpact-sim-overview “Modeling Instruction Issue and
Semantics”). The name of the opcode has to be unique within the ISA description.
By convention it is written in snake-case, with no capital letters, though this
is not a requirement. The name of the opcode is used to generate the name of an
entry in the enumeration class `OpcodeEnum`, by prepending “k” to the
pascal-case version of the name. That is, `add_i` becomes `kAddI`. This
enumeration class is generated in a separate .h file and is used both by the
generated code and in the representation specific decoder interface.

#### Size

Each opcode has a byte size associated with it, by default this is 1. The use of
the size is left to the simulator writer. For some ISAs it makes sense to have
the instruction size represent the PC increment when issuing instructions. For
some VLIW instructions, only the top-level bundle has an address, so the size of
the individual instructions don’t matter and can be left at 1.

#### Operands

The operands of an opcode are defined by a triplet: predicate operand, source
operand list, and destination operand list. Each individual operand is given a
name. This name is important. Just like the opcode name is used to create an
entry in an enum, each unique operand name is similarly added to an enum. There
are separate enum classes for each operand category: `PredOpEnum`,
`SourceOpEnum` and `DestOpEnum`. Just like for the OpcodeEnum class, the opcode
name gets converted to Pascal-case and prepended with a ‘k’. Thus, the operand
name “I\_imm12” gives rise to the enum class entry of `kIImm`12. Operands of the
same type (predicate/source/destination), that 1) have the same width, 2) have
the same zero/sign extension, and 3) refer to the same fields in the instruction
encoding should be given the same name. This minimizes the number of distinct
operand types and simplifies implementation of the representation specific
decoder, which will need to implement 3 methods, one for each of the predicate,
source and destination type. Each takes a value of the corresponding operand
enum class as a parameter and returns a new initialized operand object.

The source and destination operands are specified in comma separated lists of
operand names. The predicate operand is a single operand name. Each type is
separated by a ‘:’, and may be left empty if desired.

Each destination operand can be specified with an instruction latency, that is,
how many cycles the result should be buffered before being written to the
destination object (register or other simulated state). A value of zero causes
an immediate update without buffering. A value of one writes the value back at
the end of the current simulated cycle, i.e., so that instructions issued in the
next simulated cycle can see the update. A value of two writes the value back
and the end of the next simulated cycle, etc. The distinction between zero and
one is significant mostly for ISAs where instructions can be issued with
parallel semantics, e.g., instructions in a VLIW instruction word, where no
instruction can see updates from any other instruction in the same word.

The following shows an example of the opcode declaration for the 32 bit add
immediate instruction with 0 latency in the RiscV32i ISA:

```
addi [4] { /*empty*/ : rs1, I_imm12 : rd(0) }
```

#### Child Instructions

The semantics of an instruction may be divided into multiple actions that are
performed at separate times during the simulated execution of that instruction.
For instance, a load instruction is typically divided into two, the address
calculation and memory request, and the write-back of the data fetched from
memory to the register. The second (and any subsequent actions) are referred to
as child instructions of the opcode. They are allocated to separate Instruction
instances and have their own operands and semantic function. In this case,
operands can be assigned to each child instruction by using parenthesized lists
of operand triplets. For instance a RiscV32i ISA load word instruction would be
described as follows:

```
lw [4] { (/*empty*/ : rs1, I_imm12), (/*empty*/ : /*empty*/ : rd) }
```

### Semantic Function

The instruction semantic function is a C++ callable that takes a pointer to the
instruction instance that implements the semantic operation of the instruction.
That is, it reads any source operands, performs the operation, and writes out
any results to the destination operands. An important part of the decoder is to
bind the correct semantic function to each instruction instance.

To make it easier, the binding is expressed directly in the ISA description file
by adding a `semfunc` attribute to the opcode declaration. The semfunc attribute
takes a list of strings, one entry for each instruction/sub-instruction
specified, containing C++ code that is suitable to assign to the C++ callable,
including pointers to free functions, std::bind and absl::bind\_front bound
methods and functions, as well as lambdas and functors.

The example below shows the semantic functions for the above load word
instruction.

```
lw [4] { (/*empty*/ : rs1, I_imm12), (/*empty*/ : /*empty*/ : rd) },
      semfunc: "&RV32ILw", "&RV32ILwChild";
```

### Disassembly

One of the intended use cases for MPACT-Sim is modeling prototype ISAs or
prototype ISA extensions. It is not always the case that there is a full toolset
available for such architectures, including a disassembler and/or debugger.
Therefore, the addition of a disassembly capability makes a lot of sense.
MPACT-Sim comes with a simple interactive user interface to step, run, set/clear
breakpoints, read and write memory and registers, etc. More complex interfaces
can easily be built, as well as custom simulator drivers. Being able to see the
disassembly of the instruction that is being executed is very valuable, and
helps debugging should there be any suspected issues with the instruction
semantics or decoding.

The disassembly format is specified as the `disasm` opcode attribute, similarly
to the semantic function attribute. The argument to `disasm` is a list of text
strings that describes how the instruction should be disassembled. The list may
contain one or more strings that, when formatted, are concatenated to a single
disassembly string. The use of the `global disasm` widths declaration, allows
for each individual string to be left or right justified within a fixed width
field. The `global disasm` declaration takes a brace delimited list of integers,
one for each field to format. The sign of the integer specifies either left (-)
or right (+) justified, while the absolute value specifies the width (similar to
C style format strings).

E.g., the following specifies that the first string will be left justified in a
field of 18 characters wide. The remaining strings will be concatenated.

```
global disasm = {-18};
```

Any term in the string following an unescaped ‘%’ sign is interpreted to require
string substitution. Typically the string substitution is performed for operands
of the instruction. Each operand class has a `ToString()` method that returns
the preferred string representation of the operand value. For instance, register
operands return the register name, whereas immediate operands return the
immediate value. More complex formatting can be performed using a ‘%(&lt;expr>)’
construct, which allows a simple expression to be used as well as formatting the
value in hexadecimal, octal or binary.

The disassembly format applies only to the main instruction, not child
instructions.

Below is an example of the RiscV32i slli (shift left logical immediate)
instruction description including the disassembly format.

```
slli[4] { /*empty*/ : rs1, I_uimm5 : rd },
     disasm: "slli", "%rd, %rs1, 0x%(I_uimm5:x)",
     semfunc: "&RV32ISll";
```

## Slots and Bundles: Supporting VLIW Architectures

As discussed so far the instruction description easily supports single issue
ISAs, that is, where the instructions have sequential semantics, regardless of
how an implementation may issue them. Most traditional architectures fall into
this category. However, VLIW ISAs impose some additional structure on the ISA,
and the MPACT-Sim isa description language has features to support these.

### Slots

A *slot* is an instruction position in a VLIW word. In its simplest form, a VLIW
word consists of exactly one slot, and that is how traditional non-VLIW ISAs are
modeled in this description. True VLIW ISAs has a number of slots. The slots may
be identical, that is, any instruction can be issued from any slot, or they can
differ, restricting which instructions can be issued from which slots. The
MPAC-Sim isa description supports both cases.

A slot definition specifies an identifier as the slot name and an optional comma
separated list of slot names to inherit from (see below). The slot body
contains, an optional include file section, a set of `default` declarations and
an `opcodes` specification, which contains all the opcode definitions valid for
this slot.

The default declarations allow for specifying the default size of instructions,
default latency for destination operands, and default opcode attributes
(semantic function and disassembly format), so that they don’t have to be
specified in opcode descriptions except when the value differs. An example is
shown below:

```
slot riscv32i {
  includes {
    // Any include files containing definitions used in the semfunc
    // attributes.
    #include "some/include/file.h"
  }
  default size = 4;
  default latency = 0;
  default opcode =
    disasm: "Illegal instruction at 0x%(@:08x)",
    semfunc: "&RV32IllegalInstruction";
  opcodes {
    ...
  }
}
```

The ‘@’ sign in the disassembly format represents the instruction address.

The opcode specification is done as previously described.

The tool generates a separate C++ decode function for each slot type that is
used in the ISA.

#### Slot Inheritance

In some ISAs there may be a subset of instructions that can be issued from
multiple slots. Instead of requiring that the opcodes be defined anew in each
such slot, the notion of slot inheritance is introduced. A slot inheriting from
another slot inherits all of the opcodes from the base slot, except those that
are marked “deleted” in the derived slot. For instance:

```
slot base {
  opcodes {
    one [4] {};
    two [4] {};
  }
}

slot derived : base {
  opcodes {
    two = delete;  // Only inherits opcode 'one'.
  }
}
```

Slot inheritance allows the ISA to be divided into subgroups by function if so
desired. In the RiscV32G description, each subgroup of the ISA is defined in a
separate slot, and then combined in a final slot that is used in the ISA.

```
slot riscv32 : riscv32i, riscv32c, riscv32m, riscv32a, riscv32f, riscv32d, zicsr, zfencei {
 // default attributes for any instructions not otherwise matched.
 default opcode =
   disasm: "Illegal instruction at 0x%(@:08x)",
   semfunc: "&RV32IllegalInstruction";
}
```

#### Templated Slots

A base slot can also be a templated slot. This allows, for instance, destination
operand latencies to be specified as an expression involving one or more
template parameters. Inherited opcodes are then evaluated in terms of the actual
template arguments. Currently only integer valued template arguments are
supported. The syntax is unsurprisingly familiar:

```
template <int a, int b>
slot base_templated {
  opcodes {
    one [4] { : rs1, rs2 : rd(a + 1) };
    two [4] { : rs1, rs2 : rd(a + b + 1) };
  }
}

slot derived : base_templated<1, 3> {
   …
}
```

### Bundles

A traditional VLIW instruction word is a bundle of slots with instructions that
are issued at the same time. However, some VLIW ISAs go further and divide its
slots into subgroups that can be issued at different times in the pipeline, or
even with a variable delay. MPACT-Sim allows bundles to be defined. A bundle
definition specifies the name of the bundle. The bundle body has two sections:
bundles and slots, that list the names of other bundle and slot definitions that
make up the current bundle.

Each bundle definition used in the isa will have a DecodeFunction generated for
it.

### ISA

The top level of the MPACT-Sim isa specification is the “isa” definition. There
may be more than one isa definition in a .isa file. The isa for which to
generate code is specified in an option to the isa-parsing tool. The “isa”
definition specifies the name of the isa, the namespace within which the C++
code will be generated, and the set of slots and bundles which makes up the isa,
similarly to a bundle definition.

The simple isa definition for RiscV32G is shown below:

```
isa RiscV32G {
  namespace mpact::sim::riscv::isa32;
  slots { riscv32; }
}
```

### Constants

Integer typed constants can be declared both at the global level and within
slots to give names to values. These constants can be used in expressions with
other constants and integer literals wherever integer values can be used (e.g.,
instruction latencies). E.g.,

```
int global_latency = 1;

slot myslot {
  int my_latency = global_latency + 1;
  ...
}
```

### Include Files

In addition to the include files specified within each slot, as described
previously, a set of include files can also be specified at the global level.
While those specified within a slot are only added to the generated code if the
slot is reachable from the top level isa, the global include files are always
included in the generated code.

```
includes {
  #include "include/a/global/file.h"
}
```

# Detailed Syntax of .isa File

The full [Antlr4](https://www.antlr.org) grammar of the .isa file is found in
the file `InstructionSet.g4`.
