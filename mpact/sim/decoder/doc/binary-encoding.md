# Writing Binary Encoding Descriptions in MPACT-Sim (`.bin_fmt`)

This guide explains how to write **binary encoding description files**
(`.bin_fmt`) in MPACT-Sim using the `BinFormat.g4` grammar and the
`bin_format_gen` code generator. It is written for developers who are new to
MPACT-Sim and want to build an instruction decoder (or encoder) for a scalar,
RISC, CISC-like fixed-word, or VLIW architecture.

For the companion guide on describing instruction semantics, operands, slots,
and bundles, see [instruction-set.md](instruction-set.md).

--------------------------------------------------------------------------------

## 1. Big Picture: How MPACT-Sim Decodes Binary Instructions

Writing a binary instruction decoder by hand in C++—shifting, masking,
sign-extending scattered immediate bits, and matching hundreds of opcodes—is
repetitive and error-prone. MPACT-Sim automates this by splitting instruction
decoding into **two declarative domain-specific languages** plus a small C++
adapter class:

1.  **The Binary Encoding Description (`.bin_fmt`, this guide)**: Describes *how
    bits are laid out in memory*:
    *   **Formats** (`format`): How an instruction word or VLIW bundle is
        divided into bitfields, nested sub-formats, and reassembled virtual
        fields (**overlays**).
    *   **Instruction Groups** (`instruction group`): Which bit-pattern
        constraints (`opcode == 0x33`, `func3 == 0`, `rs1 != 0`, etc.) identify
        each instruction opcode.
    *   **Decoder Target** (`decoder`): Which C++ namespace, `OpcodeEnum` type,
        and instruction groups to generate C++ code for.
2.  **The ISA Description (`.isa`, see
    [instruction-set.md](instruction-set.md))**: Describes *what each
    instruction does* independently of its bit encoding: slots, VLIW bundles,
    source/destination/predicate operands, latencies, C++ semantic functions,
    disassembly strings, and hardware resources.
3.  **Your Encoding Adapter Class (`<IsaName>EncodingBase` subclass)**: A small
    C++ class you write that bridges the two generated libraries: it passes a
    fetched instruction word to the generated `Decode<Group>()` function to get
    the `OpcodeEnum`, and uses the generated `Extract<Field>()` functions to
    construct register and immediate operands when requested by the `.isa`
    decoder.

```
 Raw Instruction Bits (uint16_t / uint32_t / uint64_t / uint8_t[])
        |
        v
 +------------------------------------------------------------------+
 | Generated from .bin_fmt (bin_format_gen)                         |
 |  - Decode<Group>(inst_word)        -> OpcodeEnum                 |
 |  - <fmt>::Extract<Field>(inst_word)-> integer / sub-format bits  |
 |  - Encoder::<Fmt>::Insert<Field>() -> bit insertion for encoding |
 +------------------------------------------------------------------+
        |
        v
 +------------------------------------------------------------------+
 | Your C++ Subclass of <IsaName>EncodingBase                       |
 |  - GetOpcode(...)      returns decoded OpcodeEnum                |
 |  - GetSource(...)      uses Extract* to build SourceOperands     |
 |  - GetDestination(...) uses Extract* to build DestOperands       |
 +------------------------------------------------------------------+
        |
        v
 +------------------------------------------------------------------+
 | Generated from .isa (decoder_gen)                                |
 |  - Builds mpact::sim::generic::Instruction* with semantic        |
 |    functions, operands, latencies, disassembly, and attributes   |
 +------------------------------------------------------------------+
```

--------------------------------------------------------------------------------

## 2. Quickstart: Your First `.bin_fmt` File

Suppose we want to decode a tiny 32-bit RISC architecture (`MiniRisc`) with
three instructions:

*   `add rd, rs1, rs2` (R-type format)
*   `addi rd, rs1, imm12` (I-type format, 12-bit signed immediate)
*   `beq rs1, rs2, offset13` (B-type format, where a 13-bit signed PC-relative
    branch offset has bits scattered across two fields and a constant `0` in
    bit 0)

### Step 1: Write `mini_risc.bin_fmt`

```c
// mini_risc.bin_fmt

// 1. Define a common 32-bit base format for the instruction group.
format Inst32[32] {
  fields:
    unsigned upper[25];
    unsigned opcode[7];
};

// 2. Define concrete 32-bit formats that inherit from Inst32.
//    Fields are listed from most-significant bit (bit 31) down to bit 0,
//    and their widths must sum to 32.
format RType[32] : Inst32 {
  fields:
    unsigned funct7[7];   // bits [31..25]
    unsigned rs2[5];      // bits [24..20]
    unsigned rs1[5];      // bits [19..15]
    unsigned funct3[3];   // bits [14..12]
    unsigned rd[5];       // bits [11..7]
    unsigned opcode[7];   // bits [6..0]
};

format IType[32] : Inst32 {
  fields:
    signed imm12[12];     // bits [31..20] (automatically sign-extended!)
    unsigned rs1[5];      // bits [19..15]
    unsigned funct3[3];   // bits [14..12]
    unsigned rd[5];       // bits [11..7]
    unsigned opcode[7];   // bits [6..0]
};

format BType[32] : Inst32 {
  fields:
    unsigned imm7[7];     // bits [31..25]
    unsigned rs2[5];      // bits [24..20]
    unsigned rs1[5];      // bits [19..15]
    unsigned funct3[3];   // bits [14..12]
    unsigned imm5[5];     // bits [11..7]
    unsigned opcode[7];   // bits [6..0]
  overlays:
    // Reassemble the 13-bit signed branch offset from scattered bits + LSB 0:
    signed b_imm[13] = imm7[6], imm5[0], imm7[5..0], imm5[4..1], 0b0;
};

// 3. Define the instruction encodings and field constraints.
instruction group MiniRisc32[32] : Inst32 {
  add  : RType : funct7 == 0b000'0000, funct3 == 0b000, opcode == 0b011'0011;
  addi : IType : funct3 == 0b000, opcode == 0b001'0011;
  beq  : BType : funct3 == 0b000, opcode == 0b110'0011;
};

// 4. Configure the generated C++ binary decoder.
decoder MiniRiscBinDecoder {
  namespace tutorial::mini_risc::encoding;
  opcode_enum = "::tutorial::mini_risc::isa::OpcodeEnum";
  includes {
    #include "mini_risc_enums.h"
  }
  MiniRisc32;
};
```

