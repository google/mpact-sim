#include "mpact/sim/util/other/simple_uart.h"

#include <cstdint>
#include <cstring>
#include <iostream>

#include "absl/log/log.h"
#include "mpact/sim/generic/arch_state.h"

namespace mpact::sim::util {

// Constructors.
SimpleUart::SimpleUart(ArchState* state, std::ostream& output)
    : output_(&output) {}

SimpleUart::SimpleUart(ArchState* state) : SimpleUart(state, std::cerr) {}

void SimpleUart::Load(uint64_t address, DataBuffer* db, Instruction* inst,
                      ReferenceCount* context) {
  // If the address is unaligned, or the size is not a multiple of 4, it should
  // really be handled before it gets this far. Set the db to zero and finish
  // the load.
  if ((address & 0b11) || (db->size<uint8_t>() & 0b11)) {
    std::memset(db->raw_ptr(), 0, db->size<uint8_t>());
  } else {
    uint32_t offset = address & 0xffff;
    for (int i = 0; i < db->size<uint32_t>(); ++i) {
      db->Set<uint32_t>(i, Read(offset + i * 4));
    }
  }
  // Execute the instruction to process and write back the load data.
  if (nullptr != inst) {
    if (db->latency() > 0) {
      inst->IncRef();
      if (context != nullptr) context->IncRef();
      inst->state()->function_delay_line()->Add(db->latency(),
                                                [inst, context]() {
                                                  inst->Execute(context);
                                                  if (context != nullptr)
                                                    context->DecRef();
                                                  inst->DecRef();
                                                });
    } else {
      inst->Execute(context);
    }
  }
}

// No support for vector loads.
void SimpleUart::Load(DataBuffer* address_db, DataBuffer* mask_db, int el_size,
                      DataBuffer* db, Instruction* inst,
                      ReferenceCount* context) {
  LOG(FATAL) << "SimpleUart does not support vector loads";
}

void SimpleUart::Store(uint64_t address, DataBuffer* db) {
  // If the address is unaligned, or the size is not a multiple of 4, it should
  // really be handled before it gets this far. Set the db to zero and finish
  // the load.
  if ((address & 0b11) || (db->size<uint8_t>() & 0b11)) return;

  uint32_t offset = address & 0xffff;
  for (int i = 0; i < db->size<uint32_t>(); ++i) {
    Write(offset + i * 4, db->Get<uint32_t>(i));
  }
}

uint32_t SimpleUart::Read(uint32_t offset) {
  uint32_t value;
  switch (offset) {
    case 0x0000:
      if (dlab_) {  // Divisor Latch Low Byte.
        value = divisor_low_byte_;
      } else {  // Receiver Buffer.
        value = 0;
      }
      break;
    case 0x0004:
      if (dlab_) {  // Divisor Latch High Byte.
        value = divisor_high_byte_;
      } else {  // Interrupt Enable Register
        value = interrupt_enable_;
      }
      break;
    case 0x0008:  // Interrupt Identification Register.
      value = 0;
      break;
    case 0x000c:  // Line Control Register.
      value = line_control_reg_;
      break;
    case 0x0010:  // Modem Control Register.
      value = 0;
      break;
    case 0x0014:  // Line Status Register.
      value = 0;
      break;
    case 0x0018:  // Modem Status Register.
      value = 0;
      break;
    case 0x001c:  // Scratch Register.
      value = scratch_;
      break;
    default:
      value = 0;
      break;
  }
  return value;
}

void SimpleUart::Write(uint32_t offset, uint32_t value) {
  switch (offset) {
    case 0x0000:
      if (dlab_) {  // Divisor Latch Low Byte.
        divisor_low_byte_ = value;
      } else {  // Transmitter Buffer.
        char char_value = static_cast<char>(value & 0xff);
        output_->put(char_value);
        if (char_value == '\n') {
          // TODO(torerik): change this code when this class has better tracing
          // control.
          // output_->write(line_buffer_.data(), line_buffer_.size());
          output_->flush();
        }
      }
      break;
    case 0x0004:
      if (dlab_) {  // Divisor Latch High Byte.
        divisor_high_byte_ = value;
      } else {  // Interrupt Enable Register
        interrupt_enable_ = value;
      }
      break;
    case 0x0008:  // Interrupt Identification Register.
      // Ignore for now.
      break;
    case 0x000c:  // Line Control Register.
      line_control_reg_ = value;
      dlab_ = (line_control_reg_ & 0x80) != 0;
      break;
    case 0x0010:  // Modem Control Register.
      // Ignore for now.
      break;
    case 0x0014:  // Line Status Register.
      // Ignore for now.
      break;
    case 0x0018:  // Modem Status Register.
      // Ignore for now.
      break;
    case 0x001c:  // Scratch Register.
      scratch_ = value;
      break;
    default:
      // Ignore.
      break;
  }
}

// No support for vector stores.
void SimpleUart::Store(DataBuffer* address, DataBuffer* mask_db, int el_size,
                       DataBuffer* db) {
  LOG(FATAL) << "SimpleUart does not support vector stores";
}

}  // namespace mpact::sim::util
