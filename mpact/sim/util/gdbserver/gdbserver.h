// Copyright 2026 Google LLC
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

// This file contains a GDB server that can be used to with MPACT-Sim based
// simulators. It requires that the debugger (gdb, lldb, etc.) already
// supports your architecture.

#ifndef MPACT_SIM_UTIL_GDBSERVER_GDBSERVER_H_
#define MPACT_SIM_UTIL_GDBSERVER_GDBSERVER_H_

#include <cstdint>
#include <istream>
#include <ostream>
#include <string_view>

#include "absl/container/flat_hash_map.h"
#include "absl/types/span.h"
#include "mpact/sim/generic/core_debug_interface.h"
#include "mpact/sim/generic/debug_info.h"
#include "mpact/sim/util/renode/socket_streambuf.h"

namespace mpact::sim::util::gdbserver {

using ::mpact::sim::generic::DebugInfo;
using ::mpact::sim::util::renode::SocketStreambuf;

class GdbServer {
 public:
  // The constructor takes a span of core debug interfaces (usually the sim
  // top objects), and a debug info object to provide information about the
  // registers. Programs should already be loaded onto the core debug
  // interfaces before the Gdb server is connected.

  explicit GdbServer(
      absl::Span<generic::CoreDebugInterface*> core_debug_interfaces,
      const DebugInfo& debug_info);
  ~GdbServer();

  // Open the GDB server on the given port, wait for a connection, and process
  // the GDB commands.
  bool Connect(int port);

 private:
  // Terminate the connection to the GDB client.
  void Terminate();
  // Sends the given response to the GDB client. Wrapping it in the proper
  // packet format and adding the checksum.
  void Respond(std::string_view response);
  // Sends the given error message to the GDB client in an error packet.
  void SendError(std::string_view error);

  // Takes a GDB command string, verifies the checksum, and if it is valid,
  // then submits the command to be parsed.
  void AcceptGdbCommand(std::string_view command);
  // Parses the given GDB command and calls the appropriate command handler.
  void ParseGdbCommand(std::string_view command);

  // GDB command handlers.

  // Halt the simulator.
  void GdbHalt();
  // Return the halt reason.
  void GdbHaltReason();
  // Continue the simulation.
  void GdbContinue(std::string_view command);
  // Detach from the simulator.
  void GdbDetach();
  // Select the thread to operate on.
  void GdbSelectThread(std::string_view command);
  // Return the thread info.
  void GdbThreadInfo();
  // Read memory from the simulator.
  void GdbReadMemory(std::string_view address, std::string_view length);
  // Write memory to the simulator.
  void GdbWriteMemory(std::string_view address, std::string_view length,
                      std::string_view data);
  // Read GPR registers from the simulator.
  void GdbReadGprRegisters();
  // Write GPR registers to the simulator.
  void GdbWriteGprRegisters(std::string_view data);
  // Read a register from the simulator.
  void GdbReadRegister(std::string_view register_number_str);
  // Write a register to the simulator.
  void GdbWriteRegister(std::string_view register_number_str,
                        std::string_view register_value_str);
  // Query gdbserver features.
  void GdbQuery(std::string_view command);
  // Set gdbserver features.
  void GdbSet(std::string_view command);
  // Step the simulator.
  void GdbStep(std::string_view command);
  // Add a breakpoint to the simulator.
  void GdbAddBreakpoint(char type, std::string_view address_str,
                        std::string_view kind_str);
  // Remove a breakpoint from the simulator.
  void GdbRemoveBreakpoint(char type, std::string_view address_str,
                           std::string_view kind_str);
  // Respond with the supported GDB features.
  void GdbSupported(std::string_view command);
  // Get the executable file name and program arguments.
  void GdbExecAndArgs(std::string_view command);

  SocketStreambuf* out_buf_ = nullptr;
  SocketStreambuf* in_buf_ = nullptr;
  std::ostream* os_ = nullptr;
  std::istream* is_ = nullptr;

  bool connection_active_ = false;
  bool error_message_supported_ = false;
  uint8_t buffer_[16 * 1024];
  bool good_ = false;
  int server_socket_ = -1;
  int cli_fd_ = -1;
  int current_thread_id_ = 0;
  absl::flat_hash_map<char, int> thread_select_;
  absl::Span<generic::CoreDebugInterface*> core_debug_interfaces_;
  const DebugInfo& debug_info_;
};

}  // namespace mpact::sim::util::gdbserver

#endif  // MPACT_SIM_UTIL_GDBSERVER_GDBSERVER_H_
