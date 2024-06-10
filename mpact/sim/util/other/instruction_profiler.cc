// Copyright 2024 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "mpact/sim/util/other/instruction_profiler.h"

#include <cstdint>
#include <cstring>
#include <ostream>
#include <utility>

#include "absl/log/log.h"
#include "absl/numeric/bits.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "elfio/elf_types.hpp"
#include "mpact/sim/util/memory/memory_watcher.h"
#include "mpact/sim/util/program_loader/elf_program_loader.h"

namespace mpact::sim::util {

InstructionProfiler::InstructionProfiler(ElfProgramLoader &elf_loader,
                                         unsigned granularity)
    : elf_loader_(&elf_loader) {
  if (!absl::has_single_bit(granularity)) {
    LOG(ERROR) << absl::StrCat("Invalid granularity: ", granularity,
                               ",  must be a power of 2");
  }
  shift_ = absl::bit_width(granularity) - 1;
  SetElfLoader(&elf_loader);
}

InstructionProfiler::InstructionProfiler(unsigned granularity)
    : elf_loader_(nullptr) {
  if (!absl::has_single_bit(granularity)) {
    LOG(ERROR) << absl::StrCat("Invalid granularity: ", granularity,
                               ",  must be a power of 2");
  }
  shift_ = absl::bit_width(granularity) - 1;
}

InstructionProfiler::~InstructionProfiler() {
  for (auto const &[unused, counters] : profile_ranges_) {
    delete[] counters;
  }
  profile_ranges_.clear();
}

void InstructionProfiler::AddSampleInternal(uint64_t sample) {
  if (elf_loader_ == nullptr) return;
  // Look up a new range.
  auto it = profile_ranges_.find({sample, sample});
  if (it == profile_ranges_.end()) {
    LOG(WARNING) << absl::StrCat("Profile sample out of range: ",
                                 absl::Hex(sample << shift_));
    return;
  }
  // Save the range info and increment the counter.
  last_profile_range_ = it->second;
  last_start_ = it->first.start;
  last_end_ = it->first.end;
  last_profile_range_[sample - last_start_]++;
}

void InstructionProfiler::WriteProfile(std::ostream &os) {
  os << "Address,Count" << "\n";
  for (auto const &[range, counters] : profile_ranges_) {
    uint64_t size = range.end - range.start;
    for (auto i = 0; i < size; ++i) {
      if (counters[i] == 0) continue;
      os << absl::StrFormat("0x%llx,%llu\n", (range.start + i) << shift_,
                            counters[i]);
    }
  }
}

void InstructionProfiler::SetElfLoader(ElfProgramLoader *elf_loader) {
  elf_loader_ = elf_loader;
  uint64_t begin = 0;
  uint64_t end = 0;
  // Iterate through the elf segments (assumes they are in order), and
  // coalesces ranges that are spaced by less than 0x1'000 units of granularity.
  // This reduces the number of ranges in the map and improves performance
  // during simulation.
  for (auto const &segment : elf_loader_->elf_reader()->segments) {
    // Only consider segments that are loaded, executable, and with size > 0.
    if (segment->get_type() != PT_LOAD) continue;
    if ((segment->get_flags() & PF_X) == 0) continue;
    uint64_t size = segment->get_memory_size() >> shift_;
    if (size == 0) continue;

    uint64_t vaddr_begin = segment->get_virtual_address() >> shift_;
    // If it's the first time we see a segment, just get the start and end
    // values.
    if (begin == 0 && end == 0) {
      begin = vaddr_begin;
      end = vaddr_begin + size;
      continue;
    };
    // If the segment is close enough to the current, just coalesce.
    if (vaddr_begin - end < 0x1000) {
      end = vaddr_begin + size;
      continue;
    }
    // Otherwise, create a entry from the previously accumulated ranges, and
    // start a new range.
    size = end - begin - 1;
    uint64_t *counters = new uint64_t[size];
    ::memset(counters, 0, size * sizeof(uint64_t));
    profile_ranges_.insert(
        std::make_pair(MemoryWatcher::AddressRange{begin, end}, counters));
    begin = vaddr_begin;
    end = vaddr_begin + size;
  }
  // Make the last entry.
  if (begin != 0 || end != 0) {
    uint64_t size = end - begin - 1;
    uint64_t *counters = new uint64_t[size];
    ::memset(counters, 0, size * sizeof(uint64_t));
    profile_ranges_.insert(
        std::make_pair(MemoryWatcher::AddressRange{begin, end - 1}, counters));
  }
}

}  // namespace mpact::sim::util
