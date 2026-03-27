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

#include "mpact/sim/util/gdbserver/gdbserver.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "absl/types/span.h"
#include "googlemock/include/gmock/gmock.h"
#include "googletest/include/gtest/gtest.h"
#include "mpact/sim/generic/core_debug_interface.h"
#include "mpact/sim/generic/data_buffer.h"
#include "mpact/sim/generic/debug_info.h"
#include "mpact/sim/generic/instruction.h"
#include "net/util/ports.h"
#include "thread/fiber/fiber.h"

namespace mpact::sim::util::gdbserver {
namespace {

using ::mpact::sim::generic::AccessType;
using ::mpact::sim::generic::CoreDebugInterface;
using ::mpact::sim::generic::DataBufferFactory;
using ::mpact::sim::generic::DebugInfo;
using RunStatus = ::mpact::sim::generic::CoreDebugInterface::RunStatus;
using HaltReason = ::mpact::sim::generic::CoreDebugInterface::HaltReason;
using HaltReasonValueType =
    ::mpact::sim::generic::CoreDebugInterface::HaltReasonValueType;
using ::mpact::sim::generic::Instruction;
using ::testing::_;
using ::testing::Return;

// Mock debug interface for testing.
class MockCoreDebugInterface : public CoreDebugInterface {
 public:
  MOCK_METHOD(absl::StatusOr<uint64_t>, ReadRegister, (const std::string& name),
              (override));
  MOCK_METHOD(absl::Status, WriteRegister,
              (const std::string& name, uint64_t value), (override));
  MOCK_METHOD(absl::StatusOr<size_t>, ReadMemory,
              (uint64_t address, void* buf, size_t length), (override));
  MOCK_METHOD(absl::StatusOr<size_t>, WriteMemory,
              (uint64_t address, const void* buf, size_t length), (override));
  MOCK_METHOD(absl::StatusOr<generic::DataBuffer*>, GetRegisterDataBuffer,
              (const std::string& name), (override));
  MOCK_METHOD(absl::Status, Run, (), (override));
  MOCK_METHOD(absl::Status, Halt, (), (override));
  MOCK_METHOD(absl::Status, Halt, (HaltReason reason), (override));
  MOCK_METHOD(absl::Status, Halt, (HaltReasonValueType halt_reason),
              (override));
  MOCK_METHOD(absl::StatusOr<int>, Step, (int num), (override));
  MOCK_METHOD(absl::StatusOr<RunStatus>, GetRunStatus, (), (override));
  MOCK_METHOD(absl::Status, Wait, (), (override));
  MOCK_METHOD(absl::StatusOr<HaltReasonValueType>, GetLastHaltReason, (),
              (override));
  MOCK_METHOD(absl::Status, SetSwBreakpoint, (uint64_t address), (override));
  MOCK_METHOD(absl::Status, ClearSwBreakpoint, (uint64_t address), (override));
  MOCK_METHOD(absl::Status, ClearAllSwBreakpoints, (), (override));
  MOCK_METHOD(bool, HasBreakpoint, (uint64_t address), (override));
  MOCK_METHOD(uint64_t, GetSwBreakpointInfo, (), (const, override));
  MOCK_METHOD(absl::Status, SetDataWatchpoint,
              (uint64_t address, size_t length, AccessType access_type),
              (override));
  MOCK_METHOD(absl::Status, ClearDataWatchpoint,
              (uint64_t address, AccessType access_type), (override));
  MOCK_METHOD(void, GetWatchpointInfo,
              (uint64_t& address, AccessType& access_type), (override));
  MOCK_METHOD(std::string, GetExecutableFileName, (), (override));
  MOCK_METHOD(absl::StatusOr<Instruction*>, GetInstruction, (uint64_t address),
              (override));
  MOCK_METHOD(absl::StatusOr<std::string>, GetDisassembly, (uint64_t address),
              (override));
};

// Test debug info class.
class TestDebugInfo : public DebugInfo {
 public:
  TestDebugInfo() {
    for (int i = 0; i < 32; ++i) {
      debug_register_map_[i] = absl::StrCat("x", i);
    }
    debug_register_map_[32] = "pc";
  }
  const DebugRegisterMap& debug_register_map() const override {
    return debug_register_map_;
  }
  int GetFirstGpr() const override { return 0; }
  int GetLastGpr() const override { return 31; }
  int GetGprWidth() const override { return 64; }
  int GetRegisterByteWidth(int register_number) const override {
    return 64 / 8;
  }
  std::string_view GetLLDBHostInfo() const override { return ""; }
  std::string_view GetGdbTargetXml() const override { return ""; }

