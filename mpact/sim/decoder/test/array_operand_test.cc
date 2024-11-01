#include <sys/types.h>

#include <cstdint>
#include <memory>

#include "absl/log/check.h"
#include "absl/strings/str_cat.h"
#include "googlemock/include/gmock/gmock.h"  // IWYU pragma: keep
#include "googletest/include/gtest/gtest.h"
#include "mpact/sim/decoder/test/push_pop_decoder.h"
#include "mpact/sim/decoder/test/push_pop_inst_enums.h"
#include "mpact/sim/generic/arch_state.h"
#include "mpact/sim/generic/data_buffer.h"
#include "mpact/sim/generic/register.h"
#include "mpact/sim/util/memory/flat_demand_memory.h"

// This file tests that the "array" or "list" valued operands used in the .isa
// description is handled correctly by the decoder. This test case uses the
// push/pop isa and decoder.

namespace {

using ::mpact::sim::decoder::test::OpcodeEnum;
using ::mpact::sim::decoder::test::PushPopDecoder;
using ::mpact::sim::generic::DataBuffer;
using ::mpact::sim::util::FlatDemandMemory;

using TestRegister = ::mpact::sim::generic::Register<uint32_t>;

constexpr uint64_t kBase = 0x1000;

// A test state, since ArchState cannot be instantiated directly.
class TestState : public ::mpact::sim::generic::ArchState {
 public:
  TestState() : ::mpact::sim::generic::ArchState("TestState") {}
};

// Helper functions to generate encoding push and pop instructions with
// different rlist and spimm values.
static uint16_t GeneratePushInstruction(uint8_t rlist, uint8_t spimm) {
  uint16_t inst = 0b101'11000'0000'00'10;
  return inst | ((rlist & 0xf) << 4) | ((spimm & 0x3) << 2);
}

static uint16_t GeneratePopInstruction(uint8_t rlist, uint8_t spimm) {
  uint16_t inst = 0b101'11010'0000'00'10;
  return inst | ((rlist & 0xf) << 4) | ((spimm & 0x3) << 2);
}

// Test fixture. This adds registers to the state and writes instructions to
// memory.
class ArrayOperandTest : public ::testing::Test {
 protected:
  ArrayOperandTest() {
    state_ = std::make_unique<TestState>();
    memory_ = std::make_unique<FlatDemandMemory>();
    decoder_ = std::make_unique<PushPopDecoder>(state_.get(), memory_.get());
    // Add registers to the state.
    for (int i = 1; i < 32; i++) {
      state_->AddRegister(new TestRegister(state_.get(), absl::StrCat("x", i)));
    }
    // Write instructions to memory - 16 each of pushes and pops. The first
    // 4 of each are illegal instructions since rlist < 4.
    DataBuffer *db = state_->db_factory()->Allocate<uint16_t>(1);
    for (int i = 0; i < 16; i++) {
      db->Set<uint16_t>(0, GeneratePushInstruction(i, i & 0x3));
      memory_->Store(kBase + i * db->size<uint8_t>(), db);
    }
    for (int i = 0; i < 16; i++) {
      db->Set<uint16_t>(0, GeneratePopInstruction(i, i & 0x3));
      memory_->Store(kBase + 16 * db->size<uint8_t>() + i * db->size<uint8_t>(),
                     db);
    }
    db->DecRef();
  }

  std::unique_ptr<TestState> state_;
  std::unique_ptr<FlatDemandMemory> memory_;
  std::unique_ptr<PushPopDecoder> decoder_;
};

// Verify that the push instructions are decoded correctly.
TEST_F(ArrayOperandTest, PushInstructionDecoding) {
  for (int i = 0; i < 16; i++) {
    auto *inst = decoder_->DecodeInstruction(kBase + i * 2);
    CHECK_NE(inst, nullptr);
    if (i < 4) {
      EXPECT_EQ(inst->opcode(), *OpcodeEnum::kNone);
    } else {
      EXPECT_EQ(inst->opcode(), *OpcodeEnum::kPush);
    }
    inst->DecRef();
  }
}

// Verify that the pop instructions are decoded correctly.
TEST_F(ArrayOperandTest, PopInstructionDecoding) {
  for (int i = 0; i < 16; i++) {
    auto *inst = decoder_->DecodeInstruction(kBase + 16 * 2 + i * 2);
    CHECK_NE(inst, nullptr);
    if (i < 4) {
      EXPECT_EQ(inst->opcode(), *OpcodeEnum::kNone);
    } else {
      EXPECT_EQ(inst->opcode(), *OpcodeEnum::kPop);
    }
    inst->DecRef();
  }
}

// Verify that the push instructions have the correct number of source and
// destination operands.
TEST_F(ArrayOperandTest, PushOperands) {
  for (int i = 4; i < 16; i++) {
    auto *inst = decoder_->DecodeInstruction(kBase + i * 2);
    CHECK_NE(inst, nullptr);
    EXPECT_EQ(inst->opcode(), *OpcodeEnum::kPush)
        << inst->AsString() << " for instruction " << i;
    // Push instructions have 3 source operands (x2, spimm6, rlist) in addition
    // to the list of registers to be pushed. The number of registers pushed
    // is determined by the rlist field, 4 -> 1 register, 5 -> 2 registers,
    // etc., up to 14 -> 11 registers. Then for 15 -> 13 registers.
    EXPECT_EQ(inst->SourcesSize(), 3 + (i - 3) + (i == 15 ? 1 : 0));
    // There should only be a single destination operand, x2.
    EXPECT_EQ(inst->DestinationsSize(), 1);
    inst->DecRef();
  }
}

// Verify that the pop instructions have the correct number of source and
// destination operands.
TEST_F(ArrayOperandTest, PopOperands) {
  for (int i = 4; i < 16; i++) {
    auto *inst = decoder_->DecodeInstruction(kBase + 16 * 2 + i * 2);
    CHECK_NE(inst, nullptr);
    EXPECT_EQ(inst->opcode(), *OpcodeEnum::kPop)
        << inst->AsString() << " for instruction " << i;
    // Pop instructions have 3 source operands (x2, spimm6, rlist).
    EXPECT_EQ(inst->SourcesSize(), 3);
    // The number of destination operands is x2 and the list of registers to be
    // popped from the stack. The number of registers  is determined by the
    // rlist field, 4 -> 1 register, 5 -> 2 registers, etc., up to 14 -> 11
    // registers. Then for 15 -> 13 registers.
    EXPECT_EQ(inst->DestinationsSize(), 1 + (i - 3) + (i == 15 ? 1 : 0));
    inst->DecRef();
  }
}

}  // namespace
