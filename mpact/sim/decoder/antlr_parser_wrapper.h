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

#ifndef LEARNING_BRAIN_RESEARCH_MPACT_SIM_DECODER_ANTLR_PARSER_WRAPPER_H_
#define LEARNING_BRAIN_RESEARCH_MPACT_SIM_DECODER_ANTLR_PARSER_WRAPPER_H_

#include <istream>
#include <string>

#include "antlr4-runtime/antlr4-runtime.h"

namespace mpact {
namespace sim {
namespace decoder {

// Convenience class to wrap all the Antlr parser components.
template <typename Parser, typename Lexer>
class AntlrParserWrapper {
 public:
  explicit AntlrParserWrapper(std::istream *source_stream) {
    instruction_set_input_.load(*source_stream, /*lenient=*/true);
    lexer_ = new Lexer(&instruction_set_input_);
    tokens_ = new antlr4::CommonTokenStream(lexer_);
    parser_ = new Parser(tokens_);
  }

  explicit AntlrParserWrapper(const std::string &source) {
    instruction_set_input_.load(source, /*lenient=*/true);
    lexer_ = new Lexer(&instruction_set_input_);
    tokens_ = new antlr4::CommonTokenStream(lexer_);
    parser_ = new Parser(tokens_);
  }

  ~AntlrParserWrapper() {
    delete parser_;
    delete tokens_;
    delete lexer_;
  }

  Parser *parser() const { return parser_; }

 private:
  antlr4::ANTLRInputStream instruction_set_input_;
  Lexer *lexer_;
  antlr4::CommonTokenStream *tokens_;
  Parser *parser_;
};

}  // namespace decoder
}  // namespace sim
}  // namespace mpact

#endif  // LEARNING_BRAIN_RESEARCH_MPACT_SIM_DECODER_ANTLR_PARSER_WRAPPER_H_
