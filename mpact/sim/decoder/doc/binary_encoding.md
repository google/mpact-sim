# MPACT-Sim Binary Instruction Decoder Generator

# Goals

*   Define a convenient format for describing binary instruction formats and
    instruction encodings.
*   Create an algorithm to partition the instructions into groups for faster
    decode.
*   Generate C++ code for an efficient decoder to decode the instructions in the
    description

# Overview

A binary instruction decoder is tedious and at times error prone to write by
hand. For instance, the Risc-V RV32G instruction set consists of 122 separate
instructions and over a dozen instruction formats. Other architectures have
more, such as the The TI C64+ DSP with almost 250 different instructions
(including 35 compact encodings).

While the decoder source code may be repetitive, the large number of bit-field
extractions and value comparisons makes it more likely to introduce careless
bugs that are hard to track down. Moreover, responding to changes in the
encoding formats or overall encoding scheme requires a lot of rework, and makes
binary encoding a poor choice for doing what-if analysis and architectural
exploration. However, given that it is the preferred output format for most tool
chains, it is almost without exception necessary to support.

Defining a declarative format for describing instruction encodings and providing
a tool that parses generating the C++ code to perform the instruction decoding
not only improves programmer productivity, but provides a much more readable
specification of the implementation that can more easily be compared to the ISA
manual. Changes to instruction encodings are trivial to implement, and the
resulting decoder code is correct by construction.

# Binary Instruction Description

The binary instruction description file has two main components: a set of
instruction format definitions and a set of instruction encoding definitions.
The former defines the layout of bit fields in an instruction format, as well as
overlays, which can be considered virtual fields that can be constructed by
concatenating bit constants and/or fragments of the instruction word. Overlays
are useful if there are alternative interpretations of the bits in an
instruction, or if the bits in a field have to be reordered to be interpreted
correctly. For instance, in the RiscV ISA, the 32 bit store instructions
concatenate two separate, non adjacent bit fields to form the 12 bit signed
offset used in the address calculation. Another example, also from the RiscV
ISA, are the 32 bit branch instructions. The branch offset is constructed from
two immediates, but instead of just concatenating the two fields, they are
intertwined, concatenating the two msb’s, followed by the concatenation of the
remainder of the fields, adding a constant 0 bit at the lsb, to create a byte
address.

Instruction encodings are described separately, grouped by instruction size, and
lists constraints (equal or not equal) on field values in the instruction
format.

## Instruction Formats

The instruction format definition has three components. The header defines the
format name, the bit width, and an optional base format. The base format helps
group formats together in a hierarchy. Only instruction encodings using formats
that derive from the same base can be put into the same instruction encoding
group. This allows encodings for execution mode dependent instructions to be
grouped according to mode and decoded accordingly, or merely a way of forcing
some instructions to be decoded in separate decode functions than others. An
example from RiscV RV32G is shown below.

```
format BType[32] : Inst32
```

The body of instruction formats are divided into two sections: fields and
overlays.

### Fields

The fields section lists the fields of the format in order from msb to lsb. The
total width of the fields has to equal the declared width of the format. A field
is either a bit field definition or a reference to another format. A bit field
definition consists of an `unsigned` or `signed` specification, a name, and bit
width. A format reference consists of the keyword `format` followed by the
format name, and an optional size specification, which specifies how many times
that format is repeated. The fields section of the RiscV RV32G BType format
declaration shown previously is listed below:

```
 fields:
   unsigned imm7[7];
   unsigned rs2[5];
   unsigned rs1[5];
   unsigned func3[3];
   unsigned imm5[5];
   unsigned opcode[7];
```

### Overlays

An overlay is a named virtual field of the format that consists of a sequence of
field (or format) references and binary literals in order of concatenation from
left to right. Overlays are used when the value of a desired quantity from the
instruction word is not encoded directly in a field, but has to be assembled
from the fields or formats, or fragments of fields or formats, possibly with
binary literals added to the mix. Binary literals are expressed in the form
‘0b&lt;binary digits>’, where the single quote can be used as a grouping
mechanism (e.g., `0b0100'1010`). The width of the literal is defined by the
number of binary digits following the ‘0b’ prefix.

An overlay definition consists of an `unsigned` or `signed` specification, a
name, and bit width, (just like a field specification), but then followed by an
equal sign and a comma separated list of overlay components. Each overlay
component is one of:

*   A field or format name, which means the entire field/format is used.
    *   E.g., `imm5`
