# Overview

This directory contains C# code that is loaded into renode. It implements the
interface ReNode uses to interact with CPU type bus objects. This code
translates ReNode calls to the "C" callable interface that is implemented by the
mpact_sim/util/renode C++ library code, and which is linked with an mpact
simulator instance. This library code translates and forwards the required calls
to the `RenodeDebugInterface` that is implemented separately for each mpact
simulator target. For details see the specific targets of interest.

## ReNode file MpactBaseCPU.cs

The C# code in MpactBaseCPU.cs implements two classes: `MpactBaseCPU` and
`MpactPeripheral`. The first derives from ReNode's `BaseCPU` class, and provides
a CPU interface to ReNode that dynamically loads and controls an mpact simulator
in a dynamic library using a small "C" linkage interface. The second implements
a bus target interface so that any memory the simulator manages can be made
accessible to the ReNode system model from the system bus.

The MpactBaseCPU.cs should be placed in the renode subdirectory
`src/Plugins/MpactPlugin`.

The CPU can be instantiated as follows:

```
mytype: MpactCPU.MpactBaseCPU @ sysbus
    id: 1
    cpuType: "Mpact.MyType"
    endianness: Endianess.LittleEndian
    memoryBase: 0x80000000
    memorySize: 0x10000000
```

This instantiates an object of MpactCPU.MpactBaseCPU, connected to sysbus with
the instance name mytype. The object is constructed using the parameters listed
below.

The peripheral can be instantiated as follows:

```
mytype_mem : MpactCPU.MpactPeripheral @ sysbus 0x80000000
    size: 0x10000000
    baseAddress: 0x80000000
    mpactCpu: mytype
```

In this case, a bus target is defined at address 0x8000'0000 on sysbus, with a
size of 0x1000'0000, and connects with the mytype CPU instance. When ReNode
makes references to this peripheral, only the offset from the mapped address is
passed on. The value of the constructor parameter baseAddress is added to every
such reference. It does not have to be the same as the address to which the
peripheral is added to the bus, thus allowing the requests to be remapped in
memory.

## Mpact simulator interface

The library code defines and implements code for the "C" linkage interface that
is called by `MpactBaseCPU`, and the code necessary to manage one or multiple
instances of mpact simulators. This code is linked in with the simulator and a
simulator target specific implementation of the `RenodeDebugInterface`.

The name of the dynamic library is passed to ReNode by setting the instance
property CpuLibraryPath in either the ReNode platform definition (.repl) or
script (.resc) file.

## MpactRiscVCPU.cs

The `MpactRiscVCPU` class in MpactRiscVCPU.cs inherits from `MpactBaseCPU`. It
adds 4 configuration items to the base class:

*   `InstProfile` is a boolean that when true enables instruction profiling in
    the simulator. Following execution there will be a `.csv` file that contains
    a table of instruction addresses and the number of times each instruction
    was executed.
*   `MemProfiler` is a boolean that when true enables data profiling in the
    simulator. Following execution there will be a `.csv` file that contains a
    table of first and last word addresses in a range, and the size of the
    range. It does not contain an access count.
*   `StackEnd` is a ulong that contains the address of the end of the stack.
    Some RiscV libraries don't properly initialize the stack pointer (i.e., it
    is left at 0x0). Setting the stack end and stack size will cause the
    simulator to initialize the stack pointer to the value of the sum of stack
    end and stack size.
*   `StackSize` is a ulong that contains the size to use for the stack.

The MpactRiscVCPU class can be used with either the `librenode_mpact_riscv32.so`
or the `librenode_mpact_riscv64.so` targets in the mpact_riscv repository.

## MpactCheriotCPU.cs

The `MpactCheriotCPU` class in MpactCheriotCPU.cs inherits from `MpactBaseCPU`.
It adds 4 configuration items to the base class:

*   `InstProfile` is a boolean that when true enables instruction profiling in
    the simulator. Following execution there will be a `.csv` file that contains
    a table of instruction addresses and the number of times each instruction
    was executed.
*   `MemProfiler` is a boolean that when true enabled data profiling in the
    simulator. Following execution there will be a `.csv` file that contains a
    table of first and last word addresses in a range, and the size of the
    range. It does not contain an access count.

The MpactCheriotCPU class can be used with the `librenode_mpact_cheriot.so`
target in the mpact_cheriot repository.
