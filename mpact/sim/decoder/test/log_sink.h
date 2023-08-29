#ifndef MPACT_SIM_DECODER_TEST_LOG_SINK_H_
#define MPACT_SIM_DECODER_TEST_LOG_SINK_H_

#include <string>

#include "absl/base/log_severity.h"
#include "absl/log/log_entry.h"
#include "absl/log/log_sink.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"

namespace mpact {
namespace sim {
namespace test {

class LogSink : public absl::LogSink {
 public:
  LogSink() = default;
  ~LogSink() override = default;
  // Method called for each log message.
  void Send(const absl::LogEntry &entry) override {
    switch (entry.log_severity()) {
      case absl::LogSeverity::kInfo:
        absl::StrAppend(&info_log_, entry.text_message());
        break;
      case absl::LogSeverity::kWarning:
        absl::StrAppend(&warning_log_, entry.text_message());
        break;
      case absl::LogSeverity::kError:
        absl::StrAppend(&error_log_, entry.text_message());
        break;
      case absl::LogSeverity::kFatal:
        absl::StrAppend(&fatal_log_, entry.text_message());
        break;
    }
  }

  absl::string_view info_log() const { return info_log_; }
  absl::string_view warning_log() const { return warning_log_; }
  absl::string_view error_log() const { return error_log_; }
  absl::string_view fatal_log() const { return fatal_log_; }

 private:
  std::string info_log_;
  std::string warning_log_;
  std::string error_log_;
  std::string fatal_log_;
};

}  // namespace test
}  // namespace sim
}  // namespace mpact

#endif  // MPACT_SIM_DECODER_TEST_LOG_SINK_H_