*   A field or format name with a bit range, which selects only the specified
    bits.
    *   E.g., `imm5[4..1]`
*   A bit range on its own, which refers directly to the current format’s bits.
    *   E.g., `[11..8]` (which would refer to the same bits as `imm[4..1]`)
*   A binary number literal which defines both the value and the width (number
    of digits).
    *   E.g., `0b0100'1010`

The overlay section of the RiscV RV32G BType format declaration is shown below:

```
 overlays:
   signed imm[13] = imm7[6], imm5[0], imm7[5..0], imm5[4..1], 0b0;
```

## Instruction Encodings

The instruction encodings are grouped in one or more `instruction group`
definitions. Each such definition gives rise to a separate instruction decode
function in the generated code. The instructions in a single group are subject
to two constraints: they must all have the same width, and the instruction
formats used by the instructions must all derive from the same ancestor (as
specified in the group header). The user may otherwise freely select how to
group the instructions according to relevant ISA constraints, such as mode etc.

## Header

The header of the instruction group defines the name of the group, the width,
the opcode enum C++ type used for the opcodes and the base instruction format
all the grouped instructions’ formats must derive from. An example from RiscV 32
is shown below:

```
instruction group RiscVInst32[32] : Inst32Format
```

This defines “RiscVInst32” as the name of an instruction group with 32 bit wide
instructions, using the base format "Inst32Format". All the instructions listed
within have to derive from "Inst32Format".

## Instructions

The instructions in an instruction group are listed sequentially in a semicolon
separated list. Each instruction is described by its name, the instruction
format used, and a comma separated list of value constraints on fields or
overlays in that instruction.

As an example, consider the RiscV branch on equal “beq” instruction. This
instruction uses the BType format that was listed previously in the discussion
on instruction formats.

```
beq    : BType  : func3 == 0b000, opcode == 0b110'0011;
```

As can be seen, the value constraints are that the instruction field func3 has
to be equal to 0, and the opcode field has to have the value 0x63. Note, the
radix of the numeric literal in each constraint is not restricted to binary, but
can be any of binary, octal, hexadecimal or decimal. The choice is up to the
author in terms of what is most convenient.

Another example using five instructions from the 16 bit RiscV instruction group
that all use format CR illustrates how not-equal constraints can also be used :

```
format CR[16] : Inst16Format {
 fields:
   unsigned func4[4];
   unsigned rs1[5];
   unsigned rs2[5];
   unsigned op[2];
};

instructions Inst16[16] "isa::OpcodeEnum" : Inst16Format {
 ...
 cjr       : CR : func4 == 0b1000, rs1 != 0, rs2 == 0, op == 0b10;
 cmv       : CR : func4 == 0b1000, rs1 != 0, rs2 != 0, op == 0b10;
 cebreak   : CR : func4 == 0b1001, rs1 == 0, rs2 == 0, op == 0b10;
 cjalr     : CR : func4 == 0b1001, rs1 != 0, rs2 == 0, op == 0b10;
 cadd      : CR : func4 == 0b1001, rs1 != 0, rs2 != 0, op == 0b10;
 ...
};
```

## Decoder

The decoder definition specifies properties for the generated binary instruction
decoder. Multiple decoders may be specified in a .bin\_fmt file, but only one
can be selected, by passing in a command line option to the parser tool, for
parsing and code generation.

The decoder definition specifies 4 pieces of information: namespace, the opcode
enumeration type to use, decoder specific include files, and the instruction
groups for which to generate decoders for.

```
decoder RiscV32GV {
  namespace mpact::sim::riscv::encoding;
  opcode_enum = "isa32v::OpcodeEnum";
  includes {
    #include "some/include/file.h"
  }
  // Combine instruction groups RiscV32 I, M and F.
  RiscV32G = {RiscVIInst32, RiscVMInst32, RiscVFInst32};
  // Separate group for compact instructions.
  RiscVCInst16;
};
```

### Namespace

The namespace property specifies the namespace in which the generated code will
be placed.

### Opcode Enumeration Type

The `opcode_enum` specifies the text string that references the opcode
enumeration type to use as the return value for the instruction decoder(s).

The binary instruction decoder generator is designed to work with the
representation independent decoder generator (go/mpact-sim-isa-decoder). As
such, it has to translate from the binary representation of instructions to the
opcode enumeration type defined in the generated files by that tool. In order
for this to work properly, the string literal has to specify the name of the
enumeration type as it needs to be used from within the namespaces (see the
detailed grammar rules), defined for the binary decoder generator. The name of
each instruction listed in the binary encoding description file must match the
name of an instruction in the representation independent decoder description
file for the enumeration type members to match up.

