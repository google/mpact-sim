// Copyright 2023 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef MPACT_SIM_GENERIC_DELAY_LINE_H_
#define MPACT_SIM_GENERIC_DELAY_LINE_H_

#include <cstdint>
#include <vector>

/* Commented out for now. Not currently using threads.
#include "absl/base/thread_annotations.h"
#include "absl/synchronization/mutex.h"
*/
#include "absl/numeric/bits.h"
#include "mpact/sim/generic/delay_line_interface.h"

namespace mpact {
namespace sim {
namespace generic {

// The DelayLine is the generic class for implementing delay lines for
// values and deferred function calls. It allows one or more "actions" to
// be scheduled to be performed a number of cycles (calls to Advance()) in
// the future. The exact action to be performed is determined by the template
// parameter type DelayRecord. Abstractly, the DelayLine class is a circular
// buffer of lists of DelayRecords. It is implemented as a vector
// of vectors of DelayRecord instances. A given index into the first dimension
// contains a vector of the set of DelayRecord instances that specifies the
// "actions" that are performed for a given cycle in the future (based on its
// distance from the current_ index (mod vector size).
//
// The DelayLine circular buffer is always a power of two to make the
// mod operator cheap.
//
// The DelayLine class can be instantiated by passing in the DelayRecord
// type as a template argument. The DelayRecord must have a void Apply()
// method that performs the actions intended once the delay line has advanced
// to that record. Additionally, the DelayRecord type must have a constructor
// that can be called by std::vector::emplace_back().
//
// For instance, if the DelayRecord consists of a pointer and a value (with the
// intent that after a delay, the value gets written to the object pointed to
// by that pointer) a reasonable DelayRecord would be.
//
// struct MyDelayRecord {
//   int *destination;
//   int value;
//   void Apply() {
//     *destination = value;
//   }
//   MyDelayRecord(int *dest, int val) : destination(dest), value(val) {}
// };
//
// This class is thread safe.
template <typename DelayRecord>
class DelayLine : public DelayLineInterface {
 public:
  constexpr static int kDefaultDelayLineDepth = 16;
  // Constructor that takes an initial minimum size
  explicit DelayLine(uint32_t min_size)
      : delay_line_(absl::bit_ceil(min_size)), current_(0), num_entries_(0) {
    mask_ = delay_line_.size() - 1;
  }

  // Default constructor and destructors
  DelayLine() : DelayLine(kDefaultDelayLineDepth) {}
  ~DelayLine() override = default;

  // Add an item to the delay line with the given latency. The Ts... argument
  // pack is the set of arguments to the constructor of the DelayRecord which
  // is the value record type for the DelayLine. Returns the total number of
  // valid elements.
  template <typename... Ts>
  int Add(int latency, Ts... args) /*ABSL_LOCKS_EXCLUDED(delay_line_lock_)*/ {
    // absl::MutexLock dl(&delay_line_lock_);
    // If the latency is longer than the delay line, resize the delay line.
    if (latency >= static_cast<int>(delay_line_.size())) {
      Resize(latency);
    }
    int pos = (latency + current_) & mask_;
    delay_line_[pos].emplace_back(args...);
    return ++num_entries_;
  }

  // Advances the delay line by a cycle and performs any actions required.
  // Depending on the record type R, the actual action may vary. The initial
  // actions supported generically are writing back a data buffer to a state
  // instance and calling a function. Additional actions can be created by
  // deriving new delay lines as needed. Returns the number of valid entries
  // left in the delay line.
  int Advance() override /*ABSL_LOCKS_EXCLUDED(delay_line_lock_)*/ {
    // absl::MutexLock dl(&delay_line_lock_);
    current_ = (current_ + 1) & mask_;
    for (auto& rec : delay_line_[current_]) {
      rec.Apply();
      num_entries_--;
    }
    delay_line_[current_].clear();
    return num_entries_;
  }

  // Returns true if the delay line is empty.
  bool IsEmpty() const override { return num_entries_ == 0; }

 private:
  // If the latency is >= the delay line length, the delay line has to be
  // resized so the latency is covered.
  void Resize(
      uint32_t min_size) /*ABSL_EXCLUSIVE_LOCKS_REQUIRED(delay_line_lock_)*/ {
    // Compute a new size that's >= min_size and is a power of 2
    // Given that the initial size is a power of two, multiplying the
    // current size by two until it's greater or equal to min_size is
    // sufficient.
    uint32_t prev_size = delay_line_.size();

    if (min_size < prev_size) {
      return;
    }

    uint32_t new_size = absl::bit_ceil(min_size);

    mask_ = new_size - 1;
    delay_line_.resize(new_size);

    // now need to move elements from 0 to current-1 to new locations.
    for (int index = 0; index < current_; ++index) {
      if (!delay_line_[index].empty()) {
        delay_line_[prev_size + index] = delay_line_[index];
        delay_line_[index].clear();
      }
    }
  }

  std::vector<std::vector<DelayRecord>> delay_line_;
  /*ABSL_GUARDED_BY(delay_line_lock_)*/
  int current_ /*ABSL_GUARDED_BY(delay_line_lock_)*/;
  int mask_ /*ABSL_GUARDED_BY(delay_line_lock_)*/;
  /*absl::Mutex delay_line_lock_;*/
  uint32_t num_entries_;
};

}  // namespace generic
}  // namespace sim
}  // namespace mpact

#endif  // MPACT_SIM_GENERIC_DELAY_LINE_H_