 private:
  DebugRegisterMap debug_register_map_;
};

// Minimal GDB client for testing.
class GdbTestClient {
 public:
  GdbTestClient() = default;
  ~GdbTestClient() {
    if (sock_fd_ != -1) {
      close(sock_fd_);
    }
  }

  // Connect to the server.
  bool Connect(int port) {
    sock_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_fd_ < 0) {
      LOG(ERROR) << "Socket creation error";
      return false;
    }

    struct sockaddr_in serv_addr;
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);

    if (inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr) <= 0) {
      LOG(ERROR) << "Invalid address/ Address not supported";
      return false;
    }

    if (connect(sock_fd_, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) <
        0) {
      LOG(ERROR) << "Connection Failed";
      return false;
    }
    return true;
  }

  // GDB handshake: send '+'.
  void SendAck() {
    char ack = '+';
    send(sock_fd_, &ack, 1, 0);
  }

  // Read ack from server, expecting '+'.
  bool ExpectAck() {
    char ack = 0;
    read(sock_fd_, &ack, 1);
    return ack == '+';
  }

  // Send a command string, wrapping it in GDB packet format.
  void SendCommand(const std::string& command) {
    uint8_t checksum = 0;
    for (char c : command) {
      checksum += c;
    }
    std::string packet =
        absl::StrFormat("$%s#%02x", command, static_cast<int>(checksum));
    send(sock_fd_, packet.c_str(), packet.length(), 0);
  }

  // Receive a response packet from server and extract content.
  std::string ReceiveResponse() {
    char c;
    // Look for '$'.
    do {
      read(sock_fd_, &c, 1);
    } while (c != '$');

    std::string response;
    uint8_t checksum = 0;
    while (true) {
      read(sock_fd_, &c, 1);
      if (c == '#') break;
      response += c;
      checksum += c;
    }
    char checksum_chars[3];
    int read_count = 0;
    while (read_count < 2) {
      read_count += read(sock_fd_, &checksum_chars[read_count], 2 - read_count);
    }
    checksum_chars[2] = '\0';
    int received_checksum;
    if (!absl::SimpleHexAtoi(checksum_chars, &received_checksum)) {
      LOG(ERROR) << "Invalid checksum format in response";
      return "ERROR";
    }
    if (checksum != received_checksum) {
      LOG(ERROR) << "Checksum mismatch: expected "
                 << absl::StrFormat("%02x", checksum) << ", got "
                 << checksum_chars;
      return "ERROR";
    }
    return response;
  }

 private:
  int sock_fd_ = -1;
};

class GdbServerTest : public ::testing::Test {
 protected:
  GdbServerTest() {
    core_debug_interfaces_.push_back(&mock_core_);
    debug_info_ = new TestDebugInfo();
    gdb_server_ =
        new GdbServer(absl::MakeSpan(core_debug_interfaces_), *debug_info_);
  }

  ~GdbServerTest() override {
    delete gdb_server_;
    delete debug_info_;
  }

  MockCoreDebugInterface mock_core_;
  std::vector<CoreDebugInterface*> core_debug_interfaces_;
  DebugInfo* debug_info_;
  GdbServer* gdb_server_;
  DataBufferFactory db_factory_;
};

TEST_F(GdbServerTest, Detach) {
  int port = net_util::PickUnusedPortOrDie();

  thread::Fiber server_fiber([&]() { gdb_server_->Connect(port); });

  // Give server time to start up and listen.
  absl::SleepFor(absl::Milliseconds(100));

  GdbTestClient client;
  ASSERT_TRUE(client.Connect(port));

  // Initial handshake.
  client.SendAck();
  // Send 'D' - detach command.
  client.SendCommand("D");
  // Server should ack command.
  EXPECT_TRUE(client.ExpectAck());
  // Server should respond with OK.
  EXPECT_EQ("OK", client.ReceiveResponse());
  // Client should ack response.
  client.SendAck();

  server_fiber.Join();
}