### Include Files

Include files specific to this decoder may be specified here. They will be
copied to the generated code.

### Instruction Groups

The remainder specifies the named instruction groups to generate decode
functions for. Multiple instruction groups can be combined into a single group
for purposes of generating only one decode function for their combined
instructions, as opposed to having separate functions for each one. Only
instruction groups that use the same format can be combined in this way. In the
example above, three instruction groups (integer, multiply/divide, and floating
point) are combined to generate a single decoder, while a separate decoder
function is generated for the compact, 16-bit instructions.

# Generated C++ Header Code

The binary decoder generator tool generates a set of functions in the namespace
defined in the description file (see detailed syntax). As mentioned previously,
a decode function is generated for each instruction group. The signature is

```
<enum type> Decode<group name>(<word type> inst_word);
```

Where &lt;enum type> is the string literal in the instruction group definition,
&lt;group name> is the name of the instruction group, and &lt;word type> is the
smallest unsigned integer type that fits the width of the instruction. When
called, the function will return the enum member that matches the word passed in
as an argument, or the enum member kNone (always defined), if there is no match.

The tool also generates extract functions for each field and overlay in the
formats that are defined in the description file. Each such extraction function
is defined within a nested namespace named after the format in which it is
defined. The extraction functions are defined as inline in the .h file to
improve performance without forcing every function to be included in the
executable, as many of these are not used by the decoder, but should be made
available to the user if desired.

The return type is the smallest signed/unsigned (according to the field/overlay
definition) integer type that fits the width of the field/overlay. The argument
type is the smallest unsigned integer type that fits the format width. The
following example shows the RiscV BType format and the extraction functions
generated:

<table>
  <tr>
   <td><code>format BType[32] : Inst32Format {</code>
<p>
<code>  fields:</code>
<p>
<code>    unsigned imm7[7];</code>
<p>
<code>    unsigned rs2[5];</code>
<p>
<code>    unsigned rs1[5];</code>
<p>
<code>    unsigned func3[3];</code>
<p>
<code>    unsigned imm5[5];</code>
<p>
<code>    unsigned opcode[7];</code>
<p>
<code>  overlays:</code>
<p>
<code>    signed imm[13] = imm7[6], imm5[0], imm7[5..0], imm5[4..1], 0b0;</code>
<p>
<code>};</code>
   </td>
  </tr>
  <tr>
   <td><code>namespace BType {</code>
<p>
<code>inline uint8_t ExtractOpcode(uint32_t value) {</code>
<p>
<code> return value & 0x7f;</code>
<p>
<code>}</code>
<p>
<code>inline uint8_t ExtractImm7(uint32_t value) {</code>
<p>
<code> return  (value >> 25) & 0x7f;</code>
<p>
<code>}</code>
<p>
<code>inline uint8_t ExtractRs1(uint32_t value) {</code>
<p>
<code> return  (value >> 15) & 0x1f;</code>
<p>
<code>}</code>
<p>
<code>inline uint8_t ExtractImm5(uint32_t value) {</code>
<p>
<code> return  (value >> 7) & 0x1f;</code>
<p>
<code>}</code>
<p>
<code>inline uint8_t ExtractRs2(uint32_t value) {</code>
<p>
<code> return  (value >> 20) & 0x1f;</code>
<p>
<code>}</code>
<p>
<code>inline uint8_t ExtractFunc3(uint32_t value) {</code>
<p>
<code> return  (value >> 12) & 0x7;</code>
<p>
<code>}</code>
<p>
<code>inline int16_t ExtractImm(uint32_t value) {</code>
<p>
<code> int16_t result;</code>
<p>
<code> result = (value & 0x80000000) >> 19;</code>
<p>
<code> result |= (value & 0x80) &lt;< 4;</code>
<p>
<code> result |= (value & 0x7e000000) >> 20;</code>
<p>
<code> result |= (value & 0xf00) >> 7;</code>
<p>
<code> result = result &lt;< 3;</code>
<p>
<code> result = result >> 3;</code>
<p>
<code> return result;</code>
<p>
<code>}</code>
<p>
<code>}  // namespace BType</code>
   </td>
  </tr>
</table>

# Generating the Decoder Functions

