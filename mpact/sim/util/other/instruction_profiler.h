/*
 * Copyright 2024 Google LLC
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef MPACT_SIM_UTIL_OTHER_PROFILER_H_
#define MPACT_SIM_UTIL_OTHER_PROFILER_H_

#include <cstdint>
#include <ostream>

#include "absl/container/btree_map.h"
#include "mpact/sim/generic/counters_base.h"
#include "mpact/sim/util/memory/memory_watcher.h"
#include "mpact/sim/util/program_loader/elf_program_loader.h"

// This file contains a class definition for a profiler. It connects to a
// counter, and whenever that counter's value is changed, the profiler takes
// the new value using the CounterValueSetInterface SetValue() and adds that
// sample to the profile. Instruction profiling is implemented by connecting
// a profiler instance to a counter that has pc values written to it.

namespace mpact::sim::util {

using ::mpact::sim::generic::CounterValueSetInterface;
using ::mpact::sim::util::ElfProgramLoader;

class InstructionProfiler : public CounterValueSetInterface<uint64_t> {
 public:
  // Currently the only constructor works from text ranges in an elf file.
  // TODO(torerik): Add constructors for other sets of ranges.
  // The granularity is a power of two and determines the value difference
  // between two adjacent sample buckets. For instruction profiling this is
  // the smallest instruction size in bytes.
  InstructionProfiler(ElfProgramLoader& elf_loader, unsigned granularity);
  explicit InstructionProfiler(unsigned granularity);
  ~InstructionProfiler() override;

  // Inherited from CounterValueSetInterface. This will connect to a counter
  // that is assigned the value to profile. The most recently used range is
  // cached for performance. If it doesn't match, call AddSampleInternal().
  void SetValue(const uint64_t& value) override {
    // See if the previously referenced range applies.
    uint64_t sample = value >> shift_;
    if ((sample >= last_start_) && (sample <= last_end_)) {
      last_profile_range_[sample - last_start_]++;
      return;
    }
    AddSampleInternal(sample);
  }

  // Write the profile to the given stream in csv format.
  void WriteProfile(std::ostream& os);

  // If the elf loader wasn't set in the constructor, use this method to set
  // it once the elf file is available.
  void SetElfLoader(ElfProgramLoader* elf_loader);

 private:
  void AddSampleInternal(uint64_t sample);
  int shift_ = 0;
  ElfProgramLoader* elf_loader_ = nullptr;
  absl::btree_map<MemoryWatcher::AddressRange, uint64_t*,
                  MemoryWatcher::AddressRangeLess>
      profile_ranges_;
  uint64_t last_start_ = 0xffff'ffff'ffff'ffffULL;
  uint64_t last_end_ = 0xffff'ffff'ffff'ffffULL;
  uint64_t* last_profile_range_ = nullptr;
};

}  // namespace mpact::sim::util

#endif  // MPACT_SIM_UTIL_OTHER_PROFILER_H_
