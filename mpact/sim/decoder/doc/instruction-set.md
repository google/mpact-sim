# Writing ISA Descriptions in MPACT-Sim (`.isa`)

This guide explains how to write **instruction set architecture (ISA)
description files** (`.isa`) in MPACT-Sim using the `InstructionSet.g4` grammar
and the `decoder_gen` code generator. It is written for developers who are new
to MPACT-Sim and want to model a scalar processor, a multi-extension RISC/CISC
architecture, or a multi-slot VLIW machine.

For the companion guide on describing raw bitfields, overlays, and binary opcode
constraints, see [binary-encoding.md](binary-encoding.md).

--------------------------------------------------------------------------------

## 1. Big Picture: Representation-Independent Decoding in MPACT-Sim

At runtime, MPACT-Sim represents every decoded target instruction as an
`mpact::sim::generic::Instruction` object. An `Instruction` bundles together
everything the simulator needs to execute and inspect that instruction:

*   **Semantic function** (`void(Instruction*)`): The C++ callable that reads
    source operands, computes results, and writes destination operands.
*   **Predicate operand** *(optional)*: A boolean-like operand checked before
    executing a predicated instruction.
*   **Source operands**: Interfaces (`SourceOperandInterface*`) for reading
    registers or immediate constants.
*   **Destination operands**: Interfaces (`DestinationOperandInterface*`) for
    writing registers or architectural state (such as `next_pc`) with a
    specified cycle **latency**.
*   **Child instructions** *(optional)*: Linked `Instruction` instances for
    multi-phase operations (such as a memory load split into address issue and
    register writeback).
*   **Metadata**: Instruction address, byte size, formatted disassembly string,
    integer attributes, and hardware pipeline resource operands.

Once decoded, `Instruction` objects are stored in a decode cache so repeated
executions (such as inside loops) skip decoding entirely.

### Why `.isa` Files Are Separate from `.bin_fmt` Files

Instead of hardcoding bit shifts inside the decoder that builds `Instruction`
objects, MPACT-Sim splits decoding into two layers:

1.  **The Representation-Independent ISA Decoder (`.isa` -> `decoder_gen`)**:
    Knows *every opcode in your ISA*, which slots/bundles it can issue from,
    what operands and latencies it has, which C++ semantic function implements
    it, and how to format its disassembly. It knows **nothing** about bit
    positions, protobufs, or text assembly. Instead, `decoder_gen` generates an
    abstract C++ interface class named `<IsaName>EncodingBase` with virtual
    methods like `GetOpcode()`, `GetSource()`, and `GetDestination()`.
2.  **The Encoding Layer (`.bin_fmt` -> `bin_format_gen`, or `.proto_fmt`,
    etc.)**: Extracts opcodes and bitfields from raw machine words (see
    [binary-encoding.md](binary-encoding.md)). You write a small C++ class
    inheriting from `<IsaName>EncodingBase` that calls the `.bin_fmt`
    decode/extract functions to return `OpcodeEnum` and operand pointers.

This separation makes it easy to experiment with new instructions, widen
register files during architectural exploration, or drive the exact same ISA
semantics from binary machine code, protobuf traces, or assembly tests.

--------------------------------------------------------------------------------

## 2. Quickstart: Your First `.isa` File

Let's pair the `MiniRisc` binary encoding from
[binary-encoding.md](binary-encoding.md) with a complete `.isa` file supporting
four instructions: `add`, `addi`, `beq`, and a two-phase `lw` (load word).

### Step 1: Write `mini_risc.isa`

```c
// mini_risc.isa

// Format the first disassembly column (mnemonic) as 12 chars, left-justified:
disasm widths = {-12};

// Top-level ISA declaration:
isa MiniRisc {
  namespace tutorial::mini_risc::isa;
  slots { mini_risc; }
}

// Single instruction slot for a sequential scalar processor:
slot mini_risc {
  includes {
    #include "mini_risc_instructions.h"
  }

  default size = 4;
  default latency = 0;

  // Fallback used when decoding an unrecognized instruction (OpcodeEnum::kNone):
  default opcode =
    disasm: "illegal", "%(@:08x)",
    semfunc: "&MiniRiscIllegal";

  opcodes {
    // R-type: rd = rs1 + rs2
    add { : rs1, rs2 : rd },
      disasm: "add", "%rd, %rs1, %rs2",
      semfunc: "&MiniRiscAdd";

    // I-type: rd = rs1 + imm12
    addi { : rs1, imm12 : rd },
      disasm: "addi", "%rd, %rs1, %imm12",
      semfunc: "&MiniRiscAdd";

    // B-type: if (rs1 == rs2) next_pc = pc + b_imm
    beq { : rs1, rs2, b_imm : next_pc },
      disasm: "beq", "%rs1, %rs2, %(@+b_imm:08x)",
      semfunc: "&MiniRiscBeq";

    // Split-phase load:
    //   Parent: computes address (rs1 + imm12) and initiates memory read.
    //   Child:  writes fetched data to 'rd'.
    lw { (: rs1, imm12), (: : rd) },
      disasm: "lw", "%rd, %imm12(%rs1)",
      semfunc: "&MiniRiscLwIssue", "&MiniRiscLwWriteback";
  }
}
```

### Step 2: Implement Semantic Functions (`mini_risc_instructions.h`)

Every semantic function takes a single `mpact::sim::generic::Instruction*`:

