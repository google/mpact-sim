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
#include <cstdio>
#include <istream>
#include <ostream>
#include <streambuf>
#include <string>
#include <string_view>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/types/span.h"
#include "mpact/sim/generic/core_debug_interface.h"
#include "mpact/sim/generic/debug_info.h"
#include "re2/re2.h"

namespace mpact::sim::util::gdbserver {

using ::mpact::sim::generic::DebugInfo;

// This class is used to provide a streambuf interface to a socket file
// descriptor, so that it can be used to access a socket as an istream/ostream.
// The overflow and underflow methods are the bare minimum that's required to
// implement to make this happen. No buffering is performed. This streambuf
// does not expand \n to \r\n or vice versa.
class GdbSocketStreambuf : public std::streambuf {
 public:
  explicit GdbSocketStreambuf(int fd) : fd_(fd) {}
  ~GdbSocketStreambuf() override { close(fd_); }

  // On overflow write character to the socket fd.
  int overflow(int c) override {
    if (c == EOF) {
      close(fd_);
      return c;
    }
    write(fd_, &c, 1);
    return c;
  }

  // On underflow read character from the socket fd.
  std::streambuf::int_type underflow() override {
    int count = read(fd_, &ch_, 1);
    if (count == 0) return traits_type::eof();
    setg(&ch_, &ch_, &ch_ + 1);
    return traits_type::to_int_type(ch_);
  }

 private:
  char ch_;
  int fd_;
};

class GdbServer {
 public:
  // The constructor takes a span of core debug interfaces (usually the sim
  // top objects), and a debug info object to provide information about the
  // registers. Programs should already be loaded onto the core debug
  // interfaces before the Gdb server is connected.

  // For now, only one core is supported (core 0) - the others are ignored.
  // Multiple cores will be supported in the future, but that requires that
  // the cores can be set to operate in stop mode. Then, after that we can
  // add support for non-stop mode with multiple cores.

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

  int GetThreadId(char command_type, std::string_view command);
  // Takes a GDB command string, verifies the checksum, and if it is valid,
  // then submits the command to be parsed.
  void AcceptGdbCommand(std::string_view command);
  // Parses the given GDB command and calls the appropriate command handler.
  void ParseGdbCommand(std::string_view command);

  std::string HexEncodeString(std::string_view str);
  std::string HexEncodeNumberInTargetEndianness(uint64_t number);
  // GDB command handlers.

  // Halt the simulator.
  void GdbHalt();
  // Return the halt reason.
  std::string GetHaltReason(int thread_id);
  void GdbHaltReason();
  // Continue the simulation.
  void GdbContinue(int thread_id, std::string_view command);
  void ContinueThread(int thread_id);
  void GdbVContinue(std::string_view command);
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
  void GdbReadGprRegisters(int thread_id);
  // Write GPR registers to the simulator.
  void GdbWriteGprRegisters(int thread_id, std::string_view data);
  // Read a register from the simulator.
  void GdbReadRegister(int thread_id, std::string_view register_number_str);
  // Write a register to the simulator.
  void GdbWriteRegister(int thread_id, std::string_view register_number_str,
                        std::string_view register_value_str);
  // Query gdbserver features.
  void GdbQuery(std::string_view command);
  // Set gdbserver features.
  void GdbSet(std::string_view command);
  // Step the simulator.
  void GdbStep(int thread_id);
  void StepThread(int thread_id);
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
  // Get the host info.
  void GdbHostInfo();
  void GdbXferReadTarget(std::string_view offset_str,
                         std::string_view length_str);

  GdbSocketStreambuf* out_buf_ = nullptr;
  GdbSocketStreambuf* in_buf_ = nullptr;
  std::ostream* os_ = nullptr;
  std::istream* is_ = nullptr;

  bool log_packets_ = false;
  bool no_ack_mode_ = false;
  bool no_ack_mode_latch_ = false;
  bool error_message_supported_ = false;
  bool thread_suffix_ = false;
  uint8_t buffer_[16 * 1024];
  bool good_ = false;
  int server_socket_ = -1;
  int cli_fd_ = -1;
  int current_thread_id_ = 1;
  absl::flat_hash_map<char, int> thread_select_;
  absl::Span<generic::CoreDebugInterface*> core_debug_interfaces_;
  std::vector<int> halt_reasons_;
  const DebugInfo& debug_info_;
  // Regular expressions used to parse GDB commands.
  // These are static since they are immutable once initialized.
  static LazyRE2 gdb_command_re_;
  static LazyRE2 thread_re_;
  static LazyRE2 xfer_read_target_re_;
  static LazyRE2 swbreak_set_re_;
  static LazyRE2 swbreak_clear_re_;
};

}  // namespace mpact::sim::util::gdbserver

#endif  // MPACT_SIM_UTIL_GDBSERVER_GDBSERVER_H_
