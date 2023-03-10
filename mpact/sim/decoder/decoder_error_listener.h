// Copyright 2023 Google LLC
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

#ifndef MPACT_SIM_DECODER_DECODER_ERROR_LISTENER_H_
#define MPACT_SIM_DECODER_DECODER_ERROR_LISTENER_H_

#include <exception>
#include <string>
#include <utility>

#include "absl/strings/string_view.h"
#include "antlr4-runtime/antlr4-runtime.h"

namespace mpact {
namespace sim {
namespace decoder {

// An Antlr4 error listener to check for syntax errors.
class DecoderErrorListener : public antlr4::BaseErrorListener {
 public:
  void semanticError(antlr4::Token *token, absl::string_view msg);
  void semanticWarning(antlr4::Token *token, absl::string_view msg);
  void syntaxError(antlr4::Recognizer *recognizer,
                   antlr4::Token *offendingSymbol, size_t line,
                   size_t charPositionInLine, const std::string &msg,
                   std::exception_ptr e) override;
  void reportAmbiguity(antlr4::Parser *recognizer, const antlr4::dfa::DFA &dfa,
                       size_t startIndex, size_t stopIndex, bool exact,
                       const antlrcpp::BitSet &ambigAlts,
                       antlr4::atn::ATNConfigSet *configs) override;
  void reportAttemptingFullContext(antlr4::Parser *recognizer,
                                   const antlr4::dfa::DFA &dfa,
                                   size_t startIndex, size_t stopIndex,
                                   const antlrcpp::BitSet &conflictingAlts,
                                   antlr4::atn::ATNConfigSet *configs) override;
  void reportContextSensitivity(antlr4::Parser *recognizer,
                                const antlr4::dfa::DFA &dfa, size_t startIndex,
                                size_t stopIndex, size_t prediction,
                                antlr4::atn::ATNConfigSet *configs) override;

  bool HasError() const {
    return (syntax_error_count_ + semantic_error_count_) > 0;
  }

  int syntax_error_count() const { return syntax_error_count_; }
  int semantic_error_count() const { return semantic_error_count_; }
  const std::string &file_name() const { return file_name_; }
  void set_file_name(std::string file_name) {
    file_name_ = std::move(file_name);
  }

 private:
  std::string file_name_;
  int syntax_error_count_ = 0;
  int semantic_error_count_ = 0;
  int semantic_warning_count_ = 0;
};

}  // namespace decoder
}  // namespace sim
}  // namespace mpact

#endif  // MPACT_SIM_DECODER_DECODER_ERROR_LISTENER_H_