```cpp
#include "mpact/sim/generic/instruction.h"
// (In google3, include "third_party/mpact_sim/generic/instruction.h")

namespace tutorial::mini_risc {

void MiniRiscIllegal(::mpact::sim::generic::Instruction* inst);
void MiniRiscAdd(::mpact::sim::generic::Instruction* inst);
void MiniRiscBeq(::mpact::sim::generic::Instruction* inst);
void MiniRiscLwIssue(::mpact::sim::generic::Instruction* inst);
void MiniRiscLwWriteback(::mpact::sim::generic::Instruction* inst);

}  // namespace tutorial::mini_risc
```

Inside `MiniRiscAdd(Instruction* inst)`, operands appear in the exact order
listed in `{ : rs1, rs2 : rd }`—for example, `inst->Source(0)` is `rs1`,
`inst->Source(1)` is `rs2` (or `imm12` for `addi`!), and `inst->Destination(0)`
is `rd`. Notice how `add` and `addi` can share the **exact same C++ semantic
function** (`&MiniRiscAdd`) because `SourceOperandInterface` abstracts whether
source 1 came from a register or an immediate!

### Step 3: Connect `<IsaName>EncodingBase` and `<IsaName>InstructionSet`

`decoder_gen` generates `mini_risc_enums.h` and `mini_risc_decoder.h` containing
`MiniRiscEncodingBase`, `MiniRiscInstructionSetFactory`, and
`MiniRiscInstructionSet`:

1.  Subclass `MiniRiscEncodingBase` and implement `GetOpcode()`, `GetSource()`,
    and `GetDestination()` using your `.bin_fmt` extractors (`DecodeMiniRisc32`,
    `r_type::ExtractRs1`, `i_type::ExtractImm12`, etc.).
2.  Instantiate `MiniRiscInstructionSet` and call
    `isa->Decode(address, &my_encoding)` inside your
    `DecoderInterface::DecodeInstruction(uint64_t address)` method to obtain a
    ready-to-execute `Instruction*`!

--------------------------------------------------------------------------------

## 3. Global Declarations, Includes, Constants, and Expressions

### Comments and Numeric Literals

`.isa` files support `//` line comments, `/* ... */` block comments, and
hexadecimal (`0x1f`), binary (`0b1010`), octal (`077`), and decimal (`42`)
integer literals with optional single-quote digit separators (`0b1100'0011`,
`1'000`).

### Importing `.isa` Files vs. Emitting C++ `#include` Headers

There are **two distinct include constructs** in `.isa` files:

1.  **Top-level `#include "other_file.isa"`**: Parses and imports slots,
    bundles, constants, or ISA definitions from another `.isa` file. Remember to
    list imported `.isa` files in the `includes = [...]` attribute of
    `mpact_isa_decoder` in your `BUILD` file.
2.  **`includes { #include "header.h" }` blocks**: Copies C++ `#include`
    directives into the generated `<prefix>_decoder.cc` file so semantic
    functions and helpers (like `absl::bind_front`) are visible when compiling
    the generated decoder:
    *   **Global `includes { ... }`**: Always emitted into
        `<prefix>_decoder.cc`.
    *   **Slot- or bundle-local `includes { ... }`**: Merged and emitted
        **only** if that slot or bundle is reachable from the selected `isa`
        (directly or via slot inheritance). This keeps unused extension headers
        out of builds that do not include those slots.

### Constants and Integer Expressions

You can define named `int` constants at **global scope** or inside a **`slot`**:

```c
int kBaseLatency = 1;

slot scalar_alu {
  int kMulLatency = kBaseLatency + 2;
  ...
}
```

Compile-time integer expressions can be used in constant definitions,
`disasm widths`, `default latency`, template arguments, destination operand
latencies, resource cycle windows, and instruction attributes. Expressions
support:

*   Literals, global/slot `int` constants, and `template <int ...>` parameter
    names
*   Arithmetic operators: unary `-`, `*`, `/`, `+`, `-`, and parentheses `(...)`
*   Built-in function: `abs(expr)`

### Disassembly Column Alignment (`disasm widths`)

```c
disasm widths = {-18};
```

When an opcode specifies multiple comma-separated strings in `disasm:` (for
example, `disasm: "addi", "%rd, %rs1, %imm12"`), `disasm widths` controls the
fixed width and alignment of each column from left to right:

*   **Negative integer (`-18`)**: Left-justified in an 18-character field.
*   **Positive integer (`18`)**: Right-justified in an 18-character field.
*   Any extra `disasm` strings beyond the number of entries in `disasm widths`
    (or all strings if `disasm widths` is omitted) are concatenated with no
    extra padding.

--------------------------------------------------------------------------------

## 4. Top-Level `isa` Declaration

Every code generation target begins with an `isa` block:

```c
isa RiscV32G {
  namespace mpact::sim::riscv::isa32;
  slots { riscv32; }
}
```

or, for a multi-bundle VLIW architecture:

```c
isa MyVliwIsa {
  namespace my_sim::vliw::isa;
  bundles {
    main_bundle;
  }
}
```

*   **`isa <IsaName>`**: Sets the PascalCase prefix for generated classes
    (`<IsaName>EncodingBase`, `<IsaName>InstructionSet`,
    `<IsaName>InstructionSetFactory`, `<IsaName>EncoderInterfaceBase`).
*   **`namespace <cpp::ns>;`**: The C++ namespace for all generated enums and
    classes.
*   **`slots { ... }` or `bundles { ... }`**: A sequential/single-word ISA lists
    its `slots` directly; an ISA with sub-bundle dispatch structure lists its
    top-level `bundles`.
*   Multiple `isa` declarations may coexist in `.isa` files; select the active
    target via `isa_name = "RiscV32G"` in `mpact_isa_decoder` (or
    `--isa_name=RiscV32G` on the `decoder_gen` CLI).

--------------------------------------------------------------------------------

