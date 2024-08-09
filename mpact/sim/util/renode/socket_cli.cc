// Copyright 2024 Google LLC
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

#include "mpact/sim/util/renode/socket_cli.h"

#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>

#include <cerrno>
#include <cstring>
#include <iostream>
#include <istream>
#include <ostream>
#include <thread>
#include <utility>

#include "absl/functional/any_invocable.h"
#include "absl/log/log.h"
#include "mpact/sim/generic/debug_command_shell_interface.h"
#include "mpact/sim/util/renode/socket_streambuf.h"

namespace mpact {
namespace sim {
namespace util {
namespace renode {

using ::mpact::sim::generic::DebugCommandShellInterface;

SocketCLI::SocketCLI(int port, DebugCommandShellInterface &dbg_shell,
                     absl::AnyInvocable<void(bool)> is_connected_cb)
    : dbg_shell_(dbg_shell), is_connected_cb_(std::move(is_connected_cb)) {
  // Set initial status as not connected.
  is_connected_cb_(false);
  good_ = true;
  // If the port has not been specified, just return.
  if (port == -1) {
    good_ = false;
    return;
  }
  // Create the socket on the given port.
  server_socket_ =
      socket(/*domain=*/AF_INET, /*type=*/SOCK_STREAM, /*protocol=*/0);
  int one = 1;
  int err =
      setsockopt(server_socket_, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
  if (err != 0) {
    LOG(ERROR) << "Failed to set socket options: " << strerror(errno);
    good_ = false;
    return;
  }
  sockaddr_in server_address_int;
  server_address_int.sin_family = AF_INET;
  server_address_int.sin_addr.s_addr = INADDR_ANY;
  server_address_int.sin_port = htons(port);
  std::memset(&server_address_int.sin_zero, 0,
              sizeof(server_address_int.sin_zero));
  int res = bind(server_socket_,
                 reinterpret_cast<const sockaddr *>(&server_address_int),
                 sizeof(server_address_int));
  if (res != 0) {
    good_ = false;
    LOG(ERROR) << "Failed to bind to port " << port << ": " << strerror(errno);
    return;
  }
  res = listen(server_socket_, /*backlog=*/1);
  if (res != 0) {
    good_ = false;
    LOG(ERROR) << "Failed to listen on port " << port << ": "
               << strerror(errno);
  }

  // Launch CLI thread.
  cli_thread_ = std::thread([this, port]() {
    // Accept the connection and set up streams.
    cli_fd_ = accept(server_socket_, /*addr=*/nullptr, /*addrlen=*/nullptr);
    if (cli_fd_ == -1) {
      good_ = false;
      LOG(ERROR) << "Failed to accept connection on port " << port << ": "
                 << strerror(errno);
    } else {
      good_ = true;
      // Create input and output streams attached to the socket from which the
      // cli will read commands/write responses.
      SocketStreambuf out_buf(cli_fd_);
      SocketStreambuf in_buf(cli_fd_);
      std::ostream os(&out_buf);
      if (!os.good()) return;
      std::istream is(&in_buf);
      if (!is.good()) return;
      // Notify that CLI is connected.
      is_connected_cb_(true);
      // Start the CLI.
      os << "CLI connected:\n";
      dbg_shell_.Run(is, os);
      // Notify that CLI is disconnected.
      is_connected_cb_(false);
    }
    good_ = false;
  });
}

SocketCLI::~SocketCLI() {
  // Shutdown the connection.
  if (cli_fd_ != -1) {
    (void)shutdown(cli_fd_, SHUT_RDWR);
    int err = close(cli_fd_);
    if (err != 0) {
      LOG(ERROR) << "Failed to close client socket " << cli_fd_ << ": "
                 << strerror(errno);
    }
  }
  if (server_socket_ != -1) {
    int res = shutdown(server_socket_, SHUT_RDWR);
    if (res != 0) {
      LOG(ERROR) << "Failed to shutdown server socket " << server_socket_
                 << ": " << strerror(errno);
    }
    res = fcntl(server_socket_, F_GETFD);
    if (res >= 0) {
      int err = close(server_socket_);
      if (err != 0) {
        LOG(ERROR) << "Failed to close server socket " << server_socket_ << ": "
                   << strerror(errno);
      }
    }
  }
  if (cli_thread_.joinable()) cli_thread_.join();
}

}  // namespace renode
}  // namespace util
}  // namespace sim
}  // namespace mpact