TEST_F(GdbServerTest, HaltReason) {
  int port = net_util::PickUnusedPortOrDie();
  EXPECT_CALL(mock_core_, GetLastHaltReason())
      .WillOnce(Return(
          static_cast<HaltReasonValueType>(HaltReason::kSoftwareBreakpoint)));

  thread::Fiber server_fiber([&]() { gdb_server_->Connect(port); });

  // Give server time to start up and listen.
  absl::SleepFor(absl::Milliseconds(100));

  GdbTestClient client;
  ASSERT_TRUE(client.Connect(port));

  // Initial handshake.
  client.SendAck();
  // Send '?' - halt reason command.
  client.SendCommand("?");
  // Server should ack command.
  EXPECT_TRUE(client.ExpectAck());
  // Server should respond with T02thread:1;.
  EXPECT_EQ("T02thread:1;", client.ReceiveResponse());
  // Client should ack response.
  client.SendAck();

  // Send 'D' - detach command to terminate connection.
  client.SendCommand("D");
  EXPECT_TRUE(client.ExpectAck());
  EXPECT_EQ("OK", client.ReceiveResponse());
  client.SendAck();

  server_fiber.Join();
}

TEST_F(GdbServerTest, ReadGpr) {
  int port = net_util::PickUnusedPortOrDie();

  std::vector<std::array<uint8_t, 8>> reg_storage(32);
  std::vector<generic::DataBuffer*> dbs;
  for (int i = 0; i < 32; ++i) {
    generic::DataBuffer* db = db_factory_.Allocate<uint8_t>(8);
    dbs.push_back(db);
    uint64_t reg_val = 0x0102030405060700 | i;
    memcpy(db->raw_ptr(), &reg_val, 8);
  }

  for (int i = 0; i < 32; ++i) {
    EXPECT_CALL(mock_core_, GetRegisterDataBuffer(absl::StrCat("x", i)))
        .WillOnce(Return(dbs[i]));
  }

  thread::Fiber server_fiber([&]() { gdb_server_->Connect(port); });

  absl::SleepFor(absl::Milliseconds(100));

  GdbTestClient client;
  ASSERT_TRUE(client.Connect(port));

  client.SendAck();
  client.SendCommand("g");
  EXPECT_TRUE(client.ExpectAck());
  std::string response = client.ReceiveResponse();
  client.SendAck();
  // 32 registers * 8 bytes/reg * 2 hex chars/byte = 512 hex chars.
  EXPECT_EQ(response.length(), 32 * 8 * 2);
  // Reg x0: 0007060504030201
  EXPECT_EQ(response.substr(0, 16), "0007060504030201");
  // Reg x31: 1f07060504030201
  EXPECT_EQ(response.substr(31 * 16, 16), "1f07060504030201");

  for (auto* db : dbs) db->DecRef();

  client.SendCommand("D");
  EXPECT_TRUE(client.ExpectAck());
  EXPECT_EQ("OK", client.ReceiveResponse());
  client.SendAck();
  server_fiber.Join();
}

TEST_F(GdbServerTest, ReadRegister) {
  int port = net_util::PickUnusedPortOrDie();

  uint64_t reg_val = 0x1234;
  generic::DataBuffer* db = db_factory_.Allocate<uint8_t>(8);
  memcpy(db->raw_ptr(), &reg_val, 8);

  // Reading register 32 ('pc', hex '20').
  EXPECT_CALL(mock_core_, GetRegisterDataBuffer("pc")).WillOnce(Return(db));

  thread::Fiber server_fiber([&]() { gdb_server_->Connect(port); });

  absl::SleepFor(absl::Milliseconds(100));

  GdbTestClient client;
  ASSERT_TRUE(client.Connect(port));

  client.SendAck();
  // Read register 0x20 (pc).
  client.SendCommand("p20");
  EXPECT_TRUE(client.ExpectAck());
  // 0x1234 in little endian hex is 3412000000000000 for 64 bits.
  EXPECT_EQ("3412000000000000", client.ReceiveResponse());
  client.SendAck();

  db->DecRef();

  client.SendCommand("D");
  EXPECT_TRUE(client.ExpectAck());
  EXPECT_EQ("OK", client.ReceiveResponse());
  client.SendAck();
  server_fiber.Join();
}