## 5. Describing Opcodes and Operands (`opcodes { ... }`)

Inside a `slot`, the `opcodes { ... }` block defines the instructions valid in
that slot.

### Basic Syntax

```c
opcode_name [byte_size] { predicate : sources : destinations },
  attribute_1,
  attribute_2,
  ...;
```

*   **`opcode_name`**: Must be unique across the ISA. `decoder_gen` converts
    `snake_case` names to `PascalCase` prefixed with `k` in `OpcodeEnum` (for
    example, `add_i` -> `OpcodeEnum::kAddI`), which must match the name used in
    `.bin_fmt`.
*   **`[byte_size]`** *(optional)*: Instruction byte size stored on
    `Instruction::size()` (commonly used for PC increments). If omitted, the
    slot's `default size` is used (which itself defaults to `1` if unset).

### Operand Triplets (`{ pred : sources : dests }`)

Operands are written inside braces `{ ... }` in up to three colon-separated
sections—**predicate**, **source list**, and **destination list**—all of which
are optional:

| Syntax                 | Meaning                  | Example Instruction     |
| :--------------------- | :----------------------- | :---------------------- |
| `{}`                   | No predicate, no         | `nop {}` or `ecall {}`  |
:                        : sources, no destinations :                         :
| `{ pred }`             | Predicate operand only   | `st_marker { pred }`    |
| `{ : rs1, imm12 }`     | Sources only (no         | `fence { : imm12 }`     |
:                        : predicate, no            :                         :
:                        : destinations)            :                         :
| `{ : rs1, S_imm12, rs2 | Sources only (trailing   | `sw { : rs1, S_imm12,   |
: \: }`                  : `\:` optional)           : rs2 \: }`               :
| `{ : rs1, rs2 : rd }`  | Sources and destination  | `add { : rs1, rs2 : rd  |
:                        : (no predicate)           : }`                      :
| `{ pred : sx, sy : dst | Predicate, sources, and  | `vadd { pred : sx, sy : |
: }`                     : destination              : dst }`                  :

#### Naming Operands Wisely

Every distinct operand name you write creates an entry in `PredOpEnum`,
`SourceOpEnum`, or `DestOpEnum` (`rs1` -> `SourceOpEnum::kRs1`, `I_imm12` ->
`SourceOpEnum::kIImm12`). In `<IsaName>EncodingBase`, you implement one factory
entry per enum value!

**Best Practice**: Give two operands the **same name** whenever they belong to
the same category (predicate, source, or destination) and have the **exact same
bit position, bit width, and sign/zero extension** in the encoding. Conversely,
give them **different names** (such as `I_imm12` vs. `S_imm12` vs. `I_uimm5`)
when they extract different bitfields or signedness.

### Scalar vs. List / Array Operands (`[rlist]`)

Some instructions read or write a **variable-length list of registers** encoded
by a bitmask or count field (for example, Arm `push`/`pop` or RISC-V
`cm.push`/`cm.pop`). Enclose the operand name in square brackets `[name]`:

```c
push { : x2, spimm6, rlist, [rlist] : x2 };
pop  { : x2, spimm6, rlist : x2, [rlist] };
```

*   Scalar `rlist` generates `SourceOpEnum::kRlist` and calls
    `GetSource(...) -> SourceOperandInterface*`.
*   Bracketed `[rlist]` generates `ListSourceOpEnum::kRlist` (or
    `ListDestOpEnum::kRlist`) and calls
    `GetSources(...) -> std::vector<SourceOperandInterface*>` (or
    `GetDestinations(...)`), appending every returned operand to the
    `Instruction`!

### Relocation Annotation (`%reloc`)

Prefixing a source or destination operand with `%reloc(op_name)` marks that
operand as carrying symbol relocation information when generating
assembler/encoder glue in `<prefix>_encoder.{h,cc}`:

```c
jal { : %reloc(J_imm20) : next_pc, rd };
```

### Destination Operand Latencies

Every destination operand in MPACT-Sim has a cycle **latency** controlling when
the written value takes effect:

*   **Latency `0`**: Immediate writeback during semantic function execution
    (typical for sequential single-issue simulators where no parallel slot reads
    old values in the same cycle).
*   **Latency `1`**: Buffered writeback at the end of the current cycle, visible
    on the *next* cycle (essential for **VLIW** bundles so parallel instructions
    in the same bundle read pre-bundle register values!).
*   **Latency `N` (`N > 1`)**: Buffered writeback visible after `N` simulated
    cycles (for modeling non-interlocked or pipelined execution).

You can specify destination latency in three ways:

1.  **Compile-time expression**: `rd(0)`, `rd(2)`, `rd(kLoadLatency + 1)`
2.  **Dynamic / runtime wildcard**: `rd(*)` — tells the generated decoder to
    call `enc->GetLatency(slot, entry, opcode, DestOpEnum::kRd, dest_no)` at
    decode time.
3.  **Slot default**: `rd` (no parentheses) — uses `default latency = <expr>;`
    from the enclosing `slot`. (If a slot has no `default latency`, omitting
    `(latency)` on a destination operand is an error unless supplied via
    `latency:`.)

### Multi-Phase / Child Instructions (`(parent), (child)`)

Instructions whose execution spans multiple stages—most commonly **memory
loads**, where stage 1 computes the effective address and issues the memory
request, and stage 2 writes the returned data buffer into `rd`—are modeled using
parenthesized operand tuples:

```c
lw { (: rs1, I_imm12), (: : rd) },
  disasm: "lw", "%rd, %I_imm12(%rs1)",
  semfunc: "&RV32ILw", "&RV32ILwChild";
```

