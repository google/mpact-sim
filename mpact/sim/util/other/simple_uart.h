#ifndef LEARNING_BRAIN_RESEARCH_MPACT_SIM_CHERI_RISCV_SIMPLE_UART_H_
#define LEARNING_BRAIN_RESEARCH_MPACT_SIM_CHERI_RISCV_SIMPLE_UART_H_

#include <cstdint>
#include <ostream>
#include <string>

#include "mpact/sim/generic/arch_state.h"
#include "mpact/sim/generic/data_buffer.h"
#include "mpact/sim/generic/instruction.h"
#include "mpact/sim/generic/ref_count.h"
#include "mpact/sim/util/memory/memory_interface.h"

// This file declares the class to implement a very trivial uart with only
// output capabilities for now. The following memory mapped registers are
// defined:
//
//  Offset:   Dlab:    Semantics:
//  0x0000      1      Divisor latch low byte.
//  0x0000      0      TX(write)/RX(read) buffer.
//  0x0004      1      Divisor latch high byte.
//  0x0004      0      Interrupt enable register.
//  0x0008      x      Interrupt identification register.
//  0x000c      x      Line control register.
//  0x0010      x      Modem control register.
//  0x0014      x      Line status register.
//  0x0018      x      Modem status register.
//  0x001c      x      Scratch register.

namespace mpact::sim::util {

using ::mpact::sim::generic::ArchState;
using ::mpact::sim::generic::DataBuffer;
using ::mpact::sim::generic::Instruction;
using ::mpact::sim::generic::ReferenceCount;
using ::mpact::sim::util::MemoryInterface;

class SimpleUart : public MemoryInterface {
 public:
  // Constructors.
  // Instantiate the uart and set the output to std::cerr.
  explicit SimpleUart(ArchState *state);
  // Instantiate the uart and set the output to the given ostream.
  SimpleUart(ArchState *state, std::ostream &output);

  // Memory interface methods used to write to the memory mapped registers.
  void Load(uint64_t address, DataBuffer *db, Instruction *inst,
            ReferenceCount *context) override;
  void Load(DataBuffer *address_db, DataBuffer *mask_db, int el_size,
            DataBuffer *db, Instruction *inst,
            ReferenceCount *context) override;
  void Store(uint64_t address, DataBuffer *db) override;
  void Store(DataBuffer *address, DataBuffer *mask_db, int el_size,
             DataBuffer *db) override;

 private:
  // Helper methods.
  uint32_t Read(uint32_t offset);
  void Write(uint32_t offset, uint32_t value);
  ArchState *state_;
  // The dlab_ bit of the line control register. The dlab bit changes the
  // register map for offsets 0x0 and 0x4 when set.
  bool dlab_ = false;
  // Selected uart registers.
  uint32_t line_control_reg_ = 0;
  uint32_t divisor_high_byte_ = 0;
  uint32_t divisor_low_byte_ = 0;
  uint32_t interrupt_enable_ = 0;
  uint32_t scratch_;
  std::ostream *output_;
};

}  // namespace mpact::sim::util

#endif  // LEARNING_BRAIN_RESEARCH_MPACT_SIM_CHERI_RISCV_SIMPLE_UART_H_
