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

// This file specializes the MpactCPU class to add custom interfaces and
// properties required for the mpact_cheriot simulator.

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
using Antmicro.Renode.Peripherals.MpactCPU;
using ELFSharp.ELF;
using ELFSharp.UImage;

namespace Antmicro.Renode.Peripherals.MpactCPU
{

public enum CheriotCpu {
    Base = 0,
    Rvv = 1,
    RvvFp = 2,
}

// The MpactCheriotCPU class. This class derives from BaseCPU, which implements
// a CPU in ReNode. It is the interface between ReNode and the mpact_cheriot
// simulator library.
public class MpactCheriotCPU : MpactBaseCPU, ICluster<MpactCheriotCPU>,
                               ICpuSupportingGdb {

    public MpactCheriotCPU(uint id, UInt64 memoryBase, UInt64 memorySize,
                           UInt64 revocationMemoryBase, string cpuType,
                           IMachine machine, Endianess endianness,
                           CpuBitness bitness = CpuBitness.Bits32)
        : base(id, memoryBase, memorySize, cpuType, machine, endianness,
               bitness) {
        revocationMemBase = revocationMemoryBase;
	Clustered = new MpactCheriotCPU[] { this };
    }

    ~MpactCheriotCPU() {
    }


    public override string Architecture { get { return "MpactCheriot";} }

    public override void SetMpactConfig(List<string> config_names,
                                        List<string> config_values) {
        base.SetMpactConfig(config_names, config_values);
        if (inst_profile) {
            config_names.Add("instProfile");
            config_values.Add("0x1");
        }
        if (mem_profile) {
            config_names.Add("memProfile");
            config_values.Add("0x1");
        }
        if (clint_mmr_base != 0) {
            config_names.Add("clintMMRBase");
            config_values.Add(clint_mmr_base.ToString("X"));
        }
        if (clint_period != 0) {
            config_names.Add("clintPeriod");
            config_values.Add(clint_period.ToString("X"));
        }
        if (icache_config != "") {
            config_names.Add("iCache");
            config_values.Add(icache_config);
        }
        if (dcache_config != "") {
            config_names.Add("dCache");
            config_values.Add(dcache_config);
        }
        config_names.Add("coreVersion");
        config_values.Add(core_version.ToString("X"));
    }

    public bool InstProfile {
        get => inst_profile;
        set => inst_profile = value;
    }

    public bool MemProfile {
        get => mem_profile;
        set => mem_profile = value;
    }

    public UInt64 ClintMMRBase {
        get => clint_mmr_base;
        set => clint_mmr_base = value;
    }

    public UInt64 ClintPeriod {
        get => clint_period;
        set => clint_period = value;
    }

    public string ICache {
        get => icache_config;
        set => icache_config = value;
    }

    public string DCache {
        get => dcache_config;
        set => dcache_config = value;
    }

    public uint CoreVersion {
        get => core_version;
        set => core_version = value;
    }

    // ICPUWithHooks methods.

    public void AddHookAtInterruptBegin(Action<ulong> hook) {
        throw new RecoverableException(
                        "Interrupt hooks not currently supported");
    }
    public void AddHookAtInterruptEnd(Action<ulong> hook) {
        throw new RecoverableException(
                        "Interrupt hooks not currently supported");
    }
    public void AddHookAtWfiStateChange(Action<bool> hook) {
        throw new RecoverableException(
                        "Wfi state change hook not currently supported");
    }

    public void AddHook(ulong addr, Action<ICpuSupportingGdb, ulong> hook) {
        throw new RecoverableException("Hooks not currently supported");
    }
    public void RemoveHook(ulong addr, Action<ICpuSupportingGdb, ulong> hook) {
        /* empty */
    }
    public void RemoveHooksAt(ulong addr) { /* empty */ }
    public void RemoveAllHooks() { /* empty */ }

    // ICluster methods.
    public new IEnumerable<ICluster<MpactCheriotCPU>> Clusters {get; } = new List<ICluster<MpactCheriotCPU>>(0);
    public new IEnumerable<MpactCheriotCPU> Clustered {get; }

    // ICPUSupportingGdb methods.

    public void EnterSingleStepModeSafely(HaltArguments args) {
        // this method should only be called from CPU thread,
        // but we should check it anyway
        CheckCpuThreadId();

        ExecutionMode = ExecutionMode.SingleStep;

        UpdateHaltedState();
        InvokeHalted(args);
    }

    public string GDBArchitecture { get { return "riscv:cheriot"; } }

    public List<GDBFeatureDescriptor> GDBFeatures {
        get {
            if (gdbFeatures.Any()) return gdbFeatures;

            // Populate the register map if it hasn't been done yet.
            if (registerNamesMap.Count == 0) {
                GetMpactRegisters();
            }
            var cpuGroup = new GDBFeatureDescriptor("org.gnu.gdb.riscv.cpu");
            var intType = $"uint32";
            var c0_index = registerNamesMap["c0"];
            for (var i = 0u; i < 16; ++i) {
                cpuGroup.Registers.Add(
                        new GDBRegisterDescriptor((uint)c0_index + i, 32u,
                                                  $"c{i}", intType, "general"));
            }
            var pcc_index = registerNamesMap["pcc"];
            cpuGroup.Registers.Add(
                        new GDBRegisterDescriptor((uint)pcc_index, 32u, "pcc",
                                                  "code_ptr", "general"));
            gdbFeatures.Add(cpuGroup);

            return gdbFeatures;
        }
    }

    // End of ICpuSupportingGdb methods.

    private bool inst_profile = false;
    private bool mem_profile = false;
    private UInt64 revocationMemBase;
    private UInt64 clint_mmr_base = 0x0;
    private UInt64 clint_period = 0;
    private uint core_version = 100;  // Default is 100 (v1.0).
    private string icache_config = "";
    private string dcache_config = "";
    private List<GDBFeatureDescriptor>
                gdbFeatures = new List<GDBFeatureDescriptor>();
}  // class MpactCheriotCPU

}  // namespace Antmicro.Renode.Peripherals.MpactCPU