*   The first tuple `(: rs1, I_imm12)` populates the parent `Instruction*`
    (`&RV32ILw`).
*   The second tuple `(: : rd)` allocates a child `Instruction*`
    (`&RV32ILwChild`) attached via `inst->child()` (additional tuples chain via
    `inst->child()->child()`, etc.).
*   When `semfunc:` is specified, you must provide **one semantic function
    string per tuple** (parent first, then each child in order).

### Templated Opcode Expansion (`GENERATE`)

Just like `.bin_fmt`, a slot's `opcodes` block supports `GENERATE(...)` over
single-variable lists or synchronized value tuples, expanding the Cartesian
product when multiple range assignments are separated by commas:

```c
GENERATE(
    [op, fn] = [{add, MyAdd}, {sub, MySub}, {and, MyAnd}],
    [suffix, dsp_suf, src2] = [{r, "", rs2}, {i, ".i", imm12}]
) {
  $(op)_$(suffix) { : rs1, $(src2) : rd },
    disasm: "$(op)$(dsp_suf)", "%rd, %rs1, %$(src2)",
    semfunc: "&$(fn)";
};
```

Notes on `GENERATE` in `.isa` files:

*   Remember the trailing semicolon (`};`) after the `GENERATE` block (every
    entry in `opcodes { ... }` is `;`-terminated).
*   Attributes like `disasm:` and `semfunc:` still require string literal quotes
    around the template (`semfunc: "&$(fn)"`).
*   Range values may be identifiers (`add`, `MyAdd`), numbers, or quoted strings
    (`""`, `".i"`). Outer double quotes on range values are stripped before
    substituting `$(var)`, which makes quoted strings ideal for empty suffixes
    (`""`) or fragments containing punctuation (`".i"`).

--------------------------------------------------------------------------------

## 6. Opcode Attributes Reference

An opcode can have up to **five** comma-separated attributes (at most one of
each kind):

### 1. Semantic Functions (`semfunc:`)

```c
semfunc: "&MySemFunc"
semfunc: "&ParentFunc", "&ChildFunc"
```

Each string literal must be a valid C++ expression convertible to
`Instruction::SemanticFunction` (`void(Instruction*)`). Common idioms:

```cpp
// Given C++ declarations:
void MyAdd(Instruction* inst);
void VectorOp(int elem_bytes, Instruction* inst);
void ShiftOp(Instruction* inst, bool arithmetic);
```

```c
// In .isa:
semfunc: "&MyAdd"
semfunc: "absl::bind_front(&VectorOp, 4)"
semfunc: "std::bind(&ShiftOp, std::placeholders::_1, true)"
semfunc: "[](Instruction* inst) { /* inline no-op */ }"
```

### 2. Disassembly Formatting (`disasm:`)

```c
disasm: "beq", "%rs1, %rs2, %(@+B_imm12:08x)"
```

Supported `%` substitutions inside `disasm` strings (escape a literal percent
sign as `\%`):

*   **`%op_name`**: Calls `AsString()` on the matching predicate, source, or
    destination operand (for example, `"x5"` for a register or `"42"` for an
    immediate).
*   **`%op_name?`** *(optional operand formatting)*: Useful for predicates
    (e.g., `"%pred? add"`), printing the operand only when present/active.
*   **`%(expr:fmt)`** *(formatted numeric/PC expression)*:
    *   `@` represents `inst->address()` (the PC of the instruction).
    *   `@ + op_name` or `@ - op_name` adds/subtracts an operand's integer value
        to/from the PC (ideal for PC-relative branches and jumps!).
    *   `(op_name << N)`, `(op_name >> N)`, or `@ + (op_name << N)` shifts the
        operand value before formatting. Bit-shift sub-expressions **must** be
        enclosed in parentheses (for example, `%((U_imm20 >> 12):d)` or
        `%(@ + (imm << 2):08x)`).
    *   `:fmt` is an integer format specifier: `:d` (decimal), `:o` (octal),
        `:x` (lowercase hex, which **automatically prepends `"0x"`**), or `:X`
        (uppercase hex **without** a `"0x"` prefix). Specifiers may include a 1-
        or 2-digit width (`1`–`99`), optionally zero-padded, such as `:08x` or
        `:04d` (if you write a leading `0`, always include the 1- or 2-digit
        width, e.g., `:08x` rather than `:0x`, and avoid writing `"0x%(...:x)"`
        since `:x` already emits `"0x"`).

### 3. Hardware Pipeline Resources (`resources:`)

To detect structural or data hazards in cycle-accurate or scoreboarding
simulators, declare hardware/register resources in three optional
colon-separated lists:

```c
resources: { use_list : acquire_list : hold_list }
```

*   **`use_list`** *(1st section)*: Resources that must be **free at
    dispatch/issue time** (checked only, not reserved).
*   **`acquire_list`** *(2nd section)*: Resources that must be **free at
    dispatch time AND reserved** for their specified `[begin..end]` cycle
    window.
*   **`hold_list`** *(3rd section)*: Resources **reserved** for their
    `[begin..end]` cycle window *without* checking whether they are already free
    at dispatch time.

Each item can be a scalar `name` or list `[name]` with an optional cycle range:

| Syntax           | Start Cycle | End Cycle        | Classification          |
| :--------------- | :---------- | :--------------- | :---------------------- |
| `rd` or `rd[]`   | `0` (issue) | `rd`'s dest      | Simple                  |
:                  :             : latency (or `0`) : (`SimpleResourceEnum`)  :
| `rd[..3]` or     | `0` (issue) | Cycle `3`        | Simple                  |
: `rd[0..3]`       :             :                  : (`SimpleResourceEnum`)  :
| `alu_pipe[1..3]` | Cycle `1`   | Cycle `3`        | Complex                 |
:                  :             :                  : (`ComplexResourceEnum`) :
| `mul_wb[2..]`    | Cycle `2`   | Matching dest    | Complex                 |
:                  :             : latency (or `0`) : (`ComplexResourceEnum`) :

