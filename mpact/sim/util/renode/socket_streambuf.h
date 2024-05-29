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
