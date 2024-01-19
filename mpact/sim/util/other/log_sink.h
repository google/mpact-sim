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

#ifndef LEARNING_BRAIN_RESEARCH_MPACT_SYSMODEL_LOG_SINK_H_
#define LEARNING_BRAIN_RESEARCH_MPACT_SYSMODEL_LOG_SINK_H_

#include <list>
#include <string>

#include "absl/base/log_severity.h"
#include "absl/log/log_entry.h"
#include "absl/log/log_sink.h"

namespace mpact {
namespace sim {
namespace util {

// This class is used to count the number of log messages in different
// categories that are encountered. It can be used to determine if significant
// errors have occurred. This does not replace any other log sinks, just
// provides an additional sink for monitoring purposes.
class LogSink : public absl::LogSink {
 public:
  LogSink() = default;
  ~LogSink() override = default;
  // Method called for each log message. Only look at the severity and count
  // the message in the appropriate category.
  void Send(const absl::LogEntry& entry) override {
    switch (entry.log_severity()) {
      case absl::LogSeverity::kInfo:
        num_info_++;
        break;
      case absl::LogSeverity::kWarning:
        oops_entries_.emplace_back(entry.text_message());
        num_warning_++;
        break;
      case absl::LogSeverity::kError:
        oops_entries_.emplace_back(entry.text_message());
        num_error_++;
        break;
      case absl::LogSeverity::kFatal:
        oops_entries_.emplace_back(entry.text_message());
        num_fatal_++;
        break;
    }
  }

  // Clear all the counts.
  void Reset() {
    num_info_ = 0;
    num_warning_ = 0;
    num_error_ = 0;
    num_fatal_ = 0;
  }

  int NumOops() const { return num_warning_ + num_error_ + num_fatal_; }

  // Getters.
  int num_info() const { return num_info_; }
  int num_warning() const { return num_warning_; }
  int num_error() const { return num_error_; }
  int num_fatal() const { return num_fatal_; }

  const std::list<std::string>& oops_entries() const { return oops_entries_; }

 private:
  std::list<std::string> oops_entries_;
  int num_info_ = 0;
  int num_warning_ = 0;
  int num_error_ = 0;
  int num_fatal_ = 0;
};

}  // namespace util
}  // namespace sim
}  // namespace mpact

#endif  // LEARNING_BRAIN_RESEARCH_MPACT_SYSMODEL_LOG_SINK_H_