*   Resources that are only ever reserved starting at cycle `0` become **Simple
    Resources** (`SimpleResourceEnum`, efficiently tracked with a single
    bitvector).
*   Any resource reserved starting at a non-zero cycle (`begin_cycle != 0`) in
    the ISA is automatically classified as a **Complex Resource**
    (`ComplexResourceEnum`, or `ListComplexResourceEnum` for `[name]`).
*   Common resource patterns can be named once at the top of a `slot` and reused
    across opcodes:

```c
slot my_slot {
  resources ThreeOp = { next_pc, rs1, rs2 : rd[..rd] };
  opcodes {
    add { : rs1, rs2 : rd }, resources: ThreeOp, semfunc: "&MyAdd";
  }
}
```

### 4. Instruction Attributes (`attributes:`)

Instruction attributes attach compile-time `int` metadata flags/values (such as
branch markers, privilege levels, or memory access tags) to decoded
`Instruction`s:

```c
slot my_slot {
  default attributes = { is_branch = 0, is_mem = 0, priv_level = 0 };
  opcodes {
    beq { : rs1, rs2, b_imm : next_pc },
      attributes: { is_branch = 1 },
      semfunc: "&MyBeq";
  }
}
```

*   `decoder_gen` emits `namespace my_slot { enum class AttributeEnum {
    kIsBranch = 0, kIsMem = 1, kPrivLevel = 2, kPastMaxValue = 3 }; }` in
    `<prefix>_enums.h`.
*   At runtime, query attributes on an `Instruction*` via
    `inst->Attributes()[static_cast<int>(my_slot::AttributeEnum::kIsBranch)]`.
*   Always write explicit `= value` assignments (such as `is_branch = 1`) when
    setting non-zero flags: omitting `= expr` (e.g., `{ is_branch }`) registers
    the attribute name in `AttributeEnum` with value `0`. Unmentioned attributes
    inherit the slot's `default attributes` value (or `0` if none).

### 5. Explicit Destination Latency Attribute (`latency:`)

```c
latency: rd(3), [rlist](2)
```

Sets or overrides latencies on destination operands already declared in
`{ ... }`. This is particularly handy when customizing an inherited opcode in a
derived slot using `= override`.

--------------------------------------------------------------------------------

## 7. Slots, Inheritance, Templates, and VLIW Bundles

### Slot Defaults

Every **referenced** slot (one listed in `isa` or a `bundle`) **must** define
`default opcode` with a `semfunc:`:

```c
slot alu_slot {
  default size = 4;
  default latency = 1;
  default attributes = { is_alu = 1 };
  default opcode =
    disasm: "illegal",
    semfunc: "&HandleIllegalInstruction";
  opcodes { ... }
}
```

`default opcode` is used both for `OpcodeEnum::kNone` (when binary decoding
finds no match) and as the fallback semantic function for any opcode in the slot
that omits `semfunc:`. Base slots used *only* for inheritance do not need
`default opcode`.

### Slot Inheritance (`: base`), `= delete`, and `= override`

You can factor modular ISA extensions or shared VLIW slot subsets into base
slots and combine them in derived slots:

```c
slot rv32i { opcodes { add { : rs1, rs2 : rd }, semfunc: "&Add"; sub { : rs1, rs2 : rd }, semfunc: "&Sub"; } }
slot rv32m { opcodes { mul { : rs1, rs2 : rd(3) }, semfunc: "&Mul"; } }

slot core_slot : rv32i, rv32m {
  default opcode = semfunc: "&Illegal";
  opcodes {
    // Exclude an opcode inherited from a base slot:
    sub = delete;

    // Customize attributes (latency, semfunc, disasm, resources, attributes)
    // of an inherited opcode without redeclaring its operands:
    mul = override,
      latency: rd(5),
      semfunc: "&SlowMul";
  }
}
```

Slot inheritance must form a **tree** (inheriting from the same ancestor twice
via diamond inheritance, or inheriting the same opcode name from two base slots,
is reported as an error).

### Templated Base Slots (`template <int ...>`)

When two slots or cores share the same instructions but have different pipeline
latencies, parameterize the base slot with `template <int ...>`:

```c
template <int alu_lat, int mul_lat>
slot base_ops {
  opcodes {
    add { : rs1, rs2 : rd(alu_lat) }, semfunc: "&Add";
    mul { : rs1, rs2 : rd(mul_lat) }, semfunc: "&Mul";
  }
}

slot fast_core : base_ops<1, 2> {
  default opcode = semfunc: "&Illegal";
}

slot deep_pipe_core : base_ops<2, 5> {
  default opcode = semfunc: "&Illegal";
}
```

### Replicated Slots (`slot name[N]`) and VLIW `bundle` Declarations

In a VLIW architecture, an instruction word (bundle) issues multiple slots in
parallel, and some slots may appear multiple times (for example, 4 identical
vector ALU slots and 2 load slots):

