#ifndef MPACT_SIM_GENERIC_DEBUG_COMMAND_SHELL_INTERFACE_H_
#define MPACT_SIM_GENERIC_DEBUG_COMMAND_SHELL_INTERFACE_H_

#include <cstddef>
#include <cstdint>
#include <functional>
#include <iostream>
#include <string>
#include <vector>

#include "absl/container/btree_map.h"
#include "absl/functional/any_invocable.h"
#include "absl/strings/string_view.h"
#include "mpact/sim/generic/arch_state.h"
#include "mpact/sim/generic/core_debug_interface.h"
#include "mpact/sim/util/program_loader/elf_program_loader.h"

namespace mpact::sim::generic {

class DebugCommandShellInterface {
 public:
  static constexpr int kMemBufferSize = 32;

  struct WatchpointInfo {
    uint64_t address;
    size_t length;
    AccessType access_type;
    bool active;
  };

  // Each core must provide the debug interface and the elf loader.
  struct CoreAccess {
    CoreDebugInterface *debug_interface = nullptr;
    std::function<util::ElfProgramLoader *()> loader_getter;
    ArchState *state = nullptr;
    absl::btree_map<int, uint64_t> breakpoint_map;
    int breakpoint_index = 0;
    absl::btree_map<int, WatchpointInfo> watchpoint_map;
    int watchpoint_index = 0;
  };

  virtual ~DebugCommandShellInterface() = default;

  // Type of custom command processing invocables. It takes a string_view of
  // the current text input, the current core access structure, and a string
  // to be written to the command shell output. The invocable should return
  // true if the command input string was successfully matched, regardless of
  // any error while executing the command, in which case the output string
  // should be set to an appropriate error message.
  using CommandFunction = absl::AnyInvocable<bool(
      absl::string_view, const CoreAccess &, std::string &)>;

  // Add core access to the system.
  virtual void AddCore(const CoreAccess &core_access) = 0;
  virtual void AddCores(const std::vector<CoreAccess> &core_access) = 0;

  // This adds a custom command to the command interpreter. Usage will be added
  // to the standard command usage. The callable will be called before the
  // standard commands are processed. Must be called before Run() is called.
  virtual void AddCommand(absl::string_view usage,
                          CommandFunction command_function) = 0;

  // The run method is the command interpreter. It parses the command strings,
  // executes the corresponding commands, displays results and error messages.
  virtual void Run(std::istream &is, std::ostream &os) = 0;
};

}  // namespace mpact::sim::generic

#endif  // MPACT_SIM_GENERIC_DEBUG_COMMAND_SHELL_INTERFACE_H_