A decoder function is generated for each instruction group, returning the enum
member for one of the instructions, if there is a match with that instruction,
or kNone if there is not.

## Algorithm

A new algorithm was developed to generate the decoder for the instructions in an
instruction group. The goal of the decoder is to be correct and reasonably fast
and efficient, but does not attempt to be optimal.

### Initialization

In the first stage of the algorithm only the “equal to” constraints for each
encoding are considered. The algorithm computes the mask of bits that are
constrained equal for each instruction in the instruction group. This is called
the *constraint mask* of the instruction.

### Creating Initial Encoding Groups

In the second stage of the algorithm, an initial set of *encoding groups* are
formed. The algorithm is shown below. Initially, the set of encoding groups is
empty. For each instruction encoding, consider each encoding group in turn. If
the intersection of the constraint mask of the encoding group with the
constraint mask of the encoding is zero, that is, there is no overlap between
the constrained bits in the encoding with the constrained bits in the encoding
group, then go to the next encoding group. If the intersection is non-zero,
select that encoding group.

If an encoding group was selected, add the current encoding to that group, and
update the encoding group’s constraint mask with a bitwise and of the
instruction encoding’s constraint mask. If no encoding group was selected,
create a new encoding group, setting the constraint mask of the encoding group
to that of the instruction encoding. At the end of this stage, each instruction
encoding has been assigned to an encoding group, and it is guaranteed that all
encoding groups have a non-zero constraint mask. The algorithm is shown in the
following figure.

```
for each instruction encoding enc {
  inst_enc_group = nullptr;
  for each encoding group grp {
    if ((grp->constraint_mask & enc->constraint_mask) == 0) continue
    inst_enc_group = grp
    break
  }
  if (inst_enc_group != nullptr) {
    Add enc to inst_enc_group
    inst_enc_group->constraint_mask &= enc->constraint_mask
  } else {
    Create a new encoding group enc_group
    Add enc to enc_group
    enc_group->constraint_mask = enc->constraint_mask
  }
}
```

### Subdividing Encoding Groups

Once the initial encoding groups have been formed each encoding group is
subdivided recursively into smaller encoding groups based on the “equal-to”
constraints for the instructions in the encoding group.

Three quantities are computed for each encoding group. First, the *constant
mask*, the subset of the bits in the constraint mask that have the same
constraint value across all the instruction encodings in the encoding group.
Second, the *constant value*, which is the shared value of the bits in the
constant mask. Third, the *discriminator mask*, which is the bitset difference
between the constraint mask and the constant mask. A fourth quantity is used as
well, it is computed for subgroups when an encoding group is divided. This is
the *ignore mask*, which is the set of bits that have already been handled by an
encoding group higher in the hierarchy, and no longer need to be considered. For
instance, all 32 bit instructions in the RiscV32G ISA have 0b11 as the value of
the two lsbs. Only the top level decoder function needs to check the value of
these bits, any subsequent comparisons need consider at most the remaining 28
bits.

If the number of set bits in the discriminator is 0 an encoding group cannot be
subdivided, as there are no common bits by which to separate the instructions.
Otherwise an encoding group can be subdivided into 2^(1 &lt;<
popcount(discriminator)), where popcount(x) returns the number of set bits. For
instance, if a set of instruction encodings in a group have a constraint mask of
0x7f, a constant mask of 0x3, and thus a discriminator mask of 0x7c (the
discriminator mask has five bits sets), there can be a total of 32 (2^5)
subgroups created. In other words, the encodings in the encoding group can be
divided into 32 subgroups, and the subgroup for any instruction encoding can be
looked up by an index extracted from the bits in the instruction word that
correspond to the discriminator mask.

Pseudo code for the algorithm is shown below. No subgroups are created that are
either size 0 or the size of the group itself. Moreover, no subgroups are
created if the discriminator mask has only one bit set.

```
Refine(EncodingGroup G) {
  for (int i = 0; i < (1 << popcount(G->discriminator_mask()); i++) {
    subgroup = new EncodingGroup
    subgroup->SetIgnore(G->ignore_bits() | G->discriminator_mask() |
                        G->constant_mask())
    for each encoding enc in G {
      if (discriminator_value(enc) != i) continue
      subgroup->AddEncoding(enc)
    }
    if (subgroup->empty()) {
      delete subgroup
      continue
    }
    if (subgroup->size() == G.size()) {
      delete subgroup
      return
    }
    if (popcount(subgroup->discriminator()) >= 2) {
      Refine(subgroup)
    }
  }
}
```

