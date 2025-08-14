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

#include <cstdint>
#include <ios>

#include "absl/numeric/bits.h"
#include "googlemock/include/gmock/gmock.h"  // IWYU pragma: keep
#include "googletest/include/gtest/gtest.h"

namespace {

// This is a test for a function ExtractBits used to extract bits from
// long bitvectors stored as uint8_t *. This is a function that gets written
// out to a source file as part of a code generator, so it is not included.

template <typename T>
static inline T ExtractBits(const uint8_t* data, int data_size, int msb,
                            int width) {
  if (width == 0) return 0;

  int byte_low = data_size - ((msb - width) >> 3) - 1;
  int byte_high = data_size - (msb >> 3) - 1;
  int high_bit = msb & 0x7;

  // If it is only from one byte, extract and return.
  if (byte_low == byte_high) {
    uint8_t mask = (1 << (high_bit + 1)) - 1;
    return (mask & data[byte_high]) >> (high_bit - width + 1);
  }

  // Extract from the high order byte.
  T val = 0;
  uint8_t mask = 0xff >> (7 - high_bit);
  val = (mask & data[byte_high++]);
  int remainder = width - (1 + high_bit);

  while (remainder >= 8) {
    val = (val << 8) | data[byte_high++];
    remainder -= 8;
  }

  // Extract any remaining bits from the high end of the last byte.
  if (remainder > 0) {
    val <<= remainder;
    int shift = 8 - remainder;
    uint8_t mask = 0xff << shift;
    val |= (data[byte_high] & mask) >> shift;
  }
  return val;
}

uint8_t kBitString0[] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
                         0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};

uint8_t kBitString1[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                         0x7f, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};

TEST(ExtractBitsTest, FieldWidths) {
  // Vary across offsets, 0..16, and widths, 0..64.
  for (int offset = 100; offset <= 100 + 16; offset++) {
    for (int width = 1; width <= 64; width++) {
      uint64_t value = ExtractBits<uint64_t>(kBitString0, sizeof(kBitString0),
                                             offset, width);
      uint64_t match = (1ULL << (width & 0x3f)) - 1;
      if (width == 64) {
        match--;
      }
      EXPECT_EQ(absl::popcount(value), width)
          << offset << " " << width << " 0x" << std::hex << value;
      EXPECT_EQ(value, match)
          << offset << " " << width << " 0x" << std::hex << value;
    }
  }
}

// Extract just the first one in kBitString1, the offset of the bitfield for
// each width. The bit is the msb bit of 0x7f, which is at bit position
// (counting right to left) at 8 * 8 + 6.
TEST(ExtractBitsTest, One) {
  for (int width = 1; width <= 64; width++) {
    uint64_t value = ExtractBits<uint64_t>(kBitString1, sizeof(kBitString1),
                                           8 * 8 + 6 + width - 1, width);
    EXPECT_EQ(value, 1) << "width: " << width;
  }
}

}  // namespace