TEST_F(GdbServerTest, ReadMemory) {
  int port = net_util::PickUnusedPortOrDie();

  // Read 4 bytes from 0x1000.
  EXPECT_CALL(mock_core_, ReadMemory(0x1000, _, 4))
      .WillOnce([](uint64_t address, void* buf, size_t length) {
        uint8_t data[] = {0x01, 0x02, 0x03, 0x04};
        memcpy(buf, data, 4);
        return 4;
      });

  thread::Fiber server_fiber([&]() { gdb_server_->Connect(port); });

  absl::SleepFor(absl::Milliseconds(100));

  GdbTestClient client;
  ASSERT_TRUE(client.Connect(port));

  client.SendAck();
  // Read 4 bytes from 0x1000: m1000,4
  client.SendCommand("m1000,4");
  EXPECT_TRUE(client.ExpectAck());
  EXPECT_EQ("01020304", client.ReceiveResponse());
  client.SendAck();

  client.SendCommand("D");
  EXPECT_TRUE(client.ExpectAck());
  EXPECT_EQ("OK", client.ReceiveResponse());
  client.SendAck();

  server_fiber.Join();
}

TEST_F(GdbServerTest, Continue) {
  int port = net_util::PickUnusedPortOrDie();

  EXPECT_CALL(mock_core_, GetRunStatus()).WillOnce(Return(RunStatus::kHalted));
  EXPECT_CALL(mock_core_, Run()).WillOnce(Return(absl::OkStatus()));
  EXPECT_CALL(mock_core_, Wait()).WillOnce(Return(absl::OkStatus()));
  EXPECT_CALL(mock_core_, GetLastHaltReason())
      .WillOnce(Return(
          static_cast<HaltReasonValueType>(HaltReason::kHardwareBreakpoint)));

  thread::Fiber server_fiber([&]() { gdb_server_->Connect(port); });

  absl::SleepFor(absl::Milliseconds(100));

  GdbTestClient client;
  ASSERT_TRUE(client.Connect(port));

  client.SendAck();
  client.SendCommand("c");
  EXPECT_TRUE(client.ExpectAck());
  EXPECT_EQ("T02thread:1;", client.ReceiveResponse());
  client.SendAck();

  client.SendCommand("D");
  EXPECT_TRUE(client.ExpectAck());
  EXPECT_EQ("OK", client.ReceiveResponse());
  client.SendAck();

  server_fiber.Join();
}

TEST_F(GdbServerTest, Step) {
  int port = net_util::PickUnusedPortOrDie();

  EXPECT_CALL(mock_core_, Step(1)).WillOnce(Return(1));

  thread::Fiber server_fiber([&]() { gdb_server_->Connect(port); });

  absl::SleepFor(absl::Milliseconds(100));

  GdbTestClient client;
  ASSERT_TRUE(client.Connect(port));

  client.SendAck();
  client.SendCommand("s");
  EXPECT_TRUE(client.ExpectAck());
  EXPECT_EQ("T05", client.ReceiveResponse());
  client.SendAck();

  client.SendCommand("D");
  EXPECT_TRUE(client.ExpectAck());
  EXPECT_EQ("OK", client.ReceiveResponse());
  client.SendAck();

  server_fiber.Join();
}

TEST_F(GdbServerTest, WriteGpr) {
  int port = net_util::PickUnusedPortOrDie();

  std::vector<generic::DataBuffer*> dbs;
  for (int i = 0; i < 32; ++i) {
    generic::DataBuffer* db = db_factory_.Allocate<uint8_t>(8);
    dbs.push_back(db);
  }

  for (int i = 0; i < 32; ++i) {
    EXPECT_CALL(mock_core_, GetRegisterDataBuffer(absl::StrCat("x", i)))
        .WillOnce(Return(dbs[i]));
  }

  thread::Fiber server_fiber([&]() { gdb_server_->Connect(port); });

  absl::SleepFor(absl::Milliseconds(100));

  GdbTestClient client;
  ASSERT_TRUE(client.Connect(port));

  client.SendAck();
  std::string write_cmd = "G";
  for (int i = 0; i < 32; ++i) {
    absl::StrAppend(&write_cmd, absl::StrFormat("%02x07060504030201", i));
  }
  client.SendCommand(write_cmd);
  EXPECT_TRUE(client.ExpectAck());
  EXPECT_EQ("OK", client.ReceiveResponse());
  client.SendAck();

  for (int i = 0; i < 32; ++i) {
    EXPECT_EQ(dbs[i]->Get<uint64_t>(0), 0x0102030405060700 | i);
    dbs[i]->DecRef();
  }

  client.SendCommand("D");
  EXPECT_TRUE(client.ExpectAck());
  EXPECT_EQ("OK", client.ReceiveResponse());
  client.SendAck();

  server_fiber.Join();
}

