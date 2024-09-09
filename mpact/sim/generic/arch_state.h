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

#ifndef MPACT_SIM_GENERIC_ARCH_STATE_H_
#define MPACT_SIM_GENERIC_ARCH_STATE_H_

#include <cstdint>
#include <string>
#include <type_traits>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/strings/string_view.h"
#include "mpact/sim/generic/component.h"
#include "mpact/sim/generic/data_buffer.h"
#include "mpact/sim/generic/delay_line_interface.h"
#include "mpact/sim/generic/function_delay_line.h"
#include "mpact/sim/generic/operand_interface.h"
#include "mpact/sim/generic/program_error.h"

namespace mpact {
namespace sim {
namespace generic {

class RegisterBase;
class FifoBase;

// The ArchState class is a "glue" class for the simulated architecture state.
// It is intended that it be used to derive a class for each specific
// architecture for which a simulator is created, adding any specific features
// that is needed for that class.
//
// All delay lines, registers and fifo's are registered with the ArchState
// instance used in a simulator. The ArchState instance can then be called
// to advance delay lines, and look up register and fifo instances.
class ArchState : public Component {
  // ArchState is supposed to be instantiated from a derived class specific
  // to the architecture that is to be simulated. Default constructor is
  // disabled.
 protected:
  ArchState() = delete;
  explicit ArchState(absl::string_view id) : ArchState(nullptr, id, nullptr) {}
  ArchState(absl::string_view id, SourceOperandInterface *pc_operand)
      : ArchState(nullptr, id, pc_operand) {}
  ArchState(Component *parent, absl::string_view id,
            SourceOperandInterface *pc_operand);

 public:
  ~ArchState() override;

 public:
  using RegisterMap = absl::flat_hash_map<std::string, RegisterBase *>;
  using FifoMap = absl::flat_hash_map<std::string, FifoBase *>;

  // Adds the given register to the register table.
  void AddRegister(RegisterBase *reg);
  // Adds the given register to the register table but using name as key. This
  // is useful when a register object may be accessible using more than one
  // name, or a name that differs from that stored in the register object.
  void AddRegister(absl::string_view name, RegisterBase *reg);
  // Remove the named register from the register table. No action occurs if
  // there is no such register. If multiple names map to the same register
  // object, only the single mapping from the given name is removed.
  void RemoveRegister(absl::string_view name);

  // Creates a register of the given type and adds it to the register table.
  template <typename RegisterType, typename... Ps>
  RegisterType *AddRegister(absl::string_view name, Ps... pargs) {
    auto reg = new RegisterType(this, name, pargs...);
    AddRegister(reg);
    return reg;
  }

  // Adds the given fifo to the fifo table.
  void AddFifo(FifoBase *fifo);
  // Adds the given fifo to the fifo table but using name as key. This is useful
  // when a fifo object may be accessed using more than one name, or a name that
  // differs from that stored in the fifo object.
  void AddFifo(absl::string_view name, FifoBase *fifo);
  // Remove the named fifo from the fifo table. No action occurs if there is no
  // such fifo. If multiple names map to the same fifo object, only the single
  // mapping from the given name is removed.
  void RemoveFifo(absl::string_view name);

  // Creates a fifo of the given type and adds it to the fifo table.
  template <typename FifoType, typename... Ps>
  FifoType *AddFifo(absl::string_view name, Ps... pargs) {
    auto fifo = new FifoType(this, name, pargs...);
    AddFifo(fifo);
    return fifo;
  }

  // Advance all registered delay lines by one cycle.
  inline void AdvanceDelayLines() {
    cycle_++;
    for (auto dl : delay_lines_) {
      dl->Advance();
    }
  }

  // Create and add a delay line of the given type. This provides a mechanism
  // to add additional delay lines for types other than data buffer instances
  // and void() function objects that will be advanced when the ArchState
  // delay lines are advanced. It is of course possible to create and manage
  // delay lines outside of ArchState instances. Delay lines managed by the
  // ArchState instance will be deleted when the ArchState is deleted.
  template <typename DelayLineType, typename... Ps>
  DelayLineType *CreateAndAddDelayLine(Ps... pargs) {
    static_assert(
        std::is_convertible<DelayLineType *, DelayLineInterface *>::value);
    DelayLineType *delay_line = new DelayLineType(pargs...);
    delay_lines_.push_back(static_cast<DelayLineInterface *>(delay_line));
    return delay_line;
  }
  // This function is called after any event that may have caused an interrupt
  // to be registered as pending or enabled.  It is used to inform the core that
  // it should check to see if there are available interrupts and act
  // accordingly. The method is empty by default.
  virtual void CheckForInterrupt() { /*empty*/ }

  // Accessors for data members
  const std::string &id() const { return component_name(); }
  // The DataBufferFactory associated with this architecture instance.
  DataBufferFactory *db_factory() const { return db_factory_; }
  // The table of registers.
  RegisterMap *registers() { return &registers_; }
  // The table of fifos.
  FifoMap *fifos() { return &fifos_; }
  // The DataBuffer instance delay line.
  DataBufferDelayLine *data_buffer_delay_line() const {
    return data_buffer_delay_line_;
  }
  // The void() function delay line
  FunctionDelayLine *function_delay_line() const {
    return function_delay_line_;
  }
  // Returns the PC operand interface (read only)
  SourceOperandInterface *pc_operand() const { return pc_operand_; }
  // Used to report program error (or even internal simulator errors).
  ProgramErrorController *program_error_controller() const {
    return program_error_controller_;
  }

  uint64_t cycle() const { return cycle_; }

 protected:
  void set_pc_operand(SourceOperandInterface *pc_operand) {
    pc_operand_ = pc_operand;
  }
  void set_cycle(uint64_t value) { cycle_ = value; }

 private:
  uint64_t cycle_ = 0;
  SourceOperandInterface *pc_operand_;
  DataBufferFactory *db_factory_;
  RegisterMap registers_;
  FifoMap fifos_;
  DataBufferDelayLine *data_buffer_delay_line_;
  FunctionDelayLine *function_delay_line_;
  std::vector<DelayLineInterface *> delay_lines_;
  ProgramErrorController *program_error_controller_;
};

}  // namespace generic
}  // namespace sim
}  // namespace mpact

#endif  // MPACT_SIM_GENERIC_ARCH_STATE_H_