```c
isa ExampleVliw {
  namespace sim::vliw;
  bundles {
    top_bundle;
  }
}

bundle top_bundle {
  bundles {
    scalar_subbundle;
    vector_subbundle;
  }
  semfunc: "&DispatchTopBundle";
}

bundle scalar_subbundle {
  slots {
    scalar_slot[0, 1];
  }
  semfunc: "&DispatchScalarSubbundle";
}

bundle vector_subbundle {
  slots {
    vec_alu[0..3];
    vec_mem;
  }
  semfunc: "&DispatchVectorSubbundle";
}

slot scalar_slot[2] {
  default latency = 1;
  default opcode = semfunc: "&Nop";
  opcodes { ... }
}

slot vec_alu[4] {
  default latency = 1;
  default opcode = semfunc: "&Nop";
  opcodes { ... }
}

slot vec_mem {
  default latency = 2;
  default opcode = semfunc: "&Nop";
  opcodes { ... }
}
```

Rules for replicated slots and bundles:

*   When a slot declares `[N]` instances (`slot vec_alu[4]`), **every instance
    `0 .. N-1`** must be referenced at least once in `slots { ... }`. Use
    `[0..3]` or `[0, 2]` to select specific instance indices (`entry` passed to
    `GetOpcode(SlotEnum, int entry)`).
*   Unreferenced base slots and `template` slots may **not** specify `[N]`.
*   Every `bundle` must contain at least one child bundle or slot and specify a
    bundle-level `semfunc:` responsible for executing its decoded
    sub-instructions.

--------------------------------------------------------------------------------

## 8. Bazel Build Rule and Generated C++ API Reference

### `mpact_isa_decoder` Build Rule

```python
load("//third_party/mpact_sim/decoder:mpact_sim_isa.bzl", "mpact_isa_decoder")
# (Or "@mpact-sim//mpact/sim/decoder:mpact_sim_isa.bzl" in standalone Bazel)

mpact_isa_decoder(
    name = "mini_risc_isa",
    src = "mini_risc.isa",           # Or srcs = [...] for multiple top-level files
    includes = [],                   # Any #include'd .isa files
    isa_name = "MiniRisc",           # Selects 'isa MiniRisc'
    prefix = "mini_risc",            # Output file prefix (defaults to src stem)
    generator = 1,                   # Set generator = 2 to pass Instruction* to getters
    deps = [":mini_risc_semfuncs"],  # Libraries declaring your semantic functions
)
```

### Generated Files (`prefix = "mini_risc"`, `isa MiniRisc`)

| Generated File                | Key Generated Types & Symbols                |
| :---------------------------- | :------------------------------------------- |
| `mini_risc_enums.h` / `.cc`   | `SlotEnum`, `OpcodeEnum`, `PredOpEnum`,      |
:                               : `SourceOpEnum`, `ListSourceOpEnum`,          :
:                               : `DestOpEnum`, `ListDestOpEnum`,              :
:                               : `SimpleResourceEnum`, `ComplexResourceEnum`, :
:                               : `ListComplexResourceEnum`, `k*Names[]`       :
:                               : string tables, and                           :
:                               : `<slot>\:\:AttributeEnum`.                   :
| `mini_risc_decoder.h` / `.cc` | `MiniRiscEncodingBase`, `<SlotPascal>Slot`,  |
:                               : `<BundlePascal>Decoder`,                     :
:                               : `MiniRiscInstructionSetFactory`, and         :
:                               : `MiniRiscInstructionSet`.                    :
| `mini_risc_encoder.h` / `.cc` | `MiniRiscEncoderInterfaceBase` and RE2-based |
:                               : `<SlotPascal>Matcher` classes generated from :
:                               : `disasm\:` patterns for assembly text        :
:                               : encoding.                                    :

### Virtual Methods on `<IsaName>EncodingBase`

Your C++ encoding class inherits from `<IsaName>EncodingBase`. Only `GetOpcode`
is pure virtual; override whichever operand/resource getters your `.isa` uses:

```cpp
class MiniRiscEncodingBase {
 public:
  virtual ~MiniRiscEncodingBase() = default;

  // Pure virtual: return OpcodeEnum for the given slot and instance index:
  virtual OpcodeEnum GetOpcode(SlotEnum slot, int entry) = 0;

  // Override if any opcode uses predicates:
  virtual PredicateOperandInterface* GetPredicate(
      SlotEnum slot, int entry, OpcodeEnum opcode, PredOpEnum pred_op);

  // Override for scalar and '[list]' source operands:
  virtual SourceOperandInterface* GetSource(
      SlotEnum slot, int entry, OpcodeEnum opcode, SourceOpEnum source_op,
      int source_no);
  virtual std::vector<SourceOperandInterface*> GetSources(
      SlotEnum slot, int entry, OpcodeEnum opcode,
      ListSourceOpEnum list_source_op, int source_no);

  // Override for scalar and '[list]' destination operands:
  virtual DestinationOperandInterface* GetDestination(
      SlotEnum slot, int entry, OpcodeEnum opcode, DestOpEnum dest_op,
      int dest_no, int latency);
  virtual std::vector<DestinationOperandInterface*> GetDestinations(
      SlotEnum slot, int entry, OpcodeEnum opcode,
      ListDestOpEnum list_dest_op, int dest_no,
      const std::vector<int>& latency);

  // Override if any destination operand uses dynamic latency '(*)':
  virtual int GetLatency(
      SlotEnum slot, int entry, OpcodeEnum opcode, DestOpEnum dest_op,
      int dest_no);

  // Override if your .isa models simple or complex pipeline resources:
  virtual ResourceOperandInterface* GetSimpleResourceOperand(
      SlotEnum slot, int entry, OpcodeEnum opcode,
      SimpleResourceVector& resource_vec, int end);
  virtual ResourceOperandInterface* GetComplexResourceOperand(
      SlotEnum slot, int entry, OpcodeEnum opcode,
      ComplexResourceEnum resource, int begin, int end);
  virtual std::vector<ResourceOperandInterface*> GetComplexResourceOperands(
      SlotEnum slot, int entry, OpcodeEnum opcode,
      ListComplexResourceEnum resource, int begin, int end);
};
```