### Step 2: Add the Bazel Build Rule (`BUILD`)

Use `mpact_bin_fmt_decoder` (from `mpact_sim_isa.bzl`) alongside
`mpact_isa_decoder`:

```python
load(
    "//third_party/mpact_sim/decoder:mpact_sim_isa.bzl",
    "mpact_bin_fmt_decoder",
    "mpact_isa_decoder",
)
# Note: In standalone Bazel / GitHub builds, load from:
# "@mpact-sim//mpact/sim/decoder:mpact_sim_isa.bzl"

mpact_isa_decoder(
    name = "mini_risc_isa",
    src = "mini_risc.isa",
    includes = [],
    isa_name = "MiniRisc",
    prefix = "mini_risc",
)

mpact_bin_fmt_decoder(
    name = "mini_risc_bin_fmt",
    src = "mini_risc.bin_fmt",
    decoder_name = "MiniRiscBinDecoder",
    includes = [],
    prefix = "mini_risc",
    deps = [":mini_risc_isa"],
)
```

### Step 3: Use the Generated Functions in C++

Building `:mini_risc_bin_fmt` generates `mini_risc_bin_decoder.h` (plus `.cc`,
`_bin_encoder.{h,cc}`, and `_bin_types.h`). In your C++ encoding class, you can
immediately decode words and extract bitfields without writing a single
bit-shift by hand:

```cpp
#include "mini_risc_bin_decoder.h"
#include "mini_risc_enums.h"

void ParseInstruction(uint32_t inst_word) {
  using ::tutorial::mini_risc::isa::OpcodeEnum;
  namespace enc = ::tutorial::mini_risc::encoding;

  // Decode 32-bit word into OpcodeEnum::kAdd, kAddi, kBeq, or kNone:
  OpcodeEnum op = enc::DecodeMiniRisc32(inst_word);

  // Extract bitfields and overlays (signed fields/overlays are sign-extended!):
  uint8_t rd = enc::r_type::ExtractRd(inst_word);
  int16_t simm12 = enc::i_type::ExtractImm12(inst_word);
  int16_t branch_offset = enc::b_type::ExtractBImm(inst_word);
}
```

--------------------------------------------------------------------------------

## 3. File Structure, Comments, and Modularity

### Comments and Numeric Literals

`.bin_fmt` files support standard C/C++ comments and four integer radixes:

*   **Comments**: Single-line `// ...` and block `/* ... */` comments.
*   **Numbers**:
    *   Binary: `0b1100011` or `0b110'0011`
    *   Hexadecimal: `0x7f` or `0xdead'beef`
    *   Octal: `077`
    *   Decimal: `0`, `32`, `1'024`
    *   Single quotes (`'`) may appear anywhere after the first digit as visual
        digit separators.

### Splitting Descriptions Across Files (`#include` and `#once`)

Large architectures (for example, RISC-V RV32G with I, M, A, F, D, C, and Vector
extensions) can split formats and instruction groups across multiple `.bin_fmt`
files:

*   Place `#once` at the very top of a shared `.bin_fmt` file (before any
    declarations) so that including it from multiple files only processes its
    contents once.
*   Use `#include "path/to/other.bin_fmt"` at top level to import formats,
    instruction groups, or decoders from another file.
*   Remember to list all included `.bin_fmt` files in the `includes = [...]`
    attribute of `mpact_bin_fmt_decoder` in your `BUILD` file so Bazel makes
    them available in the build action.

```c
// rv_base_formats.bin_fmt
#once

format Inst32[32] {
  fields:
    unsigned payload[25];
    unsigned opcode[7];
};
```

```c
// rv_top.bin_fmt
#include "path/to/rv_base_formats.bin_fmt"
```

**Important distinction**: Top-level `#include "foo.bin_fmt"` imports another
binary format file during `bin_format_gen` parsing. By contrast,
`#include "foo.h"` *inside* a `decoder { includes { ... } }` block emits a C++
`#include` directive into the generated `.h` file.

--------------------------------------------------------------------------------

## 4. Defining Formats (`format`)

A `format` specifies how the bits of an instruction word, slot, or VLIW bundle
are partitioned.

### Syntax

```c
format FormatName[width] : BaseFormat {
  layout: default;   // Optional: 'default' or 'packed_struct'
  fields:
    // Signed/unsigned bitfields and sub-formats, from MSB to LSB
  overlays:          // Optional section
    // Virtual bitfields assembled from slices and binary literals
};
```

*(The trailing `;` after `}` is optional.)*

### Bit Numbering and Field Ordering

Understanding bit order in `BinFormat` prevents off-by-one bugs:

1.  **Declaration order is MSB to LSB (left to right)**: The first field
    declared in `fields:` occupies the highest-numbered (most-significant) bits
    of the format; the last field declared occupies the lowest-numbered bits
    ending at bit `0`.
2.  **Bit `0` is always the LSB**: Within any `W`-bit format, field, or
    sub-format, the most-significant bit is bit `W - 1` and the
    least-significant bit is bit `0`.
3.  **Exact width sum**: The widths of all entries in `fields:` **must sum
    exactly** to the width of the format. Every bit in the format must be
    accounted for—name unused bits `reserved` or `unused` rather than omitting
    them.

For example, in a 16-bit format:

```c
format CR[16] {
  fields:
    unsigned func4[4];  // bits [15..12]
    unsigned rs1[5];    // bits [11..7]
    unsigned rs2[5];    // bits [6..2]
    unsigned op[2];     // bits [1..0]
};
// Total width = 4 + 5 + 5 + 2 = 16 bits.
```

### Bitfields (`signed` and `unsigned`)

A scalar bitfield has the form:

```c
signed   field_name[width];
unsigned field_name[width];
```

*(An optional `= default_number` suffix such as `unsigned zero[3] = 0;` is also
accepted by the grammar for documentation/defaulting, though encoding
constraints are specified in `instruction group` definitions.)*

*   **`unsigned`**: The generated `Extract<Field>()` function shifts and masks
    the field and returns an unsigned C++ integer (`uint8_t`, `uint16_t`,
    `uint32_t`, or `uint64_t`).
