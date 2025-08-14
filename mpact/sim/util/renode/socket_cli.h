#ifndef MPACT_SIM_UTIL_RENODE_SOCKET_CLI_H_
#define MPACT_SIM_UTIL_RENODE_SOCKET_CLI_H_

#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>

#include <thread>

#include "absl/functional/any_invocable.h"
#include "mpact/sim/generic/debug_command_shell_interface.h"
#include "mpact/sim/util/program_loader/elf_program_loader.h"

// This file defines a class that instantiates a command line interface for
// the simulator connected to a bidirectional socket. This is used to provide
// a cli to the simulator when the simulator is being run within a library
// from ReNode.

namespace mpact {
namespace sim {
namespace util {
namespace renode {

using ::mpact::sim::generic::DebugCommandShellInterface;
using ::mpact::sim::util::ElfProgramLoader;

class SocketCLI {
 public:
  // Constructor takes the port number to listen to, the top level CherIoT
  // simulator interface, and a callback to notify when the connection status
  // of the port/cli changes.
  SocketCLI(int port, DebugCommandShellInterface& dbg_shell,
            absl::AnyInvocable<void(bool)> is_connected_cb);
  ~SocketCLI();

  bool good() const { return good_; }

 private:
  DebugCommandShellInterface& dbg_shell_;
  std::thread cli_thread_;
  bool good_ = false;
  int server_socket_ = -1;
  int cli_fd_ = -1;
  absl::AnyInvocable<void(bool)> is_connected_cb_;
};

}  // namespace renode
}  // namespace util
}  // namespace sim
}  // namespace mpact

#endif  // MPACT_SIM_UTIL_RENODE_SOCKET_CLI_H_