*(Tip: If you set `generator = 2` in `mpact_isa_decoder`, `Instruction* inst` is
passed as the first parameter to all operand, latency, and resource getter
methods on `<IsaName>EncodingBase`.)*

--------------------------------------------------------------------------------

## 9. Best Practices from Production Simulators

Real-world MPACT-Sim simulators—including **`mpact_riscv`** (RV32G/RV64G,
Vector, Bitmanip, Compressed `Zc*`, and the RVM23 profile), **`mpact_cheriot`**
(CHERIoT capability-enabled RISC-V + RVV), and the **Coral NPU** simulator
(RISC-V scalar control core + custom NPU vector extensions)—use a consistent set
of `.isa` idioms to keep large, multi-variant instruction sets modular and
maintainable.

### 1. Factor Complex ISAs into One Base Slot per Extension or Functional Unit

Avoid defining hundreds of instructions in a single monolithic `slot`. Instead,
model each ISA extension or execution functional unit as its own **unreferenced
base `slot`**, splitting large extensions into separate `.isa` files:

*   **`mpact_riscv`** defines separate base slots for `riscv32i`, `riscv32m`,
    `riscv32_amo_arithmetic`, `riscv32f`, `riscv32d`, `zicsr`, `zfencei`,
    `privileged`, `riscv32c`, `riscv32_hints`, plus standalone extension files
    such as `riscv32zb.isa`, `riscv_vector.isa`, `riscv_zc.isa`,
    `riscv_zicond.isa`, and `riscv_zicbom.isa`.
*   **`mpact_cheriot`** factors capability base instructions
    (`riscv_cheriot.isa`), single-precision floating point
    (`riscv_cheriot_f.isa`), integer RVV (`riscv_cheriot_vector.isa`), and FP
    RVV (`riscv_cheriot_vector_fp.isa`) into separate files and slots.
*   **Coral NPU** splits its ISA across `coralnpu_base.isa`,
    `coralnpu_arith.isa`, `coralnpu_mul.isa`, `coralnpu_shift.isa`, and
    `coralnpu_memory.isa`, while directly `#include`-ing `riscv32zb.isa` from
    `mpact_riscv`!

Note that these building-block slots **omit `default opcode`** because they are
only used via inheritance.

### 2. Compose Simulator Profiles Using Thin Top-Level `.isa` Files

With extensions factored into modular `.isa` files and base slots, defining a
complete processor variant or profile takes only a few dozen lines: `#include`
the extension `.isa` files, declare a top-level composite `slot` that inherits
from the desired subset of base slots, and define `default opcode` once at the
top level.

For example, `rvm23.isa` in `mpact_riscv` assembles 25 extension slots into a
single profile cleanly:

```c
// rvm23.isa
isa RVM23 {
  namespace mpact::sim::riscv::rvm23;
  slots { rvm23; }
}

#include "riscv32g.isa"
#include "riscv32zb.isa"
#include "riscv_vector.isa"
#include "riscv_zc.isa"
#include "riscv_zicond.isa"
// ... other Z* extension .isa files ...

slot rvm23 :
    riscv32i, riscv32m, riscv32_amo_arithmetic, riscv32f, riscv32d,
    riscv32_hints, zicsr, zfencei, privileged, riscv_vector,
    riscv32_zb, riscv32_zicond /* ... */ {
  default opcode =
    disasm: "Illegal instruction at %(@:08x)",
    semfunc: "&RiscVIllegalInstruction";
}
```

Similarly, a single wrapper file can define **multiple `isa` targets** for
closely related hardware configurations. For example, `riscv_cheriot_rvv.isa`
defines both `isa RiscVCheriotRVV`
(`slot riscv_cheriot_rvv : riscv32_cheriot, riscv_cheriot_vector`) and
`isa RiscVCheriotRVVFp` (adding `riscv_cheriot_vector_fp` and
`riscv_cheriot_f`), allowing two Bazel `mpact_isa_decoder` rules to generate
integer-only and FP-enabled decoders from the same `.isa` source via
`isa_name = "..."`.

### 3. Scope C++ `includes` to Individual Slots and Fully Qualify `semfunc` Names

When factoring `.isa` files for reuse across multiple targets (or even across
different repositories, as Coral NPU reuses `mpact_riscv` extensions), follow
two C++ hygiene rules:

1.  **Place semantic-function `#include` headers inside the `slot` that uses
    them, not at global scope**: For example, put
    `#include "riscv_f_instructions.h"` inside
    `slot riscv32f { includes { ... } }` and vector headers inside
    `slot riscv_vector { includes { ... } }`. Because `decoder_gen` only emits
    `includes` from slots reachable by the chosen `isa`, integer-only builds
    never pull in floating-point or vector C++ headers.
2.  **Fully qualify C++ namespaces from the global `::` root in shared
    `semfunc:` strings**: Writing
    `semfunc: "&::mpact::sim::riscv::RV32::RiscVIAdd"` instead of
    `"&RV32::RiscVIAdd"` guarantees the generated `_decoder.cc` compiles cleanly
    even when another simulator imports that slot into a completely different
    `isa` C++ namespace (such as `coralnpu::sim::isa32`).

### 4. Maximize Semantic Function Reuse via Operand Abstraction and `absl::bind_front`

Because semantic functions read and write abstract `SourceOperandInterface` and
`DestinationOperandInterface` objects, a single C++ function can often implement
many `.isa` opcodes:

