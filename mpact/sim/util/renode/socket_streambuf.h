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
#ifndef MPACT_SIM_UTIL_RENODE_SOCKET_STREAMBUF_H_
#define MPACT_SIM_UTIL_RENODE_SOCKET_STREAMBUF_H_

#include <unistd.h>

#include <cstdio>
#include <streambuf>

namespace mpact {
namespace sim {
namespace util {
namespace renode {

// This class is used to provide a streambuf interface to a socket file
// descriptor, so that it can be used to access a socket as an istream/ostream.
// The overflow and underflow methods are the bare minimum that's required to
// implement to make this happen. No buffering is performed.
class SocketStreambuf : public std::streambuf {
 public:
  explicit SocketStreambuf(int fd) : fd_(fd) {}
  ~SocketStreambuf() override { close(fd_); }

  // On overflow write character to the socket fd.
  int overflow(int c) override {
    if (c == EOF) {
      close(fd_);
      return c;
    }
    if (c == '\xa') {
      write(fd_, "\r\n", 2);
      return c;
    }
    write(fd_, &c, 1);
    return c;
  }

  // On underflow read character from the socket fd.
  std::streambuf::int_type underflow() override {
    int count = read(fd_, &ch_, 1);
    if (count == 0) return traits_type::eof();
    if (ch_ == '\r') {
      count = read(fd_, &ch_, 1);
      if (count == 0) return traits_type::eof();
    }
    setg(&ch_, &ch_, &ch_ + 1);
    return traits_type::to_int_type(ch_);
  }

 private:
  char ch_;
  int fd_;
};

}  // namespace renode
}  // namespace util
}  // namespace sim
}  // namespace mpact

#endif  // MPACT_SIM_UTIL_RENODE_SOCKET_STREAMBUF_H_