*   **`signed`**: The generated `Extract<Field>()` function shifts, masks, and
    **sign-extends** the `width`-bit two's-complement value into a signed C++
    integer (`int8_t`, `int16_t`, `int32_t`, or `int64_t`).
*   Scalar fields that require generated extractors must be at most **64 bits**
    wide. (Unreferenced padding fields in wide formats may exceed 64 bits as
    long as no extractor is needed, though splitting into `<= 64`-bit chunks is
    good practice.)

### Sub-Formats and Replicated Slots (Format Hierarchies)

Instead of `signed`/`unsigned`, a field entry can embed another `format`,
optionally repeated `[count]` times:

```c
format SubFormatName instance_name;
format SubFormatName instance_name[count];
```

This is especially useful for **VLIW bundles** that pack multiple instruction
slots into a wide bundle word:

```c
format SlotA[42] {
  fields:
    unsigned payload[42];
};

format SlotB[26] {
  fields:
    unsigned payload[26];
};

// A 128-bit VLIW bundle: [127..126]=mode, [125..42]=2x SlotA, [41..16]=SlotB, [15..0]=const
format VliwBundle128[128] {
  fields:
    unsigned mode[2];
    format SlotA slot_a[2];   // slot_a[0] is [125..84], slot_a[1] is [83..42]
    format SlotB slot_b;      // [41..16]
    unsigned imm_pool[16];    // [15..0]
};
```

For `format SlotA slot_a[2];`:

*   Instances are placed left-to-right from MSB to LSB: `index = 0` is the
    highest bit range (`[125..84]`) and `index = 1` is the next range
    (`[83..42]`).
*   `bin_format_gen` generates an indexed extractor
    `ExtractSlotA(bundle_val, int index)` and an indexed inserter
    `InsertSlotA(int index, slot_val, bundle_val)`.
*   Directly extracted sub-formats can be up to **128 bits** wide.

### Format Inheritance (`: BaseFormat`)

A format can inherit from another format:

```c
format Base32[32] {
  fields:
    unsigned body[25];
    unsigned opcode[7];
};

// Width '[32]' is optional when inheriting; if omitted, Base32's width (32) is used.
format DerivedR : Base32 {
  fields:
    unsigned funct7[7];
    unsigned rs2[5];
    unsigned rs1[5];
    unsigned funct3[3];
    unsigned rd[5];
    unsigned opcode[7];
};
```

What format inheritance does:

1.  **Width inheritance & checking**: If `[width]` is omitted on the child
    format, it inherits the parent format's bit width. If `[width]` is specified
    on both, `bin_format_gen` verifies that they match.
2.  **Instruction group membership**: Every `instruction group` specifies a base
    format (`instruction group Name[width] : BaseFormat`). Only formats equal to
    `BaseFormat` or transitively derived from `BaseFormat` may be used in that
    group.
3.  **Extractor promotion**: Non-conflicting field and overlay extractors
    defined in child formats are automatically promoted into their ancestor
    format's namespace as convenience wrappers (provided no sibling/ancestor
    defines a conflicting extractor with the same name at a different bit
    position/type).

**Note**: Format inheritance does *not* concatenate fields—each derived format
still declares all of its bits summing to the full format width.

### Supported Format Widths and C++ Representation Types

`bin_format_gen` supports formats of **any positive bit width**, mapping them to
C++ types as follows:

| Format Bit Width       | Generated C++ Input /    | Notes                |
:                        : Return Type              :                      :
| :--------------------- | :----------------------- | :------------------- |
| `1 .. 8`               | `uint8_t` (or `int8_t`   | Standard integer     |
:                        : if signed)               : shifts/masks         :
| `9 .. 16`              | `uint16_t` (or `int16_t` | Standard integer     |
:                        : if signed)               : shifts/masks         :
| `17 .. 32`             | `uint32_t` (or `int32_t` | Standard integer     |
:                        : if signed)               : shifts/masks         :
| `33 .. 64`             | `uint64_t` (or `int64_t` | Standard integer     |
:                        : if signed)               : shifts/masks         :
| `65 .. 128`            | `absl::uint128`          | Uses 128-bit         |
:                        :                          : shifts/masks         :
| `> 128` (e.g., 256-bit | `const uint8_t*` /       | Byte-pointer buffer  |
: VLIW)                  : `uint8_t*`               : extractors/inserters :

Additionally, if a format is `<= 128` bits wide and its **bit width** is **not**
a power of two (for example, a 12-bit, 24-bit, or 42-bit format),
`bin_format_gen` generates **both** integer-typed extractors (`uint16_t` /
`uint32_t` / `uint64_t` / `absl::uint128`) and `const uint8_t*` byte-buffer
overloads for convenience.

### Packed C++ Bitfield Structs (`layout: packed_struct;`)

By default (`layout: default;`), formats generate shift-and-mask functions. If
you also want a C++ packed bitfield struct and union for inspecting raw words,
add `layout: packed_struct;` at the top of the format:

```c
format RType[32] : Inst32 {
  layout: packed_struct;
  fields:
    unsigned funct7[7];
    unsigned rs2[5];
    unsigned rs1[5];
    unsigned funct3[3];
    unsigned rd[5];
    unsigned opcode[7];
};
```

In `<prefix>_bin_types.h`, `bin_format_gen` emits `PackedRType` (emitting C++
bitfields from LSB to MSB so bit `0` matches little-endian C++ bitfield layout)
and `UnionRType` (`union { PackedRType r_type; uint8_t bytes[4]; }`).
`packed_struct` requires all fields and sub-formats to be `<= 64` bits and does
not allow replicated sub-format arrays (`format Sub s[2]`).

--------------------------------------------------------------------------------

## 5. Defining Overlays (`overlays:`)

Many ISAs scatter immediate or register bits across non-adjacent positions in
the instruction word or append implicit low-order zero bits (for example, 2-byte
or 4-byte aligned branch targets). **Overlays** let you define virtual `signed`
or `unsigned` fields (up to 64 bits wide) by concatenating pieces from MSB
(left) to LSB (right):

```c
overlays:
  signed   name[width] = component_1, component_2, ...;
  unsigned name[width] = component_1, component_2, ...;
```

### The Four Overlay Component Types

Each comma-separated component in an overlay can be:

