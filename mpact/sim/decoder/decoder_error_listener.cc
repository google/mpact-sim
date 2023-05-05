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

#include "mpact/sim/decoder/decoder_error_listener.h"

#include <exception>
#include <string>

#include "absl/log/log.h"
#include "absl/strings/str_cat.h"

namespace mpact {
namespace sim {
namespace decoder {

// Error listener methods.
void DecoderErrorListener::semanticError(antlr4::Token *token,
                                         absl::string_view msg) {
  if (token != nullptr) {
    size_t line = token->getLine();
    size_t charPositionInLine = token->getCharPositionInLine();
    LOG(ERROR) << absl::StrCat(file_name_, ":", line, ":", charPositionInLine,
                               "  Error: ", msg, "\n");
  } else {
    LOG(ERROR) << absl::StrCat("Error: ", msg, "\n");
  }
  semantic_error_count_++;
}

void DecoderErrorListener::semanticWarning(antlr4::Token *token,
                                           absl::string_view msg) {
  if (token != nullptr) {
    size_t line = token->getLine();
    size_t charPositionInLine = token->getCharPositionInLine();
    LOG(WARNING) << absl::StrCat(file_name_, ":", line, ":", charPositionInLine,
                                 "  Warning: ", msg, "\n");
  } else {
    LOG(WARNING) << absl::StrCat("Warning: ", msg, "\n");
  }
  semantic_warning_count_++;
}

void DecoderErrorListener::syntaxError(antlr4::Recognizer *recognizer,
                                       antlr4::Token *offendingSymbol,
                                       size_t line, size_t charPositionInLine,
                                       const std::string &msg,
                                       std::exception_ptr e) {
  LOG(ERROR) << absl::StrCat(file_name_, ":", line, ":", charPositionInLine,
                             "\n  ", msg, "\n");
  syntax_error_count_++;
}

void DecoderErrorListener::reportAmbiguity(antlr4::Parser *recognizer,
                                           const antlr4::dfa::DFA &dfa,
                                           size_t startIndex, size_t stopIndex,
                                           bool exact,
                                           const antlrcpp::BitSet &ambigAlts,
                                           antlr4::atn::ATNConfigSet *configs) {
  // Empty.
}

void DecoderErrorListener::reportAttemptingFullContext(
    antlr4::Parser *recognizer, const antlr4::dfa::DFA &dfa, size_t startIndex,
    size_t stopIndex, const antlrcpp::BitSet &conflictingAlts,
    antlr4::atn::ATNConfigSet *configs) {
  // Empty.
}

void DecoderErrorListener::reportContextSensitivity(
    antlr4::Parser *recognizer, const antlr4::dfa::DFA &dfa, size_t startIndex,
    size_t stopIndex, size_t prediction, antlr4::atn::ATNConfigSet *configs) {
  // Empty.
}

}  // namespace decoder
}  // namespace sim
}  // namespace mpact