*   **Share semantics between compact (16-bit) and standard (32-bit)
    encodings**: In `mpact_riscv` and `mpact_cheriot`, 16-bit compressed
    instructions reuse the exact same C++ semantic functions as 32-bit
    instructions by supplying hardwired register operands (`x0`, `x2`) or
    dedicated immediate operand names in `.isa`:

    ```c
    // 32-bit register add, 32-bit immediate add, 16-bit c.mv, and 16-bit c.li
    // all bind to the exact same C++ function (&RiscVIAdd)!
    add  { : rs1, rs2 : rd },        semfunc: "&RiscVIAdd";
    addi { : rs1, I_imm12 : rd },    semfunc: "&RiscVIAdd";
    cmv  { : crs2, x0 : rd },        semfunc: "&RiscVIAdd";
    cli  { : x0, I_ci_imm6 : rd },   semfunc: "&RiscVIAdd";
    ```

*   **Use synthetic "always-true" predicate/mask operands for unpredicated
    variants**: In `riscv_vector.isa` and Coral NPU, masked vector loads
    (`vle8`) pass `vmask`, while unmasked encodings (`vle8_vm1`) pass a
    synthetic `vmask_true` operand
    (`vle8_vm1 { (: rs1, const1, vmask_true), (: : vd) }`) whose C++ getter
    returns a constant all-ones mask operand—allowing one C++ implementation to
    handle both masked and unmasked execution.

*   **Bind element widths or mode flags with `absl::bind_front`**: Instead of
    writing separate wrappers for 8-, 16-, 32-, and 64-bit vector loads or
    `vsetvli` `rd`/`rs1 == x0` variants, bind compile-time parameters directly
    in `semfunc:`:

    ```c
    vle8  { (: rs1, const1, vmask), (: : vd) },
      semfunc: "absl::bind_front(&VlStrided, /*elem_width=*/1)", "&VlChild";
    vle16 { (: rs1, const2, vmask), (: : vd) },
      semfunc: "absl::bind_front(&VlStrided, /*elem_width=*/2)", "&VlChild";
    ```

### 5. Separate Side-Effect-Free Variants and Architectural Hints into Distinct Opcodes

Pairing `.bin_fmt` field constraints (`rd == 0`, `rs1 == 0`) with distinct
`.isa` opcodes makes both functional simulation and hazard tracking cleaner and
more accurate:

*   **CSR read/write suppression (`zicsr`)**: `mpact_riscv` defines
    `csrrs { : rs1, csr : rd, csr }` alongside `csrrs_nw { : csr : rd }`
    (decoded when `rs1 == 0`) and `csrrs_nr { : rs1, csr : csr }` (decoded when
    `rd == 0`). Notice that `csrrs_nw` omits `csr` from its destination operand
    and resource-acquire lists, accurately modeling both the absence of CSR
    write side effects and the absence of CSR write hazards.
*   **Isolating hint instructions (`slot riscv32_hints`)**: Encoding `rd == 0`
    hint variants (`addi_hint1`, `slli_hint1`, etc.) in a separate
    `riscv32_hints` slot bound to `&RiscVINop` keeps `riscv32i` clean while
    letting microarchitectural models selectively `= override` specific hints
    (such as semihosting sequences or prefetch hints) in derived slots.

### 6. Name Reusable Hazard Patterns per Slot (`resources`)

Rather than repeating `{ next_pc, rs1, rs2 : rd[..rd] }` on dozens of ALU and
floating-point opcodes, declare named resource groups (`TwoOp`, `ThreeOp`,
`FourOp`) at the top of each `slot` (as done throughout `mpact_riscv` and Coral
NPU):

```c
slot riscv32f {
  resources TwoOp   = { next_pc, frs1 : frd[0..] };
  resources ThreeOp = { next_pc, frs1, frs2 : frd[0..] };
  resources FourOp  = { next_pc, frs1, frs2, frs3 : frd[0..] };
  opcodes {
    fadd_s  { : frs1, frs2, rm : frd },       resources: ThreeOp, ...;
    fmadd_s { : frs1, frs2, frs3, rm : frd }, resources: FourOp,  ...;
  }
}
```

--------------------------------------------------------------------------------

## 10. Common Pitfalls and Troubleshooting Checklist

*   **Missing `default opcode` on a referenced slot**: Every slot referenced by
    `isa` or `bundle` **must** declare `default opcode = semfunc: "...";`.
    Without it, `decoder_gen` reports an error.
*   **Missing destination operand latency**: If a slot does not set
    `default latency = <expr>;`, then every destination operand in that slot
    must explicitly specify either `(expr)` or `(*)`.
*   **Mismatched `semfunc:` count on multi-part instructions**: If an opcode has
    a parent and child tuple `{ (: rs1, imm12), (: : rd) }`, `semfunc:` must
    list **two** strings (`semfunc: "&LoadIssue", "&LoadWriteback"`), not one.
*   **Using `const Instruction*` in semantic functions**: MPACT-Sim's
    `SemanticFunction` signature is `void(Instruction*)` (non-const), and
    `std::bind` placeholders live in `std::placeholders::_1`.
*   **Unreferenced slot instances**: If you declare `slot alu[4]`, referencing
    only `alu[0..2]` in `slots { ... }` is an error—all 4 instances (`0..3`)
    must be placed in the ISA or bundles.
*   **Leading zero without width in `disasm` format**: Write `%(imm:x)`
    (unpadded hex) or `%(imm:08x)` (zero-padded 8-digit hex), **not**
    `%(imm:0x)`.

--------------------------------------------------------------------------------

## 11. Grammar Reference

The complete ANTLR4 grammar for `.isa` files is located in `InstructionSet.g4`
in the `decoder/` directory.