1.  **An entire named field**: `imm5` Uses all bits of `imm5` (where `imm5` is
    declared as a `signed` or `unsigned` field in `fields:`).
2.  **Bit slices of a named field**: `imm7[6]` or `imm7[5..0, 6]` Bit indices
    are relative to `imm7` itself (`0` is `imm7`'s LSB).
3.  **Bit slices directly from the enclosing format**: `[31, 7, 30..25, 11..8]`
    Bit indices are relative to the full format (`0` is the format's LSB). Use
    format-relative bit slices whenever an overlay needs bits belonging to a
    nested `format` sub-format, since sub-format aliases cannot be referenced by
    name in overlays.
4.  **A binary literal**: `0b0` or `0b0100'1010` Appends constant bits. The bit
    width of the literal is the exact number of `0`/`1` digits after `0b`
    (ignoring `'` separators)—for example, `0b0` is 1 bit wide and `0b0000` is 4
    bits wide.

The sum of the bit widths of all components **must equal** the declared
`[width]` of the overlay. Bit ranges (`hi..lo`) must be written in descending
MSB-to-LSB order (`hi >= lo`, such as `5..0`; ascending ranges like `0..5` are
rejected) or as single-bit indices, and multiple slices can be combined in a
comma-separated bracket list (`imm[7..4, 1, 0]`).

### Overlay Examples

```c
format JType[32] : Inst32 {
  fields:
    unsigned imm20[20];   // format bits [31..12]
    unsigned rd[5];       // format bits [11..7]
    unsigned opcode[7];   // format bits [6..0]
  overlays:
    // RISC-V J-type 21-bit signed jump offset:
    // imm[20 | 10:1 | 11 | 19:12] << 1, referenced via format bit numbers:
    signed j_imm[21] = [31, 19..12, 20, 30..21], 0b0;

    // Zero-extended 6-bit stack-pointer immediate scaled by 16 (low 4 bits 0):
    unsigned scaled_uimm[9] = imm20[4..0], 0b0000;
};
```

Overlays can be used **both**:

*   Through generated `Extract<OverlayName>()` / `Insert<OverlayName>()`
    functions in C++, **and**
*   Directly inside `instruction group` encoding constraints (e.g.,
    `nzimm != 0`).

--------------------------------------------------------------------------------

## 6. Instruction Groups and Instruction Encodings (`instruction group`)

While `format` blocks describe *how bits are named*, `instruction group` blocks
describe *which bit values identify each instruction*.

### Single Instruction Group

```c
instruction group GroupName[width] : BaseFormat {
  // instruction encodings...
};
```

Rules for a single instruction group:

*   **`[width]`**: Every instruction in the group has this exact bit width (**up
    to 64 bits**). If your ISA has both 16-bit compressed instructions and
    32-bit standard instructions (like RISC-V C + G), place the 16-bit
    instructions in a `[16]` group and the 32-bit instructions in a `[32]`
    group. (For VLIW bundles wider than 64 bits, define a bundle `format` for
    the full bundle width and define separate `instruction group`s for each
    `<= 64`-bit instruction slot format!)
*   **`: BaseFormat`**: Every instruction encoding in the group must reference
    either `BaseFormat` itself or a format that inherits (directly or
    indirectly) from `BaseFormat`, and `BaseFormat`'s width must equal
    `[width]`.

### Standard Instruction Encoding and Constraints

```c
opcode_name : FormatName : constraint_1, constraint_2, ...;
```

*   **`opcode_name`**: Must match the opcode name in your `.isa` file.
    `bin_format_gen` converts `snake_case` names to `PascalCase` prefixed with
    `k` when returning `OpcodeEnum` values (for example, `c_add` becomes
    `OpcodeEnum::kCAdd`, and `addi` becomes `OpcodeEnum::kAddi`).
*   **`FormatName`**: The specific format used to interpret field and overlay
    names in the constraint list.
*   **Constraints**: Comma-separated comparisons on fields or overlays of
    `FormatName`.

#### Supported Constraint Operators

| Operator        | Meaning          | Allowed RHS        | Notes             |
| :-------------- | :--------------- | :----------------- | :---------------- |
| `==`            | Equal to         | Numeric literal or | Supported for     |
:                 :                  : field/overlay name : `signed` and      :
:                 :                  :                    : `unsigned`.       :
:                 :                  :                    : Constant `==`     :
:                 :                  :                    : constraints       :
:                 :                  :                    : without           :
:                 :                  :                    : literal-bit       :
:                 :                  :                    : overlays drive    :
:                 :                  :                    : fast lookup-table :
:                 :                  :                    : generation.       :
| `!=`            | Not equal to     | Numeric literal or | Supported for     |
:                 :                  : field/overlay name : `signed` and      :
:                 :                  :                    : `unsigned` (e.g., :
:                 :                  :                    : `rs1 != 0`, `rs1  :
:                 :                  :                    : != rs2`).         :
| `>`, `>=`, `<`, | Relational range | Numeric literal or | Supported for     |
: `<=`            : check            : field/overlay name : **`unsigned`**    :
:                 :                  :                    : fields and        :
:                 :                  :                    : overlays (e.g.,   :
:                 :                  :                    : `rlist > 3`,      :
:                 :                  :                    : `shamt < 32`).    :

Examples of constraints:

```c
instruction group Inst16[16] : Inst16Base {
  // Constant equality and non-zero register checks:
  c_jr     : CR    : func4 == 0b1000, rs1 != 0, rs2 == 0, op == 0b10;
  c_mv     : CR    : func4 == 0b1000, rs1 != 0, rs2 != 0, op == 0b10;
  c_ebreak : CR    : func4 == 0b1001, rs1 == 0, rs2 == 0, op == 0b10;

  // Unsigned range check:
  cm_push  : PType : func8 == 0b1011'1000, rlist > 3, op == 0b10;

  // Field-to-field comparison (e.g., two register fields must differ):
  pair_op  : CR    : func4 == 0b1010, rs1 != rs2, op == 0b10;
};
```

### Intentional Duplicate Opcode Encodings (`!`)

Sometimes two distinct bit patterns or formats decode to the **exact same
logical opcode** in your `.isa` file. Normally, `bin_format_gen` warns if an
opcode name appears more than once in a group (to catch accidental copy-paste
mistakes). Prefix additional encodings with `!` to mark the duplicate opcode
name as intentional:

```c
instruction group MyGroup[32] : Inst32 {
  nop  : IType : imm12 == 0, rs1 == 0, funct3 == 0, rd == 0, opcode == 0b001'0011;
  !nop : RType : funct7 == 0, rs2 == 0, rs1 == 0, funct3 == 0, rd == 0, opcode == 0b011'0011;
};
```

### Encoding Specializations (`specializes`)

In many ISAs, a general instruction encoding has special-case encodings when
certain operand fields take specific values (for example, `addi x0, x0, 0` is
`nop`, or `jalr x0, rs1, 0` is `jr`). Instead of manually adding negated
constraints to the general instruction, you can declare that an instruction
**`specializes`** a parent instruction in the same group:

```c
instruction group RiscV32I[32] : Inst32 {
  addi : IType : funct3 == 0b000, opcode == 0b001'0011;

  // Inherits IType, funct3 == 0b000, and opcode == 0b001'0011 from 'addi',
  // and is tested before falling back to 'addi':
  nop  : specializes addi : imm12 == 0, rs1 == 0, rd == 0;
};
```

Rules for `specializes`:

*   `parent` must already be declared earlier in the same `instruction group`.
*   The specialized instruction automatically inherits `parent`'s format and all
    of `parent`'s constraints, then adds its own extra constraints.
*   Every specialized variant under the same parent must still be mutually
    distinguishable from its siblings.

### Templated Instruction Generation (`GENERATE`)

When an ISA has families of instructions with systematic names and opcode
numbers (for example, `add`, `sub`, `and`, `or`, `xor`, or width variants `_b`,
`_h`, `_w`), use `GENERATE(...)` to avoid repetitive boilerplate:

```c
instruction group AluOps[32] : Inst32 {
  // Tuple binding: 'mnem', 'f7', and 'f3' step together across the 4 tuples.
  GENERATE([mnem, f7, f3] = [
      {add, 0b000'0000, 0b000},
      {sub, 0b010'0000, 0b000},
      {and, 0b000'0000, 0b111},
      {or,  0b000'0000, 0b110}
  ]) {
    $(mnem) : RType : funct7 == $(f7), funct3 == $(f3), opcode == 0b011'0011;
  };
};
```

You can also specify **multiple comma-separated bindings** inside
`GENERATE(...)`. When multiple bindings are given, `bin_format_gen` expands
their **Cartesian product** (every combination of values):

```c
instruction group LoadStoreOps[32] : Inst32 {
  GENERATE(
      [op_prefix, fmt, opc] = [{ld, IType, 0b000'0011}, {st, SType, 0b010'0011}],
      [width_suffix, f3]    = [{b, 0b000}, {h, 0b001}, {w, 0b010}]
  ) {
    // Expands to 2 x 3 = 6 encodings: ld_b, ld_h, ld_w, st_b, st_h, st_w
    $(op_prefix)_$(width_suffix) : $(fmt) : funct3 == $(f3), opcode == $(opc);
  };
};
```

Inside a `GENERATE` body, `$(var)` can appear in the opcode name, the format
name, the field/overlay name on the left of a constraint, or the value/field on
the right of a constraint.

### Merging Instruction Groups

Modular ISA descriptions often put each extension in its own `instruction group`
(for example, `RV32I`, `RV32M`, `RV32F`) and then merge them into a single
unified decode function. You can merge groups either at top level:

```c
instruction group RV32IMF = { RV32I, RV32M, RV32F };
```

or inline inside a `decoder` block (shown in the next section). All merged
groups must have the **same bit width** and the **same base format**.

--------------------------------------------------------------------------------

## 7. Defining Decoders (`decoder`)

A `decoder` block tells `bin_format_gen` what C++ code to generate:

```c
decoder RiscV32GV {
  namespace mpact::sim::riscv::encoding;
  opcode_enum = "::mpact::sim::riscv::isa32v::OpcodeEnum";

  includes {
    #include "riscv32gv_enums.h"
  }

  // Merge 32-bit extension groups into a single DecodeRiscV32G() function:
  RiscV32G = { RiscVIInst32, RiscVMInst32, RiscVFInst32 };

  // Emit a separate DecodeRiscVCInst16() function for 16-bit instructions:
  RiscVCInst16;
};
```

### Decoder Attributes

*   **`namespace <ns>;`** *(required, at most one)*: The C++ namespace wrapping
    all generated types, decoder functions, extractors, and encoders.
*   **`opcode_enum = "<CppType>";`** *(required, at most one)*: The C++
    `OpcodeEnum` type returned by `Decode<Group>()`. When `.bin_fmt` and `.isa`
    use different C++ namespaces, qualify the enum path relative to `namespace`
    or from the global `::` root, and include the `*_enums.h` header in
    `includes`.
*   **`includes { #include "..." }`** *(optional, repeatable)*: Header files
    emitted as `#include` lines at the top of `<prefix>_bin_decoder.h`. Always
    include the `<prefix>_enums.h` header generated by `mpact_isa_decoder` so
    `OpcodeEnum` is visible.
*   **Group entries** *(at least one required)*:
    *   `GroupName;` — generates `Decode<GroupName>()` and
        `Decode<GroupName>WithFormat()` for an existing `instruction group`.
    *   `CombinedName = { GroupA, GroupB, ... };` — merges multiple compatible
        instruction groups into one combined decode table and generates
        `Decode<CombinedName>()` and `Decode<CombinedName>WithFormat()`.

Multiple `decoder` blocks may exist across `.bin_fmt` files (for example,
`RiscV32G` vs. `RiscV32GV`). Select which one to generate with
`decoder_name = "RiscV32GV"` in `mpact_bin_fmt_decoder` (or
`--decoder_name=RiscV32GV` on the `bin_format_gen` CLI).

--------------------------------------------------------------------------------

## 8. Generated Files and C++ API Reference

Given `prefix = "my_isa"` (or `src = "my_isa.bin_fmt"` with default prefix),
`mpact_bin_fmt_decoder` generates **five files** inside your declared
`namespace`:

| Generated File          | Contents                                           |
| :---------------------- | :------------------------------------------------- |
| `my_isa_bin_types.h`    | `enum class FormatEnum`, `kFormatNames[]`, and any |
:                         : `Packed<Format>` / `Union<Format>` structs for     :
:                         : `layout\: packed_struct;` formats.                 :
| `my_isa_bin_decoder.h`  | `Decode<Group>()` & `Decode<Group>WithFormat()`    |
:                         : declarations, inline                               :
:                         : `<format_snake>\:\:Extract<Field>()` namespaces,   :
:                         : and `class Extractors`.                            :
| `my_isa_bin_decoder.cc` | Multi-level lookup table and constraint-evaluation |
:                         : definitions for `Decode<Group>()`.                 :
| `my_isa_bin_encoder.h`  | `struct Encoder` with inline `Insert<Field>()`     |
:                         : bit-insertion helpers and `extern                  :
:                         : kOpcodeEncodings`.                                 :
| `my_isa_bin_encoder.cc` | `kOpcodeEncodings` hash map mapping each           |
:                         : `OpcodeEnum` to its `(fixed_bits_value,            :
:                         : bit_width)` tuple.                                 :

### 1. Instruction Decode Functions (`*_bin_decoder.h`)

For each group (e.g., `MiniRisc32` of width 32), two top-level functions are
generated:

```cpp
// Returns the matching OpcodeEnum, or OpcodeEnum::kNone if no encoding matches:
OpcodeEnum DecodeMiniRisc32(uint32_t inst_word);

// Returns both the OpcodeEnum and the matched FormatEnum (e.g., FormatEnum::kRType),
// or {OpcodeEnum::kNone, FormatEnum::kNone} if no encoding matches:
std::pair<OpcodeEnum, FormatEnum> DecodeMiniRisc32WithFormat(uint32_t inst_word);
```

### 2. Field, Sub-Format, and Overlay Extractors (`*_bin_decoder.h`)

For every format (e.g., `BType`), extractors are available in **two equivalent
styles**:

1.  **Snake-case format namespace**: `b_type::ExtractImm7(inst_word)`,
    `b_type::ExtractBImm(inst_word)`, plus bit-width constant
    `b_type::kBTypeSize`.
2.  **`Extractors` class scope**: `Extractors::BType::ExtractImm7(inst_word)`,
    `Extractors::BType::ExtractBImm(inst_word)`.

Additionally, if `BType` inherits from `Inst32`, non-conflicting extractors are
promoted to `inst32::ExtractBImm(inst_word)` /
`Extractors::Inst32::ExtractBImm(inst_word)` so you can extract common fields
without knowing which derived format was used.

### 3. Bit Inserters and Opcode Encoding Map (`*_bin_encoder.h`)

To support assemblers, binary encoders, and unit tests that construct machine
words programmatically, `*_bin_encoder.h` provides:

*   **`Encoder::<FormatPascal>::Insert<FieldOrOverlayOrSubFormat>(value,
    inst_word)`** *(and `Insert<SubFormat>(int index, value, inst_word)` for
    replicated sub-formats)*: Masks and shifts `value` into the target field,
    sub-format, or overlay bits of `inst_word` and returns the updated word (or
    writes into `uint8_t*` for `> 128`-bit formats).
*   **`kOpcodeEncodings`**: An
    `absl::NoDestructor<absl::flat_hash_map<OpcodeEnum, std::tuple<uint64_t,
    int>>>` containing `(constant_equality_bits, bit_width)` for every decoded
    opcode. Starting from `std::get<0>((*kOpcodeEncodings)[opcode])` and calling
    `Insert<Field>()` lets you assemble complete binary instructions cleanly!

--------------------------------------------------------------------------------

## 9. Under the Hood: How `bin_format_gen` Synthesizes Fast Decoders

Knowing how `bin_format_gen` compiles an `instruction group` helps you write
efficient encodings and understand ambiguity diagnostics:

1.  **Constraint Masks**: First, `bin_format_gen` inspects `==` constant
    constraints on fields and literal-free overlays to compute each
    instruction's *constraint mask* (which bits have fixed constant values).
2.  **Initial Encoding Groups**: Instructions whose constraint masks overlap are
    clustered into initial *encoding groups*. If an instruction has *no*
    constant `==` bits at all, or overlaps in incompatible ways,
    `bin_format_gen` partitions accordingly.
3.  **Recursive Discriminator Refinement**: Within each encoding group,
    `bin_format_gen` computes:
    *   **Constant mask & value**: Bits constrained to the *exact same value*
        across every instruction in the group (checked once on entry via
        `(inst_word & mask) != value`).
    *   **Discriminator mask**: Bits constrained across all instructions in the
        group that *differ* in value between instructions. When 2 or more
        discriminator bits exist, `bin_format_gen` extracts those bits into a
        compact index and dispatches through a `2^k` function-pointer table
        (`parse_group_*`), marking checked bits in an *ignore mask* so child
        levels never re-test them.
4.  **Leaf Decode Functions**:
    *   **Simple leaves**: When a leaf group has no `!=` / `<` / `<=` / `>` /
        `>=` constraints, no field-to-field comparisons, and no binary-literal
        overlays, `bin_format_gen` emits a direct
        `static OpcodeEnum opcodes[2^k]` array lookup with zero branches!
    *   **Constraint-checked leaves**: When relational constraints (`rs1 != 0`,
        `rlist > 3`, `rs1 != rs2`) or specializations are present, the leaf
        extracts the required fields once and emits targeted
        `if (...) return OpcodeEnum::k...;` checks.

--------------------------------------------------------------------------------

## 10. Best Practices from Production Simulators

Production MPACT-Sim targets—such as **`mpact_riscv`** (RV32G/RV64G, Vector,
Bitmanip, Compressed `Zc*`, and the RVM23 profile), **`mpact_cheriot`**
(capability-extended RISC-V + RVV), and the **Coral NPU** simulator (RISC-V
scalar core + custom NPU vector extensions)—scale to hundreds of instructions
across many configurations by following a consistent set of modular design
patterns.

### 1. Factor Shared Formats into Standalone `#once` Format Files

As an architecture grows, putting `format` declarations in the same file as
instruction encodings quickly leads to either monolithic files or
duplicate-format collisions when combining extensions.

Instead, **extract shared bit-level formats into dedicated format-only
`.bin_fmt` files guarded with `#once`**:

*   **`mpact_riscv`** places all 32-bit formats (`Inst32Format`, `RType`,
    `IType`, `SType`, `BType`, `UType`, `JType`, `VMem`, `VArith`, `VConfig`,
    etc.) in `riscv_format32.bin_fmt` and all 16-bit compressed formats
    (`Inst16Format`, `CR`, `CI`, `CL`, `CS`, `CB`, `CJ`, `CMPP`, etc.) in
    `riscv_format16.bin_fmt`, each starting with `#once`.
*   **Coral NPU** similarly places all standard scalar formats and custom vector
    formats (`CoralNPUSystemType`, `CoralNPUV2ArgsType`, `CoralNPUV3ArgsType`)
    in `coralnpu_format.bin_fmt`.

```c
// riscv_format32.bin_fmt
#once

format Inst32Format[32] {
  fields:
    unsigned bits[25];
    unsigned opcode[7];
};

format RType[32] : Inst32Format {
  fields:
    unsigned func7[7];
    unsigned rs2[5];
    unsigned rs1[5];
    unsigned func3[3];
    unsigned rd[5];
    unsigned opcode[7];
  overlays:
    unsigned r_uimm5[5] = rs2;
};
// ... IType, SType, BType, VMem, VArith, VConfig ...
```

Why this works so well:

1.  **Independent extension files**: Feature files like `riscv32g.bin_fmt`,
    `riscv_vector.bin_fmt`, and `riscv_zc.bin_fmt` can each
    `#include "riscv_format32.bin_fmt"` directly so they are self-contained, and
    `#once` ensures the formats are parsed only once when those files are later
    combined.
2.  **Stable C++ extractor names**: Every simulator target built from those
    formats shares identical C++ extractor namespaces (`r_type::ExtractRs1`,
    `v_arith::ExtractVm`), allowing C++ operand getter libraries (`*_getters.h`)
    to be reused across multiple simulator variants.

### 2. Split Instruction Encodings by Extension or Functional Unit

Rather than maintaining one giant `instruction group` for the entire ISA,
organize encodings into **modular per-extension or per-unit `.bin_fmt` files**,
each defining focused `instruction group` blocks:

*   **By ISA extension (`mpact_riscv` & `mpact_cheriot`)**: Separate files for
    `riscv32g.bin_fmt`, `riscv32zb.bin_fmt`, `riscv_vector.bin_fmt`,
    `riscv_zc.bin_fmt`, `riscv_zicond.bin_fmt`, `riscv_zicbom.bin_fmt`, or
    `riscv_cheriot_f.bin_fmt` and `riscv_cheriot_vector.bin_fmt`.
*   **By functional unit (Coral NPU)**: Separate files for
    `coralnpu_base.bin_fmt`, `coralnpu_arith.bin_fmt`, `coralnpu_conv.bin_fmt`,
    `coralnpu_memory.bin_fmt`, `coralnpu_mul.bin_fmt`, and
    `coralnpu_shift.bin_fmt`.
*   **Cross-width / cross-project sharing**: In `mpact_riscv`,
    `riscv32zb.bin_fmt` puts bit-manipulation encodings shared between RV32 and
    RV64 in `RiscVZbbInst32` and isolates 32-bit-only encodings (`rev8`,
    `zext_h`) into `RiscVZbbInst32Only`. Not only do both RV32 and RV64 reuse
    those groups, but the Coral NPU simulator also directly `#include`s
    `mpact_riscv`'s `riscv32zb.bin_fmt` to add Zbb support without duplicating a
    single line of encoding rules!

### 3. Keep Top-Level Profile `.bin_fmt` Files as Thin Composition Wrappers

Once formats and extensions are factored into modular files, a top-level
simulator profile `.bin_fmt` file (such as `rvm23.bin_fmt` or `riscv32v.bin_fmt`
in `mpact_riscv`, `riscv_cheriot_rvv.bin_fmt` in `mpact_cheriot`, or
`coralnpu.bin_fmt` in Coral NPU) becomes a clean, 30–60 line manifest that only:

1.  `#include`s the format and extension `.bin_fmt` files, and
2.  Declares one or more `decoder` blocks that merge the desired
    `instruction group`s by instruction width:

```c
// rvm23.bin_fmt — Top-level profile composed from modular extension files
decoder RVM23 {
  namespace mpact::sim::riscv::rvm23;
  opcode_enum = "OpcodeEnum";
  includes {
    #include "rvm23_enums.h"
  }

  // Merge all 32-bit extension groups (sharing Inst32Format) into one decoder:
  RVM23Inst32 = {
    RiscVGInst32,
    RiscVZbaInst32, RiscVZbbInst32, RiscVZbbInst32Only,
    RiscVZbcInst32, RiscVZbsInst32,
    RiscVZicond, RiscVZimop, RiscVZhintpause, RiscVZihintntl,
    RiscVZicbop, RiscVZicbom, RiscVZicboz,
  };

  // Merge all 16-bit compressed groups (sharing Inst16Format) into a second decoder:
  RVM23Inst16 = {
    RiscVZcmop, RiscVCZihintntl, RiscVCZca, RiscVCZcf,
    RiscVCZcb32, RiscVCZcmp, RiscVCZcmt,
  };
};

#include "riscv32g.bin_fmt"
#include "riscv32zb.bin_fmt"
#include "riscv_zc.bin_fmt"
#include "riscv_zhintpause.bin_fmt"
#include "riscv_zicbom.bin_fmt"
#include "riscv_zicbop.bin_fmt"
#include "riscv_zicboz.bin_fmt"
#include "riscv_zicond.bin_fmt"
#include "riscv_zihintntl.bin_fmt"
#include "riscv_zimop.bin_fmt"
```

You can also place **multiple related `decoder` targets in the same wrapper
file**—for instance, `riscv_cheriot_rvv.bin_fmt` defines both
`decoder RiscVCheriotRVV` (integer RVV) and `decoder RiscVCheriotRVVFp` (adding
`RiscVFInst32` and `RiscVVFPInst32`)—and select each one in Bazel via
`decoder_name = "..."`.

### 4. Decide Deliberately When to Merge Groups vs. Keep Separate Groups

Production simulators use two patterns for `instruction group`s in a `decoder`:

*   **Merge compatible groups (`Combined32 = { GroupA, GroupB }`)** whenever
    extensions share the same word width and encoding space (for example, all
    32-bit RISC-V extensions inheriting from `Inst32Format`). Merging generates
    a **single** fast `DecodeCombined32(uint32_t)` lookup tree and automatically
    verifies at build time that no two extensions accidentally collide in opcode
    space.
*   **Keep separate groups** when:
    1.  **Instruction widths differ**: 16-bit compressed instructions
        (`Inst16Format`) and 32-bit instructions (`Inst32Format`) cannot be
        merged; the C++ fetch path checks the low size bits and dispatches to
        `Decode...16(uint16_t)` or `Decode...32(uint32_t)`.
    2.  **Major encoding spaces use distinct base formats**: For example, Coral
        NPU V1 gives custom 2-operand and 3-operand vector encodings their own
        root formats (`CoralNPUV2ArgsType`, `CoralNPUV3ArgsType`) separate from
        `Inst32Format`, producing dedicated `DecodeCoralNPUVectorArithInst()`
        functions called when custom vector opcodes are fetched.

### 5. Use Overlays to Avoid Format Proliferation

Before creating a new `format` for a minor encoding variation, check whether an
**overlay** on an existing format is cleaner:

*   **Signed vs. unsigned views of the same bits**: In `riscv_format32.bin_fmt`,
    `IType` declares `signed imm12[12]` (for `addi`/`lw`) and adds
    `overlays: unsigned u_imm12[12] = imm12;` (for unsigned CSR addresses in
    `csrrw`) and `unsigned i_uimm5[5] = rs1;` (for CSR 5-bit immediates).
*   **Multi-purpose vector formats**: `VArith` defines a single 32-bit field
    layout and uses overlays (`uimm5`, `simm5`, `uimm6`, `func5`, `rd`, `rs1`)
    for scalar/immediate vector variants instead of duplicating half a dozen
    nearly identical vector formats.
*   **Implicit register bank prefixes**: In `riscv_format16.bin_fmt`, 16-bit
    compressed formats (`CL`, `CS`, `CIW`, `CA`, `CB`) map 3-bit compact
    register fields (`0..7` -> `x8..x15`) directly in hardware encoding order
    via `unsigned rs1[5] = 0b01, rs1p;`.

### 6. Split Architectural Corner Cases in `.bin_fmt` Constraints

Hardware architectures frequently attach special semantics when a register or
mode field is `0` or `1`. Rather than branching inside C++ semantic functions on
every execution, `mpact_riscv`, `mpact_cheriot`, and Coral NPU split these cases
at decode time using field constraints:

*   **Hardwired-zero register destinations (`rd == 0`)**: Separate `jal`
    (`rd != 0`) from `j` (`rd == 0`), `addi` (`rd != 0`) from `nop`
    (`rd == 0, rs1 == 0, imm12 == 0`), and `rd == 0` architectural hints into a
    dedicated `RiscVIHints32` group.
*   **CSR read/write side effects**: Split `csrrs` into `csrrs`
    (`rs1 != 0, rd != 0`), `csrrs_nr` (`rd == 0`, skips CSR read side effects),
    and `csrrs_nw` (`rs1 == 0`, skips CSR write attempts so reading a read-only
    CSR does not trap).
*   **Predicated vs. unpredicated vector ops**: Split `vle8` (`vm == 0`, masked)
    and `vle8_vm1` (`vm == 1`, unmasked) so unmasked vector instructions avoid
    runtime mask overhead.

### 7. Track Transitive `.bin_fmt` Includes in `BUILD` Files

Because Bazel executes `bin_format_gen` in a hermetic action sandbox,
`includes = [...]` on `mpact_bin_fmt_decoder` must list **every** transitively
`#include`d `.bin_fmt` file—including shared format files like
`riscv_format32.bin_fmt` and `riscv_format16.bin_fmt`:

```python
mpact_bin_fmt_decoder(
    name = "riscv32gv_bin_fmt",
    src = "riscv32v.bin_fmt",
    decoder_name = "RiscV32GV",
    includes = [
        "riscv32g.bin_fmt",
        "riscv32zb.bin_fmt",
        "riscv_format16.bin_fmt",
        "riscv_format32.bin_fmt",
        "riscv_vector.bin_fmt",
    ],
    prefix = "riscv32gv",
    deps = [":riscv32gv_isa"],
)
```

--------------------------------------------------------------------------------

## 11. Common Pitfalls and Troubleshooting Checklist

*   **"Format width mismatch" / fields don't add up**: Double-check that the sum
    of all widths in `fields:` equals `[width]` in `format Name[width]`, and
    that the sum of all components in each `overlay` equals `[width]` on that
    overlay. Don't forget that `0b0` is 1 bit while `0b00` is 2 bits!
*   **Reversed bit layout**: Remember that the **first** field in `fields:` is
    the **most-significant bits** (highest bit numbers), and the **last** field
    ends at **bit 0 (LSB)**—matching how instruction formats are drawn
    left-to-right in ISA manuals (such as RISC-V and Arm).
*   **Overlapping / ambiguous encodings error**: If two instructions in the same
    group have identical `==` bits and neither `!=`/relational constraints nor
    `specializes` distinguishes them, `bin_format_gen` reports an encoding
    conflict. If one encoding is a special case of another (such as `nop` vs.
    `addi`), use `nop : specializes addi : ...;`. If two distinct bit patterns
    intentionally map to the same opcode, prefix the second with `!`.
*   **Opcode enum compile error in generated `*_bin_decoder.h`**: Ensure every
    instruction name in `.bin_fmt` exists in the `.isa` file (same spelling;
    `snake_case` maps to `kPascalCase`), `opcode_enum` in `decoder { ... }`
    points to the C++ `OpcodeEnum` namespace from `.isa`, and
    `includes { #include "<prefix>_enums.h" }` is present in `decoder { ... }`.
*   **VLIW bundle wider than 64 bits**: Do **not** create an `instruction group`
    with `[width]` > 64. Instead, define a `format Bundle[128]` (or `[256]`)
    containing sub-formats for each slot (`format AluSlot alu[2];`), and define
    separate `instruction group AluGroup[42] : AluSlot { ... };` blocks for each
    slot!

--------------------------------------------------------------------------------

## 12. Grammar Reference

The complete ANTLR4 grammar for `.bin_fmt` files is located in `BinFormat.g4` in
the `decoder/` directory.