There is a property of encoding groups that is worth noting. If an encoding
group has no subgroups, and there are no instruction encodings in the group that
have not-equal constraints, and there are no equal constraints that involve
overlays with bit constants, then that encoding group is considered to be
*simple*. Instructions in a simple encoding group can have their opcode
determined using a lookup table without any explicit comparisons.

### Generating Code

The code generation process is the same for each instruction group listed in the
decoder definition. In addition to the top level decode function for the
instruction group, a separate decode function is generated for each encoding
group that will either directly return an opcode, or call the decode function
for a subgroup. The top level decoder function contains the code to call the top
level encoding groups’ decode function one after the other until one returns a
valid opcode value (not equal to `kNone`). If no valid opcode is found, `kNone`
is returned. If there is only a single top level encoding group the function is
still emitted as described. A future optimization would be to inline the next
level decode function directly instead. Below is the top level decode function
generated for RiscV32G `Inst32` instruction group:

```
isa::OpcodeEnum DecodeRiscVInst32(uint32_t inst_word) {
  isa::OpcodeEnum opcode;
  opcode = DecodeRiscVInst32_0(inst_word);
  if (opcode != isa::OpcodeEnum::kNone) return opcode;
  return opcode;
}
```

The decode functions for every encoding group follow the same pattern. First if
there is a non-zero constant\_mask for the encoding group the mask is applied to
the instruction word and the value is compared to the constant\_value. If the
values are not equal, the decode function returns `kNop`. If the encoding group
has subgroups, then the index value corresponding to the discriminator\_mask is
computed and used to select and call the function pointer for the next level
decode function, returning its return value. The RiscV32G 0 level decode
function is shown below:

```
isa::OpcodeEnum DecodeRiscVInst32_0(uint32_t inst_word) {
  if ((inst_word & 0x3) != 0x3) return isa::OpcodeEnum::kNone;
  uint32_t index;
  index = (inst_word >> 2) & 0x1f;
  return parse_group_RiscVInst32_0[index](inst_word);
}
```

The `parse_group_*` arrays are defined in the `.cc` file, and initialized with
the appropriate function pointers for the different encoding groups.

For leaf encoding groups that are simple, the decode function is almost
identical to the above function, except that the index is used to select from an
opcode array, defined static constexpr local to the decode function. The
following shows such a function for RiscV32 32 bit store instructions:

```
isa::OpcodeEnum DecodeRiscVInst32_0_8(uint32_t inst_word) {
 static isa::OpcodeEnum opcodes[4] = {
   isa::OpcodeEnum::kSb,
   isa::OpcodeEnum::kSh,
   isa::OpcodeEnum::kSw,
   isa::OpcodeEnum::kNone,
 };
 if ((inst_word & 0x4000) != 0x0) return isa::OpcodeEnum::kNone;
 uint32_t index;
 index = (inst_word >> 12) & 0x3;
 return opcodes[index];
}
```

For non-simple leaf decode functions, the code starts with field extractions
required for the constraints testing, then each opcode is considered in turn, as
shown below.

```
isa::OpcodeEnum DecodeRiscVInst16_0_12(uint16_t inst_word) {
 uint16_t index;
 index = (inst_word >> 12) & 0x1;
 uint16_t rs1_value = (inst_word >> 7) & 0x1f;
 uint16_t rs2_value = (inst_word >> 2) & 0x1f;
 if ((index == 0x0) && (rs1_value != 0x0) && (rs2_value != 0x0))
   return isa::OpcodeEnum::kCmv;
 if ((index == 0x0) && (rs2_value == 0x0) && (rs1_value != 0x0))
   return isa::OpcodeEnum::kCjr;
 if ((index == 0x1) && (rs1_value == 0x0) && (rs2_value == 0x0))
   return isa::OpcodeEnum::kCebreak;
 if ((index == 0x1) && (rs1_value != 0x0) && (rs2_value != 0x0))
   return isa::OpcodeEnum::kCadd;
 if ((index == 0x1) && (rs2_value == 0x0) && (rs1_value != 0x0))
   return isa::OpcodeEnum::kCjalr;
 return isa::OpcodeEnum::kNone;
}
```

# Detailed Syntax of the .bin File

The full [Antlr4](https://www.antlr.org) grammar of the .isa file is found in
the file `BinFormat.g4)`.
