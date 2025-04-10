//
// Copyright (c) 2010-2024 Antmicro
//
// This file is licensed under the MIT License.
// Full license text is available in 'licenses/MIT.txt'.
//
//
// Copyright (c) 2024 Google LLC
//
// Use of this source code is governed by an MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT.
//

// This file is derived from ReNode plugin sources, but has been modified
// so that it can be used as a plugin that interfaces with an mpact based
// simulator that implement the mpact_renode "C" linkage interface.

using System;
using System.Linq;
using System.Runtime.InteropServices;
using System.Threading;
using System.Collections.Generic;
using System.Collections.Concurrent;
using Antmicro.Renode.Core;
using Antmicro.Renode.Debugging;
using Antmicro.Renode.Exceptions;
using Antmicro.Renode.Hooks;
using Antmicro.Renode.Logging;
using Antmicro.Renode.Peripherals;
using Antmicro.Renode.Peripherals.Bus;
using Antmicro.Renode.Peripherals.CPU;
using Antmicro.Renode.Peripherals.Timers;
using Antmicro.Renode.Peripherals.CPU.Disassembler;
using Antmicro.Renode.Peripherals.CPU.Registers;
using Antmicro.Renode.Utilities;
using Antmicro.Renode.Time;
using Antmicro.Renode.Utilities.Binding;
using ELFSharp.ELF;
using ELFSharp.UImage;

