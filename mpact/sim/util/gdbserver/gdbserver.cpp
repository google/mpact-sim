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

#include "third_party/mpact_sim/util/gdbserver/gdbserver.h"

#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <istream>
#include <ostream>
#include <string>
#include <vector>

#include "third_party/absl/container/flat_hash_map.h"
#include "third_party/absl/log/log.h"
#include "third_party/absl/strings/numbers.h"
#include "third_party/absl/strings/str_cat.h"
#include "third_party/absl/strings/str_format.h"
#include "third_party/absl/strings/str_split.h"
#include "third_party/absl/strings/string_view.h"
#include "third_party/absl/types/span.h"
#include "third_party/mpact_sim/generic/core_debug_interface.h"
#include "third_party/mpact_sim/generic/type_helpers.h"

namespace {

// Some binary data may be escaped in the GDB command. This removes such
// escapes after the command has been "unwrapped".
std::string UnescapeCommand(std::string_view escaped_command) {
  std::string command;
  for (int i = 0; i < escaped_command.size(); ++i) {
    char c = escaped_command[i];
    if (c == 0x7d) {
      c = escaped_command[++i] ^ 0x20;
      command.push_back(c);
    }
    command.push_back(c);
  }
  return command;
}

}  // namespace

namespace mpact::sim::util::gdbserver {

using ::mpact::sim::generic::operator*;  // NOLINT

GdbServer::GdbServer(
    absl::Span<generic::CoreDebugInterface*> core_debug_interfaces,
    const DebugInfo& debug_info)
    : core_debug_interfaces_(core_debug_interfaces), debug_info_(debug_info) {}

GdbServer::~GdbServer() {
  Terminate();
  delete os_;
  delete is_;
  delete out_buf_;
  delete in_buf_;
}

bool GdbServer::Connect(int port) {
  good_ = true;
  if (port <= 0) {
    return false;
  }
  // Create the socket on the given port.
  server_socket_ =
      socket(/*domain=*/AF_INET, /*type=*/SOCK_STREAM, /*protocol=*/0);
  int one = 1;
  int err =
      setsockopt(server_socket_, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
  if (err != 0) {
    LOG(ERROR) << absl::StrFormat("Failed to set socket options: %s",
                                  strerror(errno));
    good_ = false;
    return false;
  }
  sockaddr_in server_address_int;
  server_address_int.sin_family = AF_INET;
  server_address_int.sin_addr.s_addr = INADDR_ANY;
  server_address_int.sin_port = htons(port);
  std::memset(&server_address_int.sin_zero, 0,
              sizeof(server_address_int.sin_zero));
  int res = bind(server_socket_,
                 reinterpret_cast<const sockaddr*>(&server_address_int),
                 sizeof(server_address_int));
  if (res != 0) {
    good_ = false;
    LOG(ERROR) << absl::StrFormat("Failed to bind to port %d: %s", port,
                                  strerror(errno));
    return false;
  }
  res = listen(server_socket_, /*backlog=*/1);
  if (res != 0) {
    good_ = false;
    LOG(ERROR) << absl::StrFormat("Failed to listen on port %d: %s", port,
                                  strerror(errno));
    return false;
  }

  // Accept the connection.
  cli_fd_ = accept(server_socket_, /*addr=*/nullptr, /*addrlen=*/nullptr);
  if (cli_fd_ == -1) {
    good_ = false;
    LOG(ERROR) << absl::StrFormat("Failed to accept connection on port %d: %s",
                                  port, strerror(errno));
    return false;
  }
  good_ = true;
  // Create the input and output buffers and streams to handle reads and writes
  // to the socket.
  out_buf_ = new SocketStreambuf(cli_fd_);
  in_buf_ = new SocketStreambuf(cli_fd_);
  os_ = new std::ostream(out_buf_);
  is_ = new std::istream(in_buf_);

  // First expect a plus.
  uint8_t val = is_->get();
  if (val != '+') {
    good_ = false;
    LOG(ERROR) << absl::StrFormat("Failed handshake with GDB client on port %d",
                                  port);
    Terminate();
    return false;
  }
  // Now read commands from the gdbserver client and process them.
  while (good_) {
    int buffer_pos = 0;
    // First character is always '$'.
    val = is_->get();
    if (val != '$') {
      if (val == 0xff) {
        LOG(INFO) << "Client disconnected";
        Terminate();
        return false;
      }
      good_ = false;
      LOG(ERROR) << absl::StrFormat(
          "Failed to receive command on port %d (expected '$', but received "
          "'%c' - 0x%x)",
          port, val, static_cast<int>(val));
      Terminate();
      return false;
    }
    buffer_[buffer_pos++] = val;
    // Next read the command until we see a '#'.
    do {
      val = is_->get();
      if (!is_->good()) {
        good_ = false;
        LOG(ERROR) << absl::StrFormat(
            "Failed to receive complete command on port %d received: '%s'",
            port,
            std::string_view(reinterpret_cast<char*>(buffer_), buffer_pos));
        Terminate();
        return false;
      }
      buffer_[buffer_pos++] = val;
    } while (val != '#');
    // Next read the checksum.
    buffer_[buffer_pos++] = is_->get();
    if (!is_->good()) break;
    buffer_[buffer_pos++] = is_->get();
    if (!is_->good()) break;
    if ((buffer_pos < 3) || (buffer_[buffer_pos - 3] != '#')) continue;

    AcceptGdbCommand(
        std::string_view(reinterpret_cast<char*>(buffer_), buffer_pos));
    buffer_pos = 0;
  }
  good_ = false;
  return true;
}

void GdbServer::Terminate() {
  // Shutdown the connection.
  if (cli_fd_ != -1) {
    int res = fcntl(cli_fd_, F_GETFD);
    if (res >= 0) {
      (void)shutdown(cli_fd_, SHUT_RDWR);
      (void)close(cli_fd_);
    }
  }
  if (server_socket_ != -1) {
    int res = shutdown(server_socket_, SHUT_RDWR);
    res = fcntl(server_socket_, F_GETFD);
    if (res >= 0) {
      (void)close(server_socket_);
    }
  }
  good_ = false;
}

void GdbServer::Respond(std::string_view response) {
  // Compute the checksum of the response.
  uint8_t checksum = 0;
  for (char c : response) {
    checksum += c;
  }
  // Format the response string.
  std::string response_str = absl::StrCat(
      "$", response, "#", absl::StrFormat("%02x", static_cast<int>(checksum)));
  LOG(INFO) << absl::StrFormat("Response: '%s'", response_str);
  uint8_t ack = 0;
  // Send the response to the GDB client.
  for (int i = 0; i < 5; ++i) {
    os_->write(response_str.data(), response_str.size());
    os_->flush();
    ack = is_->get();
    if (ack == '+') break;
    LOG(WARNING) << absl::StrFormat("Response not acknowledged ('%c' received)",
                                    ack);
  }
  if (ack != '+') {
    LOG(ERROR) << "Failed to send response after 5 attempts";
    Terminate();
  }
}

void GdbServer::SendError(std::string_view error) {
  Respond(absl::StrCat("E.", error));
}

void GdbServer::AcceptGdbCommand(std::string_view command) {
  LOG(INFO) << absl::StrFormat("Received: '%s'", command);
  // The command is on the form "$<command>#<2 digit checksum>".
  size_t pos = command.find_last_of('#');
  if ((command.front() != '$') || (pos == std::string_view::npos) ||
      (pos != command.size() - 3)) {
    LOG(ERROR) << "Invalid GDB command syntax";
    os_->put('-');
    os_->flush();
    return;
  }
  // Verify the checksum.
  std::string_view checksum_str = command.substr(pos + 1, 2);
  // Trim the command string.
  command.remove_prefix(1);
  command.remove_suffix(3);
  uint8_t checksum = 0;
  for (char c : command) {
    checksum += c;
  }
  int orig_checksum;
  bool success = absl::SimpleHexAtoi(checksum_str, &orig_checksum);
  if (!success) {
    LOG(ERROR) << absl::StrFormat("Invalid original checksum: '%s'",
                                  checksum_str);
    // Request a retransmission.
    os_->put('-');
    os_->flush();
    return;
  }
  if (checksum != orig_checksum) {
    LOG(ERROR) << absl::StrFormat("Invalid checksum: %x expected: %x",
                                  static_cast<int>(checksum), orig_checksum);

    // Request a retransmission.
    os_->put('-');
    os_->flush();
    return;
  }
  // Acknowledge the command.
  os_->put('+');
  os_->flush();
  std::string clean_command = UnescapeCommand(command);
  ParseGdbCommand(command);
}

void GdbServer::ParseGdbCommand(std::string_view command) {
  switch (command.front()) {
    default:
      // The command is not handled, so respond with an empty string which will
      // indicate that the command is not supported.
      Respond("");
      break;
    case '?':  // Inquire about the halt reason.
      GdbHaltReason();
      break;
    case 'c':  // Continue packet.
      command.remove_prefix(1);
      GdbContinue(command);
      break;
    case 'D':  // Detach packet.
      GdbDetach();
      break;
    case 'g':  // Read registers.
      GdbReadGprRegisters();
      break;
    case 'H':  // Select thread
      command.remove_prefix(1);
      GdbSelectThread(command);
      break;
    case 'k':  // Kill packet.
      Terminate();
      break;
    case 'm': {  // Read memory.
      command.remove_prefix(1);
      size_t pos = command.find(',');
      if (pos == std::string_view::npos) {
        if (error_message_supported_) {
          SendError("invalid memory read format");
        } else {
          Respond("E01");
        }
        return;
      }
      std::string_view address = command.substr(0, pos);
      std::string_view length = command.substr(pos + 1);
      GdbReadMemory(address, length);
      break;
    }
    case 'M': {  // Write memory.
      command.remove_prefix(1);
      size_t comma_pos = command.find(',');
      if (comma_pos == std::string_view::npos) {
        if (error_message_supported_) {
          SendError("invalid memory write format");
        } else {
          Respond("E01");
        }
        return;
      }
      size_t colon_pos = command.find(':');
      if (colon_pos == std::string_view::npos) {
        SendError("invalid memory write format");
        return;
      }
      std::string_view address = command.substr(0, comma_pos);
      std::string_view length =
          command.substr(comma_pos + 1, colon_pos - comma_pos - 1);
      std::string_view data = command.substr(colon_pos + 1);
      GdbWriteMemory(address, length, data);
      break;
    }
    case 'p': {  // Read register.
      command.remove_prefix(1);
      GdbReadRegister(command);
      break;
    }
    case 'P': {  // Write register.
      command.remove_prefix(1);
      size_t pos = command.find('=');
      if (pos == std::string_view::npos) {
        SendError("invalid register write format");
        return;
      }
      std::string_view register_name = command.substr(0, pos);
      std::string_view value = command.substr(pos + 1);
      GdbWriteRegister(register_name, value);
      break;
    }
    case 'q': {  // Query.
      command.remove_prefix(1);
      GdbQuery(command);
      break;
    }
    case 'Q': {  // Set.
      command.remove_prefix(1);
      GdbSet(command);
      break;
    }
    case 's':  // Step.
      command.remove_prefix(1);
      GdbStep(command);
      break;
    case 'z':
    case 'Z': {  // Add or remove breakpoint or watchpoint.
      // First make sure it's not a conditional breakpoint.
      if (command.find(';')) {
        Respond("");
        return;
      }
      char type = command.front();
      command.remove_prefix(1);
      size_t address_pos = command.find_first_of(',');
      if (address_pos == std::string_view::npos) {
        SendError("invalid remove breakpoint format");
        return;
      }
      size_t kind_pos = command.find_last_of(',');
      if (kind_pos == std::string_view::npos) {
        SendError("invalid remove breakpoint format");
        return;
      }
      std::string_view address =
          command.substr(address_pos + 1, kind_pos - address_pos - 1);
      std::string_view kind = command.substr(kind_pos + 1);
      if (type == 'z') {
        GdbRemoveBreakpoint(type, address, kind);
      } else {
        GdbAddBreakpoint(type, address, kind);
      }
      break;
    }
  }
}

void GdbServer::GdbHalt() {
  auto status = core_debug_interfaces_[0]->Halt(
      generic::CoreDebugInterface::HaltReason::kUserRequest);
  if (!status.ok()) {
    SendError(status.message());
    return;
  }
  status = core_debug_interfaces_[0]->Wait();
  if (!status.ok()) {
    SendError(status.message());
    return;
  }
  Respond(absl::StrCat("T03"));
}

void GdbServer::GdbHaltReason() {
  auto result = core_debug_interfaces_[0]->GetRunStatus();
  if (!result.ok()) {
    SendError(result.status().message());
    return;
  }
  if (result.value() == generic::CoreDebugInterface::RunStatus::kHalted) {
    auto result = core_debug_interfaces_[0]->GetLastHaltReason();
    if (!result.ok()) {
      SendError(result.status().message());
      return;
    }
    switch (result.value()) {
      default:
        Respond("T05");
        return;
      case *generic::CoreDebugInterface::HaltReason::kSoftwareBreakpoint:
      case *generic::CoreDebugInterface::HaltReason::kHardwareBreakpoint:
      case *generic::CoreDebugInterface::HaltReason::kDataWatchPoint:
      case *generic::CoreDebugInterface::HaltReason::kActionPoint:
        Respond("T02");
        return;
      case *generic::CoreDebugInterface::HaltReason::kSimulatorError:
        Respond("T06");
        return;
      case *generic::CoreDebugInterface::HaltReason::kUserRequest:
        Respond("T03");
        return;
      case *generic::CoreDebugInterface::HaltReason::kProgramDone:
        Respond("W00");
        return;
    }
  }
  Respond("E01");
}

void GdbServer::GdbContinue(std::string_view command) {
  auto result = core_debug_interfaces_[0]->GetRunStatus();
  if (!result.ok()) {
    SendError(result.status().message());
    return;
  }
  if (result.value() == generic::CoreDebugInterface::RunStatus::kHalted) {
    if (!command.empty()) {
      uint64_t address;
      bool success = absl::SimpleHexAtoi(command, &address);
      if (!success) {
        SendError("invalid address");
        return;
      }
      auto status = core_debug_interfaces_[0]->WriteRegister("pc", address);
      if (!status.ok()) {
        SendError(status.message());
        return;
      }
    }
    result = core_debug_interfaces_[0]->Run();
    if (!result.ok()) {
      SendError(result.status().message());
      return;
    }
  }
}

void GdbServer::GdbDetach() {
  Respond("OK");
  Terminate();
}

void GdbServer::GdbSelectThread(std::string_view command) {
  int thread_id;
  char op = command.front();
  command.remove_prefix(1);
  if (command == "-1") {
    thread_id = -1;
  } else {
    bool success = absl::SimpleHexAtoi(command, &thread_id);
    if (!success) {
      SendError("invalid thread id");
      return;
    }
  }
  thread_select_[op] = thread_id;
  Respond("OK");
}

void GdbServer::GdbThreadInfo() {
  std::string response = "m0";

  for (int i = 1; i < core_debug_interfaces_.size(); ++i) {
    absl::StrAppend(&response, ",", absl::Hex(i));
  }
  Respond(response);
}

void GdbServer::GdbReadMemory(std::string_view address_str,
                              std::string_view length_str) {
  uint64_t address;
  bool success = absl::SimpleHexAtoi(address_str, &address);
  if (!success) {
    if (error_message_supported_) {
      SendError("invalid memory read format");
    } else {
      Respond("E01");
    }
    return;
  }
  uint64_t length;
  success = absl::SimpleHexAtoi(length_str, &length);
  if (!success) {
    if (error_message_supported_) {
      SendError("invalid memory read format");
    } else {
      Respond("E01");
    }
    return;
  }
  if (length > sizeof(buffer_)) {
    if (error_message_supported_) {
      SendError("length exceeds buffer size of 4096");
    } else {
      Respond("E01");
    }
    return;
  }
  auto result = core_debug_interfaces_[0]->ReadMemory(address, buffer_, length);
  if (!result.ok()) {
    if (error_message_supported_) {
      SendError(result.status().message());
    } else {
      Respond("E01");
    }
    return;
  }
  std::string response;
  for (int i = 0; i < result.value(); ++i) {
    absl::StrAppend(&response, absl::Hex(buffer_[i], absl::kZeroPad2));
  }
  Respond(response);
}

void GdbServer::GdbWriteMemory(std::string_view address_str,
                               std::string_view length_str,
                               std::string_view data) {
  uint64_t address;
  bool success = absl::SimpleHexAtoi(address_str, &address);
  if (!success) {
    if (error_message_supported_) {
      SendError("invalid memory read format");
    } else {
      Respond("E01");
    }
    return;
  }
  uint64_t length;
  success = absl::SimpleHexAtoi(length_str, &length);
  if (!success) {
    if (error_message_supported_) {
      SendError("invalid memory read format");
    } else {
      Respond("E01");
    }
    return;
  }
  if (length > sizeof(buffer_)) {
    if (error_message_supported_) {
      SendError("length exceeds buffer size of 4096");
    } else {
      Respond("E01");
    }
    return;
  }
  int num_bytes = 0;
  for (int i = 0; (i < length) && (data.size() >= 2); ++i) {
    num_bytes++;
    std::string_view byte = data.substr(0, 2);
    data.remove_prefix(2);
    uint8_t value;
    (void)absl::SimpleHexAtoi(byte, &value);
    buffer_[i] = value;
  }
  if (num_bytes != length) {
    if (error_message_supported_) {
      SendError("length does not match data size");
    } else {
      Respond("E01");
    }
    return;
  }
  auto result =
      core_debug_interfaces_[0]->WriteMemory(address, buffer_, num_bytes);
  if (!result.ok()) {
    if (error_message_supported_) {
      SendError(result.status().message());
    } else {
      Respond("E01");
    }
    return;
  }
  if (result.value() != length) {
    if (error_message_supported_) {
      SendError("length does not match number of bytes written");
    } else {
      Respond("E01");
    }
    return;
  }
  Respond("OK");
}

void GdbServer::GdbReadGprRegisters() {
  std::string response;
  for (int i = debug_info_.GetFirstGpr(); i <= debug_info_.GetLastGpr(); ++i) {
    auto it = debug_info_.debug_register_map().find(i);
    if (it == debug_info_.debug_register_map().end()) {
      SendError("Internal error - failed to find register");
      return;
    }
    const std::string& register_name = it->second;
    auto result = core_debug_interfaces_[0]->ReadRegister(register_name);
    if (!result.ok()) {
      SendError(result.status().message());
      return;
    }
    LOG(INFO) << absl::StrFormat("Register %s = %x", register_name,
                                 result.value());
    uint64_t value = result.value();
    for (int j = 0; j < debug_info_.GetGprWidth(); j += 8) {
      absl::StrAppend(&response, absl::Hex(value & 0xff, absl::kZeroPad2));
      value >>= 8;
    }
  }
  Respond(response);
}

void GdbServer::GdbWriteGprRegisters(std::string_view data) {
  int num_bytes = 0;
  for (int i = debug_info_.GetFirstGpr(); i <= debug_info_.GetLastGpr(); ++i) {
    auto it = debug_info_.debug_register_map().find(i);
    if (it == debug_info_.debug_register_map().end()) {
      SendError("Internal error - failed to find register");
      return;
    }
    const std::string& register_name = it->second;
    uint64_t value = 0;
    for (int j = 0; j < debug_info_.GetGprWidth(); j += 8) {
      std::string_view byte = data.substr(0, 2);
      data.remove_prefix(2);
      uint8_t byte_value;
      (void)absl::SimpleHexAtoi(byte, &byte_value);
      value |= byte_value << (j * 8);
      num_bytes++;
    }
    auto status =
        core_debug_interfaces_[0]->WriteRegister(register_name, value);
    if (!status.ok()) {
      SendError(status.message());
      return;
    }
  }
  Respond("OK");
}

void GdbServer::GdbReadRegister(std::string_view register_number_str) {
  uint64_t register_number;
  bool success = absl::SimpleHexAtoi(register_number_str, &register_number);
  if (!success) {
    SendError("invalid register number");
    return;
  }
  auto it = debug_info_.debug_register_map().find(register_number);
  if (it == debug_info_.debug_register_map().end()) {
    SendError("invalid register number");
    return;
  }
  const std::string& register_name = it->second;
  auto result = core_debug_interfaces_[0]->ReadRegister(register_name);
  if (!result.ok()) {
    SendError(result.status().message());
    return;
  }
  std::string response;
  uint64_t value = result.value();
  while (value > 0) {
    absl::StrAppend(&response, absl::Hex(value & 0xff, absl::kZeroPad2));
    value >>= 8;
  }
  Respond(response);
}

void GdbServer::GdbWriteRegister(std::string_view register_number_str,
                                 std::string_view register_value_str) {
  uint64_t register_number;
  bool success = absl::SimpleHexAtoi(register_number_str, &register_number);
  if (!success) {
    SendError("invalid register number");
    return;
  }
  auto it = debug_info_.debug_register_map().find(register_number);
  if (it == debug_info_.debug_register_map().end()) {
    SendError("invalid register number");
    return;
  }
  const std::string& register_name = it->second;
  uint64_t value = 0;
  int count = 0;
  while (register_value_str.size() > 0) {
    std::string_view byte = register_value_str.substr(0, 2);
    register_value_str.remove_prefix(2);
    uint8_t byte_value;
    (void)absl::SimpleHexAtoi(byte, &byte_value);
    value |= byte_value << (count * 8);
    count++;
  }
  auto status = core_debug_interfaces_[0]->WriteRegister(register_name, value);
  if (!status.ok()) {
    SendError(status.message());
    return;
  }
  Respond("OK");
}

void GdbServer::GdbQuery(std::string_view command) {
  switch (command.front()) {
    default:
      break;
    case 'A':  // Attached?
      if (command.starts_with("Attached")) {
        Respond("0");
        return;
      }
      break;
    case 'C':  // Current thread ID.
      if (command == "C") {
        Respond(absl::StrCat("QC", current_thread_id_));
        return;
      }
      break;
    case 'E':  // ExecAndArgs?
      if (command.starts_with("ExecAndArgs")) {
        command.remove_prefix(11);
        GdbExecAndArgs(command);
        return;
      }
      break;
    case 'f':
      if (command.starts_with("fThreadInfo")) {
        GdbThreadInfo();
        return;
      }
      break;
    case 's':
      if (command.starts_with("sThreadInfo")) {
        Respond("l");
        return;
      }
      break;
    case 'S': {  // Search, Supported, or Symbol.
      if (command.starts_with("Search")) break;
      if (command.starts_with("Symbol")) break;
      if (command.starts_with("Supported")) {
        command.remove_prefix(9);
        GdbSupported(command);
        return;
      }
    }
  }
  Respond("");  // Not supported for now.
}

void GdbServer::GdbSet(std::string_view command) {
  Respond("");
  // TODO(torerik): Implement.
}

void GdbServer::GdbStep(std::string_view command) {
  auto result = core_debug_interfaces_[0]->Step(1);
  if (!result.ok()) {
    SendError(result.status().message());
    return;
  }
  Respond("T05");
}

void GdbServer::GdbAddBreakpoint(char type, std::string_view address_str,
                                 std::string_view kind_str) {
  uint64_t address;
  bool success = absl::SimpleHexAtoi(address_str, &address);
  if (!success) {
    SendError("invalid breakpoint address");
    return;
  }
  size_t kind;
  switch (type) {
    default:
      SendError("invalid breakpoint type");
      return;
    case '0': {  // Software breakpoint.
      auto status = core_debug_interfaces_[0]->SetSwBreakpoint(address);
      if (!status.ok()) {
        SendError(status.message());
        return;
      }
      Respond("OK");
      return;
    }
    case '2': {  // Data watchpoint write only.
      bool success = absl::SimpleHexAtoi(kind_str, &kind);
      if (!success) {
        SendError("invalid watchpoint kind");
        return;
      }
      auto status = core_debug_interfaces_[0]->SetDataWatchpoint(
          address, kind, generic::AccessType::kStore);
      if (!status.ok()) {
        SendError(status.message());
        return;
      }
      Respond("OK");
      return;
    }
    case '3': {  // Data watchpoint read-only.
      bool success = absl::SimpleHexAtoi(kind_str, &kind);
      if (!success) {
        SendError("invalid watchpoint kind");
        return;
      }
      auto status = core_debug_interfaces_[0]->SetDataWatchpoint(
          address, kind, generic::AccessType::kLoad);
      if (!status.ok()) {
        SendError(status.message());
        return;
      }
      Respond("OK");
      return;
    }
    case '4': {  // Data watchpoint read or write.
      bool success = absl::SimpleHexAtoi(kind_str, &kind);
      if (!success) {
        SendError("invalid watchpoint kind");
        return;
      }
      auto status = core_debug_interfaces_[0]->SetDataWatchpoint(
          address, kind, generic::AccessType::kLoadStore);
      if (!status.ok()) {
        SendError(status.message());
        return;
      }
      Respond("OK");
      break;
    }
  }
  // TODO(torerik): Implement.
  Respond("");
}

void GdbServer::GdbRemoveBreakpoint(char type, std::string_view address_str,
                                    std::string_view kind_str) {
  uint64_t address;
  bool success = absl::SimpleHexAtoi(address_str, &address);
  if (!success) {
    SendError("invalid breakpoint address");
    return;
  }
  switch (type) {
    default:
      SendError("invalid breakpoint type");
      return;
    case '0': {  // Software breakpoint.
      auto status = core_debug_interfaces_[0]->ClearSwBreakpoint(address);
      if (!status.ok()) {
        SendError(status.message());
        return;
      }
      Respond("OK");
      break;
    }
    case 2: {  // Data watchpoint write only.
      auto status = core_debug_interfaces_[0]->ClearDataWatchpoint(
          address, generic::AccessType::kStore);
      if (!status.ok()) {
        SendError(status.message());
        return;
      }
      Respond("OK");
      return;
    }
    case 3: {  // Data watchpoint read-only.
      auto status = core_debug_interfaces_[0]->ClearDataWatchpoint(
          address, generic::AccessType::kLoad);
      if (!status.ok()) {
        SendError(status.message());
        return;
      }
      Respond("OK");
      return;
    }
    case 4: {  // Data watchpoint read or write.
      auto status = core_debug_interfaces_[0]->ClearDataWatchpoint(
          address, generic::AccessType::kLoadStore);
      if (!status.ok()) {
        SendError(status.message());
        return;
      }
      Respond("OK");
      return;
    }
  }
  Respond("");
}

void GdbServer::GdbSupported(std::string_view command) {
  // Read supported GDB features.
  if (command.front() == ':') {
    command.remove_prefix(1);
    std::vector<std::string> features = absl::StrSplit(command, ';');
    for (const std::string& feature : features) {
      if (feature == "error-message+") {
        error_message_supported_ = true;
      }
    }
  }
  std::string response;
  absl::StrAppend(
      &response, "PacketSize=8192;multi-wp-addr-",
      ";multiprocess-;hwbreak-;qRelocInsn-;fork-events-;exec-events-"
      ";vContSupported-;QThreadEvents-;QThreadOptions-;no-resumed-"
      ";memory-tagging-;vfork-events-");
  Respond(response);
}

void GdbServer::GdbExecAndArgs(std::string_view command) {
  std::string file_name = core_debug_interfaces_[0]->GetExecutableFileName();
  Respond(file_name);
}

}  // namespace mpact::sim::util::gdbserver