TEST_F(GdbServerTest, WriteRegister) {
  int port = net_util::PickUnusedPortOrDie();

  generic::DataBuffer* db = db_factory_.Allocate<uint8_t>(8);

  // Writing register 32 ('pc', hex '20').
  EXPECT_CALL(mock_core_, GetRegisterDataBuffer("pc")).WillOnce(Return(db));

  thread::Fiber server_fiber([&]() { gdb_server_->Connect(port); });

  absl::SleepFor(absl::Milliseconds(100));

  GdbTestClient client;
  ASSERT_TRUE(client.Connect(port));

  client.SendAck();
  // Write register 0x20 (pc) with 0x1234.
  client.SendCommand("P20=3412000000000000");
  EXPECT_TRUE(client.ExpectAck());
  EXPECT_EQ("OK", client.ReceiveResponse());
  client.SendAck();

  EXPECT_EQ(db->Get<uint64_t>(0), 0x1234);

  db->DecRef();

  client.SendCommand("D");
  EXPECT_TRUE(client.ExpectAck());
  EXPECT_EQ("OK", client.ReceiveResponse());
  client.SendAck();

  server_fiber.Join();
}

TEST_F(GdbServerTest, WriteMemory) {
  int port = net_util::PickUnusedPortOrDie();

  // Write 4 bytes to 0x1000 with value 01020304.
  EXPECT_CALL(mock_core_, WriteMemory(0x1000, _, 4))
      .WillOnce([](uint64_t address, const void* buf,
                   size_t length) -> absl::StatusOr<size_t> {
        uint8_t expected[] = {0x01, 0x02, 0x03, 0x04};
        if (memcmp(buf, expected, 4) == 0) return 4;
        return absl::InternalError("Memory content mismatch");
      });

  thread::Fiber server_fiber([&]() { gdb_server_->Connect(port); });

  absl::SleepFor(absl::Milliseconds(100));

  GdbTestClient client;
  ASSERT_TRUE(client.Connect(port));

  client.SendAck();
  // Write 4 bytes to 0x1000: M1000,4:01020304
  client.SendCommand("M1000,4:01020304");
  EXPECT_TRUE(client.ExpectAck());
  EXPECT_EQ("OK", client.ReceiveResponse());
  client.SendAck();

  client.SendCommand("D");
  EXPECT_TRUE(client.ExpectAck());
  EXPECT_EQ("OK", client.ReceiveResponse());
  client.SendAck();

  server_fiber.Join();
}

TEST_F(GdbServerTest, SetSwBreakpoint) {
  int port = net_util::PickUnusedPortOrDie();

  EXPECT_CALL(mock_core_, SetSwBreakpoint(0x1000))
      .WillOnce(Return(absl::OkStatus()));

  thread::Fiber server_fiber([&]() { gdb_server_->Connect(port); });

  absl::SleepFor(absl::Milliseconds(100));
  GdbTestClient client;
  ASSERT_TRUE(client.Connect(port));

  client.SendAck();
  // Set sw breakpoint at 0x1000
  client.SendCommand("Z0,1000,0");
  EXPECT_TRUE(client.ExpectAck());
  EXPECT_EQ("OK", client.ReceiveResponse());
  client.SendAck();

  client.SendCommand("D");
  EXPECT_TRUE(client.ExpectAck());
  EXPECT_EQ("OK", client.ReceiveResponse());
  client.SendAck();

  server_fiber.Join();
}

TEST_F(GdbServerTest, ClearSwBreakpoint) {
  int port = net_util::PickUnusedPortOrDie();

  EXPECT_CALL(mock_core_, ClearSwBreakpoint(0x1000))
      .WillOnce(Return(absl::OkStatus()));

  thread::Fiber server_fiber([&]() { gdb_server_->Connect(port); });

  absl::SleepFor(absl::Milliseconds(100));
  GdbTestClient client;
  ASSERT_TRUE(client.Connect(port));

  client.SendAck();
  // Clear sw breakpoint at 0x1000
  client.SendCommand("z0,1000,0");
  EXPECT_TRUE(client.ExpectAck());
  EXPECT_EQ("OK", client.ReceiveResponse());
  client.SendAck();

  client.SendCommand("D");
  EXPECT_TRUE(client.ExpectAck());
  EXPECT_EQ("OK", client.ReceiveResponse());
  client.SendAck();

  server_fiber.Join();
}

}  // namespace
}  // namespace mpact::sim::util::gdbserver