namespace Antmicro.Renode.Peripherals.MpactCPU {

// This interface is implemented by MpactCheriotCPU and used by
// MpactCheriotPeripheral to access memory in mpact_cheriot simulator
public interface IMpactPeripheral :  IBytePeripheral, IWordPeripheral,
                                     IDoubleWordPeripheral,
                                     IQuadWordPeripheral,
                                     IMultibyteWritePeripheral {}


// Struct containing info for loading binary image files.
public struct BinFileLoadInfo {
    public string file_name;
    public UInt64 load_address;
    public UInt64 entry_point;
}

// The MpactCPU class. This class derives from BaseCPU, which implements
// a CPU in ReNode. It is the base implementation of the interface between
// ReNode and the mpact_sim based simulators.
public class MpactBaseCPU : BaseCPU, ICPUWithRegisters,
                            IGPIOReceiver, ITimeSink,
                            IDisposable, IMpactPeripheral {

    // Structure for ReNode register information that is obtained from the
    // simulator.
    [StructLayout(LayoutKind.Explicit, CharSet = CharSet.Ansi, Pack = 1)]
    private struct RegInfo {
        [FieldOffset(0)]
        public Int32 index;
        [FieldOffset(4)]
        public Int32 width;
        [FieldOffset(8)]
        public bool isGeneral;
        [FieldOffset(9)]
        public bool isReadOnly;
    }

    public MpactBaseCPU(uint id, UInt64 memoryBase, UInt64 memorySize,
                    string cpuType, IMachine machine, Endianess endianness,
                    CpuBitness bitness = CpuBitness.Bits32)
        : base(id, cpuType, machine, endianness, bitness) {
        this.cpu_type = cpuType;
        this.memoryBase = memoryBase;
        this.memorySize = memorySize;
        // Allocate space for marshaling data to/from simulator.
        error_ptr = Marshal.AllocHGlobal(4);
        value_ptr = Marshal.AllocHGlobal(8);
        reg_info_ptr = Marshal.AllocHGlobal(Marshal.SizeOf<RegInfo>());
        string_ptr = Marshal.AllocHGlobal(maxStringLen);
    }

    ~MpactBaseCPU() {
        Marshal.FreeHGlobal(error_ptr);
        Marshal.FreeHGlobal(value_ptr);
        Marshal.FreeHGlobal(reg_info_ptr);
        Marshal.FreeHGlobal(string_ptr);
    }


    public override string Architecture { get { return "MpactSim";} }

    public virtual void SetMpactConfig(List<string> config_names,
		               List<string> config_values) {
        config_names.Add("memoryBase");
        config_names.Add("memorySize");
        config_values.Add("0x" + memoryBase.ToString("X"));
        config_values.Add("0x" + memorySize.ToString("X"));
        if (cli_port != 0) {
            config_names.Add("cliPort");
            config_values.Add("0x" + cli_port.ToString("X"));
                config_names.Add("waitForCLI");
            config_values.Add("0x" + (wait_for_cli ? 1 : 0).ToString("X"));
        }
    }

    public override void Start() {
        base.Start();

        // Set up configuration array.
        var config_names = new List<string>();
        var config_values = new List<string>();
	SetMpactConfig(config_names, config_values);
	// Configure mpact sim.
        Int32 cfg_res = set_config(mpact_id, config_names.ToArray(),
                                   config_values.ToArray(), config_names.Count);
        if (cfg_res < 0) {
            LogAndThrowRE("Failed to set configuration information");
        }
        // Load executable file, symbols only from executable file, or bin
	// file if specified. Use symbols load if the executable is loaded
	// by the system.
        this.Log(LogLevel.Info, "symbol_file: '" + symbol_file + "'");
        if (!executable_file.Equals("")) {
            var entry_point = load_elf(mpact_id, executable_file,
                                       /*for_symbols_only=*/false,
                                       error_ptr);
            Int32 result = Marshal.ReadInt32(error_ptr);
            if (result < 0) {
                LogAndThrowRE("Failed to load executable");
            }
            PC = entry_point;
        } else if (!symbol_file.Equals("")) {
            this.Log(LogLevel.Info, "loading symbol_file: '" + symbol_file + "'");
            load_elf(mpact_id, symbol_file, /*for_symbols_only=*/true,
                     error_ptr);
            Int32 result = Marshal.ReadInt32(error_ptr);
            if (result < 0) {
                LogAndThrowRE("Failed to load symbols");
            }
        } else if (!bin_file_info.file_name.Equals("")) {
            Int32 res = load_image(mpact_id, bin_file_info.file_name,
                                   bin_file_info.load_address);
            if (res < 0) {
                LogAndThrowRE("Failed to load binary image");
            }
            PC = bin_file_info.entry_point;
        }
    }

    public override void Reset() {
        base.Reset();
        instructionsExecutedThisRound = 0;
        totalExecutedInstructions = 0;
        // Call simulator to reset.
        reset(mpact_id);
    }

    public override void Dispose() {
        base.Dispose();
        // Cleanup: simulator and any unmanaged resources.
        this.Log(LogLevel.Info, "destruct mpact");
        destruct(mpact_id);
        this.Log(LogLevel.Info, "done");
    }

    public string CpuLibraryPath {
        get {
          return cpu_library_path;
        }
        set {
                this.Log(LogLevel.Info, "Lib: " + value);
            if (String.IsNullOrWhiteSpace(value)) {
                cpu_library_path = value;
                return;
            }
            if (cpu_library_bound) {
                LogAndThrowRE("CPU library already bound to: "
                              + cpu_library_path);
            }
            cpu_library_path = value;
            try {
                binder = new NativeBinder(this, cpu_library_path);
            }
            catch (System.Exception e) {
                LogAndThrowRE("Failed to load CPU library: " + e.Message);
            }
            cpu_library_bound = true;

            // Instantiate the simulator, passing in callbacks to read and write
            // from/to the system bus.
            read_sysmem_delegate =
                    new FuncInt32UInt64IntPtrInt32_(ReadSysMemory);
            write_sysmem_delegate =
                    new FuncInt32UInt64IntPtrInt32_(WriteSysMemory);
            mpact_id = construct_with_sysbus(cpu_type, maxStringLen,
                                             read_sysmem_delegate,
                                             write_sysmem_delegate);
            if (mpact_id < 0) {
                LogAndThrowRE("Failed to create simulator instance");
            }
        }
    }

    public string ExecutableFile {
        get => executable_file;
        set => executable_file = value;
    }

    public string SymbolFile {
        get => symbol_file;
        set => symbol_file = value;
    }

    public BinFileLoadInfo BinFileInfo {
        get => bin_file_info;
        set => bin_file_info = value;
    }

    public ushort CLIPort { get => cli_port; set => cli_port = value; }

    public bool WaitForCLI {
        get => wait_for_cli;
        set => wait_for_cli = value;
    }

    public override ulong ExecutedInstructions => totalExecutedInstructions;

    public override ExecutionMode ExecutionMode {
        get { return executionMode; }
        set {
            lock(singleStepSynchronizer.Guard) {
                if (executionMode == value) return;

                executionMode = value;

                singleStepSynchronizer.Enabled = IsSingleStepMode;
                UpdateHaltedState();
            }
        }
    }

    [Register]
    public override RegisterValue PC {
        get {
            if (registerMap.Count == 0) {
                GetMpactRegisters();
            }
	    return GetRegister(PC_ID);
        }

        set {
            Int32 error = write_register(mpact_id, PC_ID, value);
            if (error < 0) {
                this.Log(LogLevel.Error, "Failed to write PC");
            }
        }
    }

    public override ExecutionResult ExecuteInstructions(
                    ulong numberOfInstructionsToExecute,
                    out ulong numberOfExecutedInstructions) {
        UInt64 instructionsExecutedThisRound = 0UL;
        ExecutionResult result = ExecutionResult.Ok;
        try {
            // Invoke simulator for the number of instructions.
            instructionsExecutedThisRound =
                    step(mpact_id, numberOfInstructionsToExecute, error_ptr);
            // Parse the result.
            Int32 step_result = Marshal.ReadInt32(error_ptr);
            switch (step_result) {
                case -1:
                    result = ExecutionResult.Aborted;
                    break;
                case 0:
                    result = ExecutionResult.Ok;
                    break;
                case 1:
                    result = ExecutionResult.Interrupted;
                    break;
                case 2:
                    result = ExecutionResult.WaitingForInterrupt;
                    break;
                case 3:
                    result = ExecutionResult.StoppedAtBreakpoint;
                    break;
                case 4:
                    result = ExecutionResult.StoppedAtWatchpoint;
                    break;
                case 5:
                    result = ExecutionResult.ExternalMmuFault;
                    break;
                default:
                    LogAndThrowRE("Unknown return value from step - "
                                  + step_result);
                    break;
                }
        }
        catch (Exception) {
            this.NoisyLog("CPU exception detected, halting.");
            InvokeHalted(new HaltArguments(HaltReason.Abort, this));
            return ExecutionResult.Aborted;
            }
        finally {
            numberOfExecutedInstructions = instructionsExecutedThisRound;
            totalExecutedInstructions += instructionsExecutedThisRound;
        }
        return result;
    }

    // ICPUWithRegisters methods implementations.
    public  void SetRegister(int register, RegisterValue value) {
        var status = write_register((Int32)mpact_id, (Int32)register,
                                    (UInt64)value);
        if (status < 0) {
            LogAndThrowRE("Failed to write register " + register);
        }
    }

    public  void SetRegisterUnsafe(int register, RegisterValue value) {
        SetRegister(register, value);
    }

    public  RegisterValue GetRegister(int register) {
        var status = read_register(mpact_id, register, value_ptr);
        if (status < 0) {
            LogAndThrowRE("Failed to read register " + register);
        }
        Int64 value = Marshal.ReadInt64(value_ptr);
        CPURegister cpu_register;
        if (registerMap.TryGetValue(register, out cpu_register)) {
            var width = cpu_register.Width;
            switch (width) {
                case 8: return (byte)value;
                case 16: return (ushort)value;
                case 32: return (uint)value;
                case 64: return (ulong)value;
                default:
                    LogAndThrowRE("Unexpected register width: {width}");
                    break;
            }
        } else {
	    string msg = $"Unknown register id: {register} not found";
            LogAndThrowRE(msg);
        }
        return (ulong)0;
    }

    public  RegisterValue GetRegisterUnsafe(int register) {
        return GetRegister(register);
    }

    protected void GetMpactRegisters() {
        if (registerMap.Count != 0) return;
        Int32 num_regs = get_reg_info_size(mpact_id);
        for (Int32 i = 0; i < num_regs; i++) {
            Int32 result = get_reg_info(mpact_id, i, string_ptr, reg_info_ptr);
            if (result < 0) {
                this.Log(LogLevel.Error,
                         "Failed to get register info for index " + i);
                continue;
            }
            var reg_info = Marshal.PtrToStructure<RegInfo>(reg_info_ptr);
            var cpu_reg = new CPURegister(reg_info.index, reg_info.width,
                                          reg_info.isGeneral,
                                          reg_info.isReadOnly);
            var reg_name = Marshal.PtrToStringAuto(string_ptr);
            registerMap.Add(reg_info.index, cpu_reg);
            registerNamesMap.Add(reg_name, reg_info.index);
        }
    }

    public  IEnumerable<CPURegister> GetRegisters() {
        if (registerMap.Count == 0) {
            GetMpactRegisters();
        }
        return registerMap.Values.OrderBy(x => x.Index);
    }

    public new string[,] GetRegistersValues() {
        if (registerMap.Count == 0) {
            GetMpactRegisters();
        }
        var result = new Dictionary<string, ulong>();
        foreach (var reg in registerNamesMap) {
            var status = read_register(mpact_id, reg.Value, value_ptr);
            if (status < 0) continue;
            Int64 value = Marshal.ReadInt64(value_ptr);
            result.Add(reg.Key, Convert.ToUInt64(value));
        }
        var table = new Table().AddRow("Name", "Value");
        table.AddRows(result, x=> x.Key, x=> "0x{0:X}".FormatWith(x.Value));
        return table.ToArray();
    }

    private void LogAndThrowRE(string info) {
        this.Log(LogLevel.Error, info);
        throw new RecoverableException(info);
    }

    public Int32 LoadImageFile(String file_name, UInt64 address) {
        return load_image(mpact_id, file_name, address);
    }

    public void OnGPIO(int number, bool value) {
        if (mpact_id < 0) {
            this.Log(LogLevel.Noisy,
                     "OnGPIO: no simulator, discard gpio {0}:{1}", number,
                     value);
            return;
        }
        Int32 status = set_irq_value(mpact_id, number, value);
        if (status < 0) {
            Console.Error.WriteLine("Failure in setting irq value");
        }
    }

    public Int32 ReadSysMemory(UInt64 address, IntPtr buffer, Int32 length) {
        // Read from System Bus.
        switch (length) {
            case 1:
                var data8 = new byte[length];
                data8[0] = machine.SystemBus.ReadByte(address, this);
                Marshal.Copy(data8, 0, buffer, length);
                break;
            case 2:
                var data16 = new Int16[1];
                data16[0] = (Int16) machine.SystemBus.ReadWord(address, this);
                Marshal.Copy(data16, 0, buffer, 1);
                break;
            case 4:
                var data32 = new Int32[1];
                data32[0] = (Int32) machine.SystemBus.ReadDoubleWord(address,
                                                                     this);
                Marshal.Copy(data32, 0, buffer, 1);
                break;
            case 8:
                var data64 = new Int64[1];
                data64[0] = (Int64) machine.SystemBus.ReadQuadWord(address,
                                                                   this);
                Marshal.Copy(data64, 0, buffer, 1);
                break;
            default:
                var dataN = new byte[length];
                machine.SystemBus.ReadBytes(address, length, dataN, 0, false,
                                            this);
                Marshal.Copy(dataN, 0, buffer, length);
                break;
        }
        return length;
    }

    public Int32 WriteSysMemory(UInt64 address, IntPtr buffer, Int32 length) {
        switch (length) {
            case 1:
                var data8 = new byte[1];
                Marshal.Copy(buffer, data8, 0, 1);
                machine.SystemBus.WriteByte(address, data8[0], this);
                break;
            case 2:
                var data16 = new Int16[1];
                Marshal.Copy(buffer, data16, 0, 1);
                machine.SystemBus.WriteWord(address, (ushort)data16[0], this);
                break;
            case 4:
                var data32 = new Int32[1];
                Marshal.Copy(buffer, data32, 0, 1);
                machine.SystemBus.WriteDoubleWord(address, (uint)data32[0],
                                                  this);
                break;
            case 8:
                var data64 = new Int64[1];
                Marshal.Copy(buffer, data64, 0, 1);
                machine.SystemBus.WriteQuadWord(address, (ulong)data64[0],
                                                this);
                break;
            default:
                var dataN = new byte[length];
                Marshal.Copy(buffer, dataN, 0, length);
                machine.SystemBus.WriteBytes(dataN, address);
                break;
        }
        return length;
    }

    // IMpactPeripheral methods.
    public void WriteByte(long address, byte value) {
        var data8 = new byte[1];
        data8[0] = (byte) value;
        Marshal.Copy(data8, 0, value_ptr, 1);
        write_memory(mpact_id, (UInt64) address, value_ptr, 1);
    }

    public byte ReadByte(long address) {
        var data8 = new byte[1];
        read_memory(mpact_id, (UInt64) address, value_ptr, 1);
        Marshal.Copy(value_ptr, data8, 0, 1);
        return (byte) data8[0];
    }

    public void WriteWord(long address, ushort value) {
        var data16 = new Int16[1];
        data16[0] = (Int16) value;
        Marshal.Copy(data16, 0, value_ptr, 1);
        write_memory(mpact_id, (UInt64) address, value_ptr, 2);
    }

    public ushort ReadWord(long address) {
        var data16 = new Int16[1];
        read_memory(mpact_id, (UInt64) address, value_ptr, 2);
        Marshal.Copy(value_ptr, data16, 0, 1);
        return (ushort) data16[0];
    }

    public void WriteDoubleWord(long address, uint value) {
        var data32 = new Int32[1];
        data32[0] = (Int32) value;
        Marshal.Copy(data32, 0, value_ptr, 1);
        write_memory(mpact_id, (UInt64) address, value_ptr, 4);
    }

    public uint ReadDoubleWord(long address) {
        var data32 = new Int32[1];
        read_memory(mpact_id, (UInt64) address, value_ptr, 4);
        Marshal.Copy(value_ptr, data32, 0, 1);
        return (uint) data32[0];
    }

    public void WriteQuadWord(long address, ulong value) {
        var data64 = new Int64[1];
        data64[0] = (Int64) value;
        Marshal.Copy(data64, 0, value_ptr, 1);
        write_memory(mpact_id, (UInt64) address, value_ptr, 8);
    }

    public ulong ReadQuadWord(long address) {
        var data64 = new Int64[1];
        read_memory(mpact_id, (UInt64) address, value_ptr, 8);
        Marshal.Copy(value_ptr, data64, 0, 1);
        return (ulong) data64[0];
    }

    public byte[] ReadBytes(long offset, int count, IPeripheral context = null) {
        var bytes = new byte[count];
        var byte_array_ptr = Marshal.AllocHGlobal(count);
        read_memory(mpact_id, (UInt64) offset, byte_array_ptr, count);
        Marshal.Copy(value_ptr, bytes, 0, count);
        Marshal.FreeHGlobal(byte_array_ptr);
        return bytes;
    }

    public void WriteBytes(long offset, byte[] array, int startingIndex,
                           int count, IPeripheral context = null) {
        var byte_array_ptr = Marshal.AllocHGlobal(count);
        Marshal.Copy(array, startingIndex, byte_array_ptr, count);
        write_memory(mpact_id, (UInt64) offset, byte_array_ptr, count);
        Marshal.FreeHGlobal(byte_array_ptr);
    }

    // End of IMpactPeripheral methods.
    protected Dictionary<string, Int32>
                registerNamesMap = new Dictionary<string, Int32>();

    private ushort cli_port = 0;
    private bool wait_for_cli = true;
    private UInt64 size = 0;
    private List<GDBFeatureDescriptor>
                gdbFeatures = new List<GDBFeatureDescriptor>();
    private UInt64 memoryBase;
    private UInt64 memorySize;
    private Dictionary<Int32, CPURegister>
                registerMap = new Dictionary<Int32, CPURegister>();
    private string executable_file = "";
    private string symbol_file = "";
    private BinFileLoadInfo
                bin_file_info = new BinFileLoadInfo {file_name = "",
                                                     load_address = 0,
                                                       entry_point = 0};
    private string cpu_library_path = "";
    private bool cpu_library_bound = false;
    private NativeBinder binder;
    private readonly object nativeLock;
    private readonly Int32 maxStringLen = 32;

    // Pointers used in marshaling data to/from the simulator library.
    private IntPtr value_ptr {get; set;}
    private IntPtr error_ptr {get; set;}
    private IntPtr reg_info_ptr {get; set;}
    private IntPtr string_ptr {get; set;}

    // Declare some additional function signatures.
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    public delegate Int32 WriteRegister(Int32 param0, Int32 param1, UInt64 param2);

    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    public delegate Int32 ReadRegister(Int32 param0, Int32 param1, IntPtr param2);

    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    public delegate Int32 MemoryAccess(Int32 param0, UInt64 param1, IntPtr param2, Int32 param3);

    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    public delegate UInt64 Step(Int32 param0, UInt64 param1, IntPtr param2);

    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    public delegate Int32 LoadImage(Int32 param0, String param1, UInt64 param2);

    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    public delegate Int32 GetRegInfoSize(Int32 param0);

    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    public delegate Int32 GetRegInfo(Int32 param0, Int32 param1, IntPtr param2, IntPtr param3);

    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    public delegate Int32 FuncInt32UInt64IntPtrInt32_(UInt64 param0, IntPtr param1, Int32 param2);

    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    public delegate Int32 ConnectWithSysbus(string param0, Int32 param1, Int32 param2,
                                            FuncInt32UInt64IntPtrInt32_ param3,
                                            FuncInt32UInt64IntPtrInt32_ param4);

    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    public delegate Int32 ConstructWithSysbus(string param0, Int32 param1,
                                              FuncInt32UInt64IntPtrInt32_ param2,
                                              FuncInt32UInt64IntPtrInt32_ param3);

    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    public delegate Int32 SetConfig(Int32 param0, string[] param1,
                                    string[] param2, Int32 param3);

    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    public delegate Int32 SetIrqValue(Int32 param0, Int32 param1, bool param2);

    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    public delegate UInt64 LoadElf(Int32 param0, string param1, bool param2,
                                   IntPtr param3);

    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    public delegate void ActionInt32_(Int32 param0);

    // Int32 read_sysmem_delegate(UInt64 address, IntPtr buffer, Int32 size)
    private FuncInt32UInt64IntPtrInt32_ read_sysmem_delegate;
    // Int32 write_sysmem_delegate(UInt64 address, IntPtr buffer, Int32 size)
    private FuncInt32UInt64IntPtrInt32_ write_sysmem_delegate;

    // Functions that are imported from the mpact sim library.
#pragma warning disable 649
    [Import(UseExceptionWrapper = false)]
    // Int32 construct_with_sysbus(string cpu_type, Int32 id,
    //                             Int32 read_callback(UInt64, IntPtr, Int32),
    //                             Int32 write_callback(UInt64, IntPtr, Int32));
    private ConstructWithSysbus construct_with_sysbus;

    [Import(UseExceptionWrapper = false)]
    // Int32 connect_with_sybsus(string cpu_type, Int32 id,
    //                           Int32 read_callback(UInt64, IntPtr, Int32),
    //                           Int32 write_callback(UInt64, IntPtr, Int32));
    private ConnectWithSysbus connect_with_sysbus;

    [Import(UseExceptionWrapper = false)]
    // void destruct(Int32 id);
    private ActionInt32_ destruct;

    [Import(UseExceptionWrapper = false)]
    // void reset(Int32 id);
    private ActionInt32_ reset;

    [Import(UseExceptionWrapper = false)]
    // Int32 get_reg_info_size(Int32 id)
    private GetRegInfoSize get_reg_info_size;

    [Import(UseExceptionWrapper = false)]
    // Int32 get_reg_info(Int32 id, IntPtr *name, IntPtr *struct);
    private GetRegInfo get_reg_info;

    [Import(UseExceptionWrapper = false)]
    // UInt64 load_elf(Int32 id, String file_name,
    //                 bool for_symbols_only, Int32* error_ptr);
    private LoadElf load_elf;

    [Import(UseExceptionWrapper = false)]
    // Int32 load_image(Int32 id, String file_name, UInt64 address);
    private LoadImage load_image;

    [Import(UseExceptionWrapper = false)]
    // UInt64 step(Int32 id, UInt64 step, IntPtr *error);
    private Step step;

    [Import(UseExceptionWrapper = false)]
    // Int32 read_register(Int32 id, Int32 reg_id, IntPtr *value);
    private ReadRegister read_register;

    [Import(UseExceptionWrapper = false)]
    // Int32 write_register(Int32 id, Int32 reg_id, UInt64 value);
    private WriteRegister write_register;

    [Import(UseExceptionWrapper = false)]
    // Int32 read_memory(Int32 id, UInt64 address, IntPtr buffer, Int32 size);
    private MemoryAccess read_memory;

    [Import(UseExceptionWrapper = false)]
    // Int32 write_memory(Int32 id, UInt64 address, IntPtr buffer, Int32 size);
    private MemoryAccess write_memory;

    [Import(UseExceptionWrapper = false)]
    // Int32 set_config(Int32 id, string[] names string[] values, Int32 size);
    private SetConfig set_config;

    [Import(UseExceptionWrapper = false)]
    // Int32 set_irq_value(Int32 id, Int32 irq_no, bool value);
    private SetIrqValue set_irq_value;

#pragma warning restore 649

    private Int32 mpact_id = -1;
    private string cpu_type;
    private ulong instructionsExecutedThisRound {get; set;}
    private ulong totalExecutedInstructions {get; set;}
    private const int PC_ID = 0x07b1;
}  // class MpactCheriotCPU


// Peripheral interface class.
public class MpactPeripheral : IKnownSize, IBytePeripheral,
                               IWordPeripheral, IDoubleWordPeripheral,
                               IQuadWordPeripheral,
                               IMultibyteWritePeripheral, IDisposable {

    public MpactPeripheral(IMachine machine, long baseAddress,
                           long size, IMpactPeripheral mpactCpu) {
        if (size == 0) {
            throw new ConstructionException("Memory size cannot be 0");
        }
        this.machine = machine;
        this.size = size;
        this.base_address = baseAddress;
        this.mpact_cpu = mpactCpu;
    }

    public void Reset() {}

    public void Dispose() {}

    // All memory accesses are forwarded to the mpactCpu with the base_address
    // added to the address field.
    public void WriteByte(long address, byte value) {
        mpact_cpu.WriteByte(address + base_address, value);
    }

    public byte ReadByte(long address) {
        return mpact_cpu.ReadByte(address + base_address);
    }

    public void WriteWord(long address, ushort value) {
        mpact_cpu.WriteWord(address + base_address, value);
    }

    public ushort ReadWord(long address) {
        return mpact_cpu.ReadWord(address + base_address);
    }

    public void WriteDoubleWord(long address, uint value) {
        mpact_cpu.WriteDoubleWord(address + base_address, value);
    }

    public uint ReadDoubleWord(long address) {
        return mpact_cpu.ReadDoubleWord(address + base_address);
    }

    public void WriteQuadWord(long address, ulong value) {
        mpact_cpu.WriteQuadWord(address + base_address, value);
    }

    public ulong ReadQuadWord(long address) {
        return mpact_cpu.ReadQuadWord(address + base_address);
    }

    public byte[] ReadBytes(long offset, int count, IPeripheral context = null) {
        return mpact_cpu.ReadBytes(offset + base_address, count, context);
    }

    public void WriteBytes(long offset, byte[] array, int startingIndex,
                           int count, IPeripheral context = null) {
        mpact_cpu.WriteBytes(offset + base_address, array, startingIndex,
                             count, context);
    }

    public long Size { get { return size;} }
    private IMpactPeripheral mpact_cpu;
    private long size;
    private long base_address = 0;
    private IMachine machine;

}  // class MpactPeripheral

}  // namespace Antmicro.Renode.Peripherals.MpactCPU

