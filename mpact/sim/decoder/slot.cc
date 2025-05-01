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

#include "mpact/sim/decoder/slot.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdlib>
#include <map>
#include <stack>
#include <string>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

#include "absl/base/no_destructor.h"
#include "absl/container/btree_set.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "mpact/sim/decoder/format_name.h"
#include "mpact/sim/decoder/instruction.h"
#include "mpact/sim/decoder/instruction_set.h"
#include "mpact/sim/decoder/instruction_set_contexts.h"
#include "mpact/sim/decoder/opcode.h"
#include "mpact/sim/decoder/resource.h"
#include "mpact/sim/decoder/template_expression.h"

namespace mpact {
namespace sim {
namespace machine_description {
namespace instruction_set {

absl::NoDestructor<absl::flat_hash_map<std::string, std::string>>
    Slot::operand_setter_name_map_;
absl::NoDestructor<absl::flat_hash_map<std::string, std::string>>
    Slot::disasm_setter_name_map_;
absl::NoDestructor<absl::flat_hash_map<std::string, std::string>>
    Slot::resource_setter_name_map_;
absl::NoDestructor<absl::flat_hash_map<std::string, std::string>>
    Slot::attribute_setter_name_map_;

// This function translates the location specification into a set of '->'
// references starting with 'inst->' to get to the operand that is implied.
static absl::StatusOr<std::string> TranslateLocator(
    const OperandLocator &locator) {
  std::string code;
  absl::StrAppend(&code, "inst->");
  if (locator.op_spec_number > 0) {
    absl::StrAppend(&code, "child()->");
  }
  for (int i = 1; i < locator.op_spec_number; i++) {
    absl::StrAppend(&code, "next()->");
  }
  if (locator.type == 'p') {
    absl::StrAppend(&code, "Predicate()");
  } else if (locator.type == 's' || locator.type == 't') {
    absl::StrAppend(&code, "Source(", locator.instance, ")");
  } else if (locator.type == 'd' || locator.type == 'e') {
    absl::StrAppend(&code, "Destination(", locator.instance, ")");
  } else {
    return absl::InternalError(absl::StrCat("Unknown locator type '",
                                            std::string(locator.type, 1), "'"));
  }
  return code;
}

// This is a helper function that generates the code snippet to extract the
// right sized value based on the length specifier in the print format
// specification. E.g., %08x, %04d, etc.
static std::string GetExtractor(const std::string &format) {
  int size = 0;
  int pos = 0;
  int len = 1;
  for (int i = 0; i < format.size(); i++) {
    if (!isdigit(format[i])) continue;
    pos = i;
    break;
  }
  while (isdigit(format[pos + len])) len++;
  if (!absl::SimpleAtoi(format.substr(pos, len), &size)) size = 0;
  if (size == 0) return "->AsInt64(0)";
  if (size <= 2) return "->AsInt8(0)";
  if (size <= 4) return "->AsInt16(0)";
  if (size <= 8) return "->AsInt32(0)";
  return "->AsInt64(0)";
}

// Small helper function to just expand the expression specified by the
// FormatInfo from parsing the disassembly specifier.
static std::string ExpandExpression(const FormatInfo &format,
                                    const std::string &locator) {
  // Handle the case when it's just an '@' - i.e., just the address.
  if (format.use_address && format.operation.empty()) {
    return absl::StrCat("(inst->address())");
  }
  if (format.operation.empty()) {
    // No +/- for the @ sign, i.e., @ <</>> amount.
    if (locator.empty()) return absl::StrCat("#error missing field locator");
    if (format.shift_amount == 0) {
      return absl::StrCat(locator, GetExtractor(format.number_format));
    }
    return absl::StrCat("(", locator, GetExtractor(format.number_format),
                        format.do_left_shift ? " << " : " >> ",
                        format.shift_amount, ")");
  }
  // (@ +/- operand) <</>> shift amount
  if (locator.empty()) return absl::StrCat("#error missing field locator");

  return absl::StrCat("(", format.use_address ? "inst->address() " : "0 ",
                      format.operation, "(", locator,
                      GetExtractor(format.number_format),
                      format.shift_amount != 0
                          ? absl::StrCat(format.do_left_shift ? " << " : " >> ",
                                         format.shift_amount, "))")
                          : "))");
}

Slot::Slot(absl::string_view name, InstructionSet *instruction_set,
           bool is_templated, SlotDeclCtx *ctx, unsigned generator_version)
    : instruction_set_(instruction_set),
      ctx_(ctx),
      generator_version_(generator_version),
      is_templated_(is_templated),
      name_(name),
      pascal_name_(ToPascalCase(name)) {}

Slot::~Slot() {
  delete default_latency_;
  default_latency_ = nullptr;
  delete default_instruction_;
  default_instruction_ = nullptr;
  for (auto *param : template_parameters_) {
    delete param;
  }
  template_parameters_.clear();
  for (auto &base : base_slots_) {
    if (base.arguments != nullptr) {
      for (auto *expr : *(base.arguments)) {
        delete expr;
      }
      delete base.arguments;
    }
  }
  base_slots_.clear();
  for (auto &[unused, inst_ptr] : instruction_map_) {
    delete inst_ptr;
  }
  instruction_map_.clear();
  for (auto &[unused, element_ptr] : constant_map_) {
    delete element_ptr;
  }
  constant_map_.clear();
  // The ctx objects stored in resource_spec_map_ are owned by the Antlr4
  // parser, so just clear the map (don't delete those objects).
  resource_spec_map_.clear();
  for (auto &[ignored, expr] : attribute_map_) {
    delete expr;
  }
  attribute_map_.clear();
}

absl::Status Slot::AppendInstruction(Instruction *inst) {
  if (!is_templated()) {
    bool valid = inst->opcode()->ValidateDestLatencies(
        [](int l) -> bool { return l >= 0; });
    if (!valid) {
      return absl::InternalError(absl::StrCat("Invalid latency for opcode '",
                                              inst->opcode()->name(), "'"));
    }
  }
  std::string name = inst->opcode()->name();
  if (!instruction_map_.contains(name)) {
    instruction_map_.emplace(name, inst);
    return absl::OkStatus();
  }
  return absl::InternalError(absl::StrCat(
      "Opcode '", name, "' already added to slot '", this->name(), "'"));
}

absl::Status Slot::AppendInheritedInstruction(Instruction *inst,
                                              TemplateInstantiationArgs *args) {
  std::string name = inst->opcode()->name();
  if (!instruction_map_.contains(name)) {
    auto derived = inst->CreateDerivedInstruction(args);

    if (derived.ok()) {
      if (!is_templated()) {
        bool valid = derived.value()->opcode()->ValidateDestLatencies(
            [](int l) -> bool { return l >= 0; });
        if (!valid) {
          return absl::InternalError(absl::StrCat(
              "Invalid latency for opcode '", inst->opcode()->name(), "'"));
        }
      }
      instruction_map_.emplace(name, derived.value());
      return absl::OkStatus();
    }
    return derived.status();
  }
  return absl::InternalError(
      absl::StrCat("instruction already added: ", inst->opcode()->name()));
}

bool Slot::HasInstruction(const std::string &opcode_name) const {
  return instruction_map_.contains(opcode_name);
}

static std::string indent_string(int n) { return std::string(n, ' '); }

// Generates a string that is a unique key for the attributes to determine which
// instructions can share attribute setter functions.
std::string Slot::CreateAttributeLookupKey(const Instruction *inst) const {
  std::string key;
  for (auto const &[name, expr] : inst->attribute_map()) {
    std::string value;
    auto result = expr->GetValue();
    if (!result.ok()) {
      absl::StrAppend(&key, name, "[e1]:");
      continue;
    }
    auto *value_ptr = std::get_if<int>(&result.value());
    if (value_ptr == nullptr) {
      absl::StrAppend(&key, name, "[e2]:");
      continue;
    }
    absl::StrAppend(&key, name, "[", *value_ptr, "]:");
  }
  return key;
}

// Generate the attribute setter function that matches the "key" of the given
// instruction.
std::string Slot::GenerateAttributeSetterFcn(absl::string_view name,
                                             const Instruction *inst) const {
  std::string output;
  absl::StrAppend(&output, "void ", name, "(Instruction *inst) {\n");
  if (!attribute_map_.empty()) {
    // Allocate the array and initialize to zero.
    absl::StrAppend(&output, "  int *attrs = new int[", attribute_map_.size(),
                    "];\n"
                    "  attrs = {");
    std::string sep = "";
    for (auto const &[name, expr] : inst->attribute_map()) {
      auto result = expr->GetValue();
      if (!result.ok()) {
        absl::StrAppend(&output, "    #error Expression for '", name,
                        "' has no constant value\n");
        continue;
      }
      int *value = std::get_if<int>(&result.value());
      if (value == nullptr) {
        absl::StrAppend(&output, "    #error Expression for '", name,
                        "' does not have type int\n");
        continue;
      }
      absl::StrAppend(&output, sep, "*value");
      sep = ", ";
    }
    absl::StrAppend(&output, "};\n");
    absl::StrAppend(&output,
                    "  inst->SetAttributes(absl::Span<int>(attrs, size));\n");
  }
  absl::StrAppend(&output, "}\n");
  return output;
}

// Return a function call string that will set the attributes for the given
// instruction. If no such appropriate function exists, create one.
std::string Slot::GenerateAttributeSetter(const Instruction *inst) {
  auto key = CreateAttributeLookupKey(inst);
  auto iter = attribute_setter_name_map_->find(key);
  if (iter == attribute_setter_name_map_->end()) {
    auto index = attribute_setter_name_map_->size();
    std::string func_name =
        absl::StrCat(pascal_name(), "Slot", "SetAttributes", index);
    iter = attribute_setter_name_map_->emplace(key, func_name).first;
    absl::StrAppend(&setter_functions_,
                    GenerateAttributeSetterFcn(func_name, inst));
  }
  return iter->second;
}

namespace {

std::string EscapeRegexCharacters(const std::string &str) {
  std::string output;
  if (str.empty()) return output;
  auto pos = str.find_last_not_of(' ');
  if (pos == std::string::npos) {
    return "\\s+";
  }
  std::string input(str.substr(pos));
  bool in_space = false;
  char p = '\0';
  for (auto c : str) {
    if (isspace(c)) {
      if (!in_space) {
        if (ispunct(p)) {
          absl::StrAppend(&output, "\\s*");
        } else {
          absl::StrAppend(&output, "\\s+");
        }
      }
      in_space = true;
      continue;
    }
    p = c;
    in_space = false;
    switch (c) {
      case '.':
        absl::StrAppend(&output, "\\.");
        break;
      case '(':
        absl::StrAppend(&output, "\\(");
        break;
      case ')':
        absl::StrAppend(&output, "\\)");
        break;
      case '[':
        absl::StrAppend(&output, "\\[");
        break;
      case ']':
        absl::StrAppend(&output, "\\]");
        break;
      case '*':
        absl::StrAppend(&output, "\\*");
        break;
      case '+':
        absl::StrAppend(&output, "\\+");
        break;
      case '?':
        absl::StrAppend(&output, "\\?");
        break;
      case '|':
        absl::StrAppend(&output, "\\|");
        break;
      case '{':
        absl::StrAppend(&output, "\\{");
        break;
      case '}':
        absl::StrAppend(&output, "\\}");
        break;
      case '^':
        absl::StrAppend(&output, "\\^");
        break;
      case '$':
        absl::StrAppend(&output, "\\$");
        break;
      case '!':
        absl::StrAppend(&output, "\\!");
        break;
      case '\\':
        absl::StrAppend(&output, "\\\\");
        break;
      default:
        absl::StrAppend(&output, std::string(1, c));
        break;
    }
  }
  return output;
}

}  // namespace

std::tuple<std::string, std::vector<OperandLocator>> Slot::GenerateRegEx(
    const Instruction *inst, std::vector<std::string> &formats) const {
  std::string output = "R\"(";
  std::string sep = "^\\s*";
  std::vector<OperandLocator> opnd_locators;
  // Iterate over the vector of disasm formats. These will end up concatenated
  // with \s+ separators.
  for (auto const *disasm_fmt : inst->disasm_format_vec()) {
    absl::StrAppend(&output, sep);
    sep = "\\s+";
    // The fragments are the text part (not part of operands), that occur
    // between the operand of the format. E.g., the commas in "r1, r2, r3".
    auto fragment_iter = disasm_fmt->format_fragment_vec.begin();
    auto fragment_end = disasm_fmt->format_fragment_vec.end();
    // The formats are the instruction formats, E.g., the register names in
    // "r1, r2, r3".
    auto format_iter = disasm_fmt->format_info_vec.begin();
    auto format_end = disasm_fmt->format_info_vec.end();
    char prev = '\0';
    // Iterate over the format fragments.
    bool optional = false;
    while (fragment_iter != fragment_end) {
      auto fragment = *fragment_iter;
      if (!fragment.empty()) {
        auto str = EscapeRegexCharacters(fragment);
        absl::StrAppend(&output, str);
        prev = str.back();
      } else {
        prev = '\0';
      }
      if (optional) {
        absl::StrAppend(&output, ")?");
      }
      optional = false;
      fragment_iter++;
      if (format_iter != format_end) {
        // If the trailling part of output is not '\\s*', and prev is
        // punctuation, but not '.' or '_', add a space separator.
        auto len = output.size();
        if (output.substr(len - 3) != "\\s*") {
          if ((prev != '\0') &&
              !(isalnum(prev) || (prev == '_') || (prev == '.'))) {
            absl::StrAppend(&output, "\\s*");
          }
        }
        if ((*format_iter)->is_optional) {
          optional = true;
          absl::StrAppend(&output, "(?:");
        }
        std::string op_name = (*format_iter)->op_name;
        absl::StrAppend(&output, "(\\S*?)");
        opnd_locators.push_back(inst->opcode()->op_locator_map().at(op_name));
        if ((fragment_iter != fragment_end) && (!(*fragment_iter).empty())) {
          char c = (*fragment_iter)[0];
          // If the next fragment is not alnum or underscore, add a space
          // separator.
          if (!isalnum(c) || (c != '_')) {
            absl::StrAppend(&output, "\\s*");
          }
        }
        format_iter++;
      }
    }
    if (optional) {
      absl::StrAppend(&output, ")?");
    }
  }
  absl::StrAppend(&output, "\\s*$)\"");
  return {output, opnd_locators};
}

std::string GenerateEncodingFunctions(const std::string &encoder,
                                      InstructionSet instruction_set) {
  std::string output;
  absl::StrAppend(&output, "namespace {\n\n");
  absl::StrAppend(
      &output, "absl::StatusOr<std::tuple<uint64_t, int>> EncodeNone(", encoder,
      "*, SlotEnum, int, OpcodeEnum, uint64_t, const "
      "std::vector<std::string> &) {\n"
      "  return absl::NotFoundError(\"No such opcode\");\n"
      "}\n\n");
  return output;
}
// Generate a regex to match the assembly string for the instructions.
std::tuple<std::string, std::string> Slot::GenerateAsmRegexMatcher() const {
  std::string h_output;
  std::string cc_output;
  std::string class_name = pascal_name() + "SlotMatcher";
  size_t max_args = 0;

  // Generate the encoder function for each instruction.
  std::string encoder =
      absl::StrCat(instruction_set_->pascal_name(), "EncoderInterfaceBase");

  // Generate the matcher class.
  absl::StrAppend(
      &h_output,
      "// Assembly matcher.\n"
      "class ",
      class_name,
      " {\n"
      " public:\n"
      "  ",
      class_name, "(", instruction_set_->pascal_name(),
      "EncoderInterfaceBase *encoder);\n"
      "  ~",
      class_name,
      "();\n"
      "  absl::Status Initialize();\n"
      "absl::StatusOr<std::tuple<uint64_t, int>> "
      "  Encode(uint64_t address, absl::string_view text, int entry, "
      "ResolverInterface *resolver, std::vector<RelocationInfo> "
      "&relocations);\n\n"
      " private:\n"
      "  bool Match(absl::string_view text, std::vector<int> &matches);\n"
      "  bool Extract(absl::string_view text, int index, "
      "std::vector<std::string> &values);\n"
      "  ",
      encoder,
      " *encoder_;\n"
      "  std::vector<RE2 *> regex_vec_;\n"
      "  RE2::Set regex_set_;\n"
      "  absl::flat_hash_map<int, int> index_to_opcode_map_;\n");
  absl::StrAppend(&cc_output, class_name, "::", class_name, "(",
                  instruction_set_->pascal_name(),
                  "EncoderInterfaceBase *encoder) :\n"
                  "  encoder_(encoder),\n"
                  "  regex_set_(RE2::Options(), RE2::ANCHOR_BOTH) {}\n"
                  "\n",
                  class_name, "::~", class_name,
                  "() {\n"
                  "  for (int i = 0; i < re2_args.size(); ++i) {\n"
                  "    delete re2_args[i];\n"
                  "  }\n"
                  "  for (auto *regex : regex_vec_) delete regex;\n"
                  "  regex_vec_.clear();\n"
                  "}\n\n"
                  "absl::Status ",
                  class_name,
                  "::Initialize() {\n"
                  "  std::string error;\n"
                  "  int index = regex_set_.Add(\"^$\", &error);\n"
                  "  if (index == -1) return absl::InternalError(error);\n"
                  "  regex_vec_.push_back(new RE2(\"^$\"));\n");
  std::vector<std::string> formats;
  for (auto const &[name, inst_ptr] : instruction_map_) {
    auto [regex, opnd_locators] = GenerateRegEx(inst_ptr, formats);
    max_args = std::max(max_args, opnd_locators.size());
    std::string opcode_name =
        absl::StrCat("OpcodeEnum::k", ToPascalCase(inst_ptr->opcode()->name()));
    absl::StrAppend(&cc_output, "  regex_vec_.push_back(new RE2(", regex,
                    "));\n"
                    "  index = regex_set_.Add(",
                    regex,
                    ", &error);\n"
                    "  if (index == -1) return absl::InternalError(error);\n"
                    "  index_to_opcode_map_.insert({index, static_cast<int>(",
                    opcode_name, ")", "});\n");
  }
  absl::StrAppend(&h_output, "  std::string args[", max_args,
                  "];\n"
                  "  std::array<RE2::Arg*, ",
                  max_args, "> re2_args = {");
  for (int i = 0; i < max_args; ++i) absl::StrAppend(&h_output, "nullptr, ");
  absl::StrAppend(&h_output, "  };\n");
  // Construct the RE2::Arg objects.
  absl::StrAppend(&cc_output,
                  "  auto ok = regex_set_.Compile();\n"
                  "  if (!ok) return absl::InternalError(\"Failed to compile "
                  "regex set\");\n"
                  "  for (int i = 0; i < ",
                  max_args,
                  "; ++i) {\n"
                  "    re2_args[i] = new RE2::Arg(&args[i]);\n"
                  "  }\n");
  absl::StrAppend(
      &cc_output,
      "  return absl::OkStatus();\n"
      "}\n\n"
      "bool ",
      class_name,
      "::Match(absl::string_view text, std::vector<int> &matches) {\n"
      "  return regex_set_.Match(text, &matches);\n"
      "}\n\n"
      "bool ",
      class_name,
      "::Extract(absl::string_view text, int index, "
      "std::vector<std::string> &values) {\n"
      "  auto &regex = regex_vec_.at(index);\n"
      "  int arg_count = regex->NumberOfCapturingGroups();\n"
      "  if (!regex_vec_.at(index)->FullMatchN(text, *regex, "
      "re2_args.data(), "
      "arg_count))\n"
      "    return false;\n"
      "  for (int i = 0; i < arg_count; ++i) {\n"
      "    values.push_back(args[i]);\n"
      "  }\n"
      "  return true;\n"
      "}\n\n"
      "absl::StatusOr<std::tuple<uint64_t, int>> ",
      pascal_name(),
      "SlotMatcher::Encode(\n"
      R"(
    uint64_t address, absl::string_view text, int entry, ResolverInterface *resolver,
    std::vector<RelocationInfo> &relocations) {
  std::vector<int> matches;
  std::string error_message = absl::StrCat("Failed to encode '", text, "':");
  if (!Match(text, matches) || (matches.size() == 0)) {
    return absl::NotFoundError(error_message);
  }
  std::vector<std::tuple<uint64_t, int>> encodings;
  for (auto index : matches) {
    std::vector<std::string> values;
    if (!Extract(text, index, values)) continue;
    int opcode_index = index_to_opcode_map_.at(index);
)",
      "    auto result = encode_fcns[opcode_index](encoder_, SlotEnum::k",
      pascal_name(),
      ", entry, \n"
      "                                     "
      "static_cast<OpcodeEnum>(opcode_index), address, values, resolver, "
      "relocations);\n",
      R"(
    if (!result.status().ok()) {
      absl::StrAppend(&error_message, "\n    ", result.status().message());
      continue;
    }
    encodings.push_back(result.value());
  }
  if (encodings.empty()) return absl::NotFoundError(error_message);
  if (encodings.size() > 1) {
    return absl::NotFoundError(
        absl::StrCat("Failed to encode '", text, "': ambiguous"));
  }
  return encodings[0];
}

)");
  absl::StrAppend(&h_output, "};\n\n");
  return {h_output, cc_output};
}

// Generate a function that will set the disassembly string for the given
// instruction.
std::string Slot::GenerateDisasmSetterFcn(absl::string_view name,
                                          const Instruction *inst) const {
  std::string output;
  std::string class_name = pascal_name() + "Slot";
  absl::StrAppend(&output, "void ", name, "(Instruction *inst) {\n");
  absl::StrAppend(&output, "  inst->SetDisassemblyString(");
  int indent = 2;
  int outer_paren = false;
  // This is used to keep track of whether the current code emitted is in
  // a call to strcat or not. It helps reduce the number of strcat calls made
  // in the generated code.
  std::stack<bool> in_strcat;
  absl::StrAppend(&output, "absl::StrCat(\n");
  in_strcat.push(true);
  outer_paren = true;
  std::string outer_sep;
  for (auto const *disasm_fmt : inst->disasm_format_vec()) {
    int inner_paren = 0;
    size_t index = 0;
    std::string inner_sep;
    // If the next string needs to be formatted within a certain width field,
    // start out with a StrFormat call.
    if (disasm_fmt->width != 0) {
      absl::StrAppend(&output, outer_sep, indent_string(indent),
                      "absl::StrFormat(\"%", disasm_fmt->width, "s\",\n");
      indent += 2;
      inner_paren++;
      in_strcat.push(false);
    } else if (!outer_sep.empty()) {
      absl::StrAppend(&output, ", ");
    }
    // If multiple strings will be generated, and we're not currently in a
    // StrCat, start a StrCat.
    if (!((disasm_fmt->format_fragment_vec.size() == 1) &&
          disasm_fmt->format_info_vec.empty()) &&
        !in_strcat.top()) {
      absl::StrAppend(&output, indent_string(indent), "absl::StrCat(\n");
      indent += 2;
      inner_paren++;
      in_strcat.push(true);
    }
    // Generate the strings from the format fragments and the format info.
    std::string next_sep;
    for (auto const &frag : disasm_fmt->format_fragment_vec) {
      if (!frag.empty()) {
        absl::StrAppend(&output, inner_sep, indent_string(indent), "\"", frag,
                        "\"");
        next_sep = ", ";
      }
      if (index < disasm_fmt->format_info_vec.size()) {
        auto *format_info = disasm_fmt->format_info_vec[index];
        if (format_info->op_name.empty()) {
          if (!format_info->is_formatted) {
            absl::StrAppend(&output, "\n#error Missing locator information");
          } else {
            absl::StrAppend(
                &output, next_sep, "absl::StrFormat(\"",
                format_info->number_format.back() == 'x' ? "0x" : "",
                format_info->number_format, "\", ",
                ExpandExpression(*format_info, ""), ")");
          }
        } else {
          auto key = format_info->op_name;
          auto iter = inst->opcode()->op_locator_map().find(key);
          if (iter == inst->opcode()->op_locator_map().end()) {
            absl::StrAppend(&output, "\n#error ", key,
                            " not found in instruction opcodes\n");
            continue;
          }
          auto result = TranslateLocator(iter->second);
          if (!result.status().ok()) {
            absl::StrAppend(&output, "\n#error ", result.status().message(),
                            "\n");
            continue;
          }
          if (!format_info->is_formatted) {
            absl::StrAppend(&output, next_sep, result.value(), "->AsString()");
          } else {
            absl::StrAppend(
                &output, next_sep, "absl::StrFormat(\"",
                format_info->number_format.back() == 'x' ? "0x" : "",
                format_info->number_format, "\", ",
                ExpandExpression(*format_info, result.value()), ")");
          }
        }
      }
      next_sep = ", ";
      index++;
      if (inner_sep.empty()) inner_sep = ",\n";
    }
    // Close up parentheses as required.
    for (int i = 0; i < inner_paren; i++) {
      absl::StrAppend(&output, ")");
      indent -= 2;
      if (!in_strcat.top()) {  // Finished a StrFormat.
        absl::StrAppend(&output, "\n", indent_string(indent));
      }
      in_strcat.pop();
    }
    if (outer_sep.empty()) outer_sep = ",\n";
  }
  if (outer_paren) {
    in_strcat.pop();
    absl::StrAppend(&output, ")");
  }

  absl::StrAppend(&output, ");\n}\n\n");
  return output;
}

// Generate a signature for the disassembly setter function required for the
// given instruction. If a matching one does not exist, call to create such a
// function.
std::string Slot::GenerateDisassemblySetter(const Instruction *inst) {
  std::string key;
  // First combine the disassembly fragments.
  for (auto const *format : inst->disasm_format_vec()) {
    for (auto const &frag : format->format_fragment_vec) {
      absl::StrAppend(&key, frag);
    }
  }
  // Then add the resources.
  absl::StrAppend(&key, ":", CreateOperandLookupKey(inst->opcode()));
  std::string func_name = absl::StrCat(
      pascal_name(), "Slot", inst->opcode()->pascal_name(), "SetDisasm");
  auto iter = disasm_setter_name_map_->find(key);
  if (iter == disasm_setter_name_map_->end()) {
    iter = disasm_setter_name_map_->emplace(key, func_name).first;
    absl::StrAppend(&setter_functions_,
                    GenerateDisasmSetterFcn(func_name, inst));
  }
  return absl::StrCat(iter->second);
}

// Generate the assembler function for the given instruction.
std::string Slot::GenerateAssemblerFcn(const Instruction *inst,
                                       absl::string_view encoder_type) const {
  std::string output;
  int num_values = inst->opcode()->source_op_vec().size() +
                   inst->opcode()->dest_op_vec().size();
  absl::StrAppend(
      &output, "absl::StatusOr<std::tuple<int, uint64_t>> ", pascal_name(),
      "Slot", "Assemble", inst->opcode()->pascal_name(), "(", encoder_type,
      " *enc, const std::vector<std::string> &values, SlotEnum "
      "slot, int entry) {\n",
      "  if (values.size() != ", num_values,
      ")\n"
      "    return absl::InvalidArgumentError(\"Wrong number of values\");\n"
      "  constexpr OpcodeEnum opcode = OpcodeEnum::k",
      inst->opcode()->pascal_name(),
      ";\n"
      "auto [inst_word, num_bits] = enc->GetOpEncoding(opcode, slot, "
      "entry);\n",
      "  absl::Status status;\n");
  auto const &source_op_vec = inst->opcode()->source_op_vec();
  for (int i = 0; i < source_op_vec.size(); ++i) {
    std::string op_name = ToPascalCase(source_op_vec[i].name);
    absl::StrAppend(&output, "  status = enc->SetSrcEncoding(values.at(", i,
                    "), slot, entry,\n"
                    "SourceOpEnum::k",
                    op_name, ", ", i,
                    ", opcode);\n"
                    "  if (!stats.ok()) return status;\n");
  }
  auto const &dest_op_vec = inst->opcode()->dest_op_vec();
  for (int i = 0; i < dest_op_vec.size(); ++i) {
    absl::StrAppend(&output, "  status = enc->SetDestEncoding(values.at(", i,
                    "), slot, entry,\n"
                    "DestOpEnum::k",
                    dest_op_vec[i]->pascal_case_name(), ", ", i,
                    ", opcode);\n"
                    "  if (!stats.ok()) return status;\n");
  }
  absl::StrAppend(
      &output,
      "  auto ok = enc->ValidateEncoding(opcode, slot, entry, inst_word);\n"
      "  if (!ok) return absl::InvalidArgumentError(\"Invalid "
      "encoding\");\n");
  absl::StrAppend(&output,
                  "return std::tie(num_bits, inst_word);\n"
                  "}\n\n");
  return output;
}

// Generate a string that is a unique identifier from the resources to
// determine which instructions can share resource setter functions.
std::string Slot::CreateResourceKey(
    const std::vector<const ResourceReference *> &refs) const {
  std::string key;
  std::vector<const ResourceReference *> complex_refs;
  std::vector<const ResourceReference *> simple_refs;
  absl::btree_set<std::string> names;
  // Iterate over use resources.
  for (auto const *ref : refs) {
    if (!ref->resource->is_simple()) {
      complex_refs.push_back(ref);
    } else {
      simple_refs.push_back(ref);
    }
  }
  // Simple use resources.
  for (auto const *ref : simple_refs) {
    std::string name = "S$";
    if (ref->is_array) {
      absl::StrAppend(&name, "[", ref->resource->pascal_name(), "]");
    } else {
      absl::StrAppend(&name, ref->resource->pascal_name());
    }
    names.insert(name);
  }
  std::string sep = "";
  for (auto const &name : names) {
    absl::StrAppend(&key, sep, name);
    sep = "/";
  }
  names.clear();
  absl::StrAppend(&key, ":");

  // Complex use resources.
  for (auto const *ref : complex_refs) {
    std::string name = "C$";
    if (ref->is_array) {
      absl::StrAppend(&name, "[", ref->resource->pascal_name(), "]");
    } else {
      absl::StrAppend(&name, ref->resource->pascal_name());
    }
    auto begin_value = ref->begin_expression->GetValue();
    if (!begin_value.ok()) {
      absl::StrAppend(&name, "(?)");
    } else {
      absl::StrAppend(&name, *std::get_if<int>(&begin_value.value()));
    }
    auto end_value = ref->end_expression->GetValue();
    if (!end_value.ok()) {
      absl::StrAppend(&name, "(?)");
    } else {
      absl::StrAppend(&name, *std::get_if<int>(&end_value.value()));
    }
    names.insert(name);
  }
  sep = "";
  for (auto const &name : names) {
    absl::StrAppend(&key, sep, name);
    sep = "/";
  }
  return key;
}

// Generate a resource setter function call for the resource "key" of the
// given instruction. If a matching one does not exist, call to create such a
// function.
std::string Slot::GenerateResourceSetter(const Instruction *inst,
                                         absl::string_view encoding_type) {
  std::string key = CreateResourceKey(inst->resource_use_vec());
  absl::StrAppend(&key, ":", CreateResourceKey(inst->resource_acquire_vec()));
  auto iter = resource_setter_name_map_->find(key);
  if (iter == resource_setter_name_map_->end()) {
    auto index = resource_setter_name_map_->size();
    std::string func_name =
        absl::StrCat(pascal_name(), "Slot", "SetResources", index);
    iter = resource_setter_name_map_->emplace(key, func_name).first;
    absl::StrAppend(&setter_functions_,
                    GenerateResourceSetterFcn(func_name, inst, encoding_type));
  }
  return iter->second;
}

// Create a resource setter function for the resource "key" of the given
// instruction.
std::string Slot::GenerateResourceSetterFcn(
    absl::string_view name, const Instruction *inst,
    absl::string_view encoding_type) const {
  std::string output;
  absl::StrAppend(&output, "void ", name, "(Instruction *inst, ", encoding_type,
                  " *enc, SlotEnum slot, int entry) {\n");
  std::string opcode_name = inst->opcode()->pascal_name();
  std::string opcode_enum = absl::StrCat("OpcodeEnum::k", opcode_name);
  if (!inst->resource_use_vec().empty() ||
      !inst->resource_acquire_vec().empty()) {
    absl::StrAppend(&output, "  ResourceOperandInterface *res_op;\n");
  }
  std::string optional_inst;
  if (generator_version_ == 2) {
    optional_inst = "inst, ";
  }
  // Get all the simple resources that need to be free, then all the complex
  // resources that need to be free in order to issue the instruction.
  std::vector<const ResourceReference *> complex_refs;
  std::vector<const ResourceReference *> simple_refs;
  for (auto const *ref : inst->resource_use_vec()) {
    // Do the complex refs last.
    if (!ref->resource->is_simple()) {
      complex_refs.push_back(ref);
    } else {
      simple_refs.push_back(ref);
    }
  }
  // Simple resources.
  if (!simple_refs.empty()) {
    // First gather the resource references into a single vector, then request
    // the resource operands for all the resource references in that vector.
    absl::StrAppend(&output, "  std::vector<SimpleResourceEnum> hold_vec = {");
    for (auto const *simple : simple_refs) {
      std::string resource_name;
      if (simple->is_array) {
        resource_name = absl::StrCat("SimpleResourceEnum::k",
                                     simple->resource->pascal_name());
      } else {
        resource_name = absl::StrCat("SimpleResourceEnum::k",
                                     simple->resource->pascal_name());
      }
      absl::StrAppend(&output, "\n      ", resource_name, ",");
    }
    absl::StrAppend(&output,
                    "};\n"
                    "  res_op = enc->GetSimpleResourceOperand(",
                    optional_inst, "slot, entry, ", opcode_enum,
                    ", hold_vec, -1);\n",
                    "  if (res_op != nullptr) {\n"
                    "    inst->AppendResourceHold(res_op);\n"
                    "  }\n");
  }
  // Complex resources.
  for (auto const *complex : complex_refs) {
    // Get the expression values for the begin and end expressions.
    auto begin_value = complex->begin_expression->GetValue();
    auto end_value = complex->end_expression->GetValue();
    if (!begin_value.ok() || !end_value.ok()) {
      absl::StrAppend(&output,
                      "#error Unable to evaluate begin or end expression\n");
      continue;
    }
    // Get the integer values from the begin and end expression values.
    int *begin = std::get_if<int>(&begin_value.value());
    int *end = std::get_if<int>(&end_value.value());
    if ((begin == nullptr) || (end == nullptr)) {
      absl::StrAppend(
          &output, "#error Unable to get value of begin or end expression\n");
      continue;
    }
    if (complex->is_array) {
      absl::StrAppend(
          &output, "  auto res_op_vec = enc->GetComplexResourceOperands(",
          optional_inst, "slot, entry, ", opcode_enum,
          ", ListComplexResourceEnum::k", complex->resource->pascal_name(),
          *begin, ", ", *end, ");\n");
      absl::StrAppend(&output,
                      "  for (auto res_op : res_op_vec) {\n"
                      "    inst->AppendResourceHold(res_op);\n"
                      "  }\n");
    } else {
      absl::StrAppend(&output, "  res_op = enc->GetComplexResourceOperand(",
                      optional_inst, "slot, entry, ", opcode_enum,
                      ", ComplexResourceEnum::k",
                      complex->resource->pascal_name(), ", ");
      absl::StrAppend(&output, *begin, ", ", *end, ");\n");
      absl::StrAppend(&output,
                      "  if (res_op != nullptr) {\n"
                      "    inst->AppendResourceHold(res_op);\n"
                      "  }\n");
    }
  }

  // Get all the simple resources that need to be reserved, then all the
  // complex resources that need to be reserved when issuing this instruction.
  complex_refs.clear();
  simple_refs.clear();
  for (auto const *ref : inst->resource_acquire_vec()) {
    // Do the complex refs last.
    if (!ref->resource->is_simple()) {
      complex_refs.push_back(ref);
    } else {
      simple_refs.push_back(ref);
    }
  }
  // Simple resources.
  if (!simple_refs.empty()) {
    // Compute the set of latencies. Insert each reference into a multi-map
    // keyed by the latency.
    std::multimap<int, const ResourceReference *> latency_map;
    absl::flat_hash_set<int> latencies;
    for (auto const *simple : simple_refs) {
      if (simple->end_expression == nullptr) {
        continue;
      }
      auto end_value = simple->end_expression->GetValue();
      if (!end_value.ok()) {
        absl::StrAppend(&output, "#error Unable to evaluate end expression\n");
        continue;
      }
      int *end = std::get_if<int>(&end_value.value());
      if (end == nullptr) {
        absl::StrAppend(&output,
                        "#error Unable to get value of  end expression\n");
        continue;
      }
      int latency = *end;
      latencies.insert(latency);
      latency_map.insert(std::make_pair(latency, simple));
    }
    // Process the resources by latencies.
    for (auto latency : latencies) {
      std::string sep = "";
      absl::StrAppend(&output, "  std::vector<SimpleResourceEnum> acquire_vec",
                      latency, " = {");
      for (auto iter = latency_map.lower_bound(latency);
           iter != latency_map.upper_bound(latency); ++iter) {
        auto *simple = iter->second;
        std::string resource_name;
        if (simple->is_array) {
          resource_name = absl::StrCat("SimpleResourceEnum::k",
                                       simple->resource->pascal_name());
        } else {
          resource_name = absl::StrCat("SimpleResourceEnum::k",
                                       simple->resource->pascal_name());
        }
        absl::StrAppend(&output, sep, "\n      ", resource_name, ",");
      }
      absl::StrAppend(&output,
                      "};\n\n"
                      "  res_op = enc->GetSimpleResourceOperand(",
                      optional_inst, "slot, entry, ", opcode_enum,
                      ", acquire_vec", latency, ", ", latency,
                      ");\n"
                      "  if (res_op != nullptr) {\n"
                      "    inst->AppendResourceAcquire(res_op);\n"
                      "  }\n");
    }
  }

  // Complex resources.
  if (!complex_refs.empty()) {
    for (auto const *complex : complex_refs) {
      // Get the expression values for the begin and end expressions.
      if (complex->begin_expression == nullptr) continue;
      if (complex->end_expression == nullptr) continue;
      auto begin_value = complex->begin_expression->GetValue();
      auto end_value = complex->end_expression->GetValue();
      if (!begin_value.ok() || !end_value.ok()) {
        absl::StrAppend(&output,
                        "#error Unable to evaluate begin or end expression\n");
        continue;
      }
      // Get the integer values from the begin and end expression values.
      int *begin = std::get_if<int>(&begin_value.value());
      int *end = std::get_if<int>(&end_value.value());
      if (complex->is_array) {
        absl::StrAppend(&output,
                        "  auto res_op_vec = "
                        "enc->GetComplexResourceOperands(",
                        optional_inst, "slot, entry, ", opcode_enum,
                        ", ListComplexResourceEnum::k",
                        complex->resource->pascal_name(), *begin, ", ", *end,
                        ");\n");
        absl::StrAppend(&output,
                        "  for (auto res_op : res_op_vec) {\n"
                        "    inst->AppendResourceHold(res_op);\n"
                        "  }\n");
      } else {
        absl::StrAppend(&output, "    res_op = enc->GetComplexResourceOperand(",
                        optional_inst, "slot, entry, ", opcode_enum,
                        ", ComplexResourceEnum::k",
                        complex->resource->pascal_name(), ", ");
        absl::StrAppend(&output, *begin, ", ", *end, ");\n");
        absl::StrAppend(&output,
                        "  if (res_op != nullptr) {\n"
                        "    inst->AppendResourceHold(res_op);\n"
                        "  }\n");
      }
    }
  }
  absl::StrAppend(&output, "}\n\n");
  return output;
}

// Generates a string that is a unique identifier from the operands to
// determine which instructions can share operand getter functions.
std::string Slot::CreateOperandLookupKey(const Opcode *opcode) const {
  std::string key;
  // Generate identifier for the predicate operand, if the opcode has one.
  const std::string &op_name = opcode->predicate_op_name();
  if (!op_name.empty()) {
    absl::StrAppend(&key, op_name, ":");
  }
  // Generate key for the source operands.
  std::string sep = "";
  for (const auto &src_op : opcode->source_op_vec()) {
    if (src_op.is_array) {
      absl::StrAppend(&key, sep, "[", src_op.name, "]");
    } else {
      absl::StrAppend(&key, sep, src_op.name);
    }
    sep = "/";
  }
  absl::StrAppend(&key, ":");
  // Append identifier for destination operands.
  sep.clear();
  for (auto const *dst_op : opcode->dest_op_vec()) {
    std::string dest_op_enum;
    if (dst_op->is_array()) {
      absl::StrAppend(&key, sep, "[", dst_op->name(), "]");
    } else {
      absl::StrAppend(&key, sep, dst_op->name());
    }
    std::string latency;
    if (dst_op->expression() == nullptr) {
      absl::StrAppend(&latency, "(*)");
    } else {
      auto result = dst_op->GetLatency();
      if (!result.ok()) {
        absl::StrAppend(&latency, "(-1)");
        continue;
      }
      if (dst_op->is_array()) {
        absl::StrAppend(&latency, "({", result.value(), "})");
      } else {
        absl::StrAppend(&latency, "(", result.value(), ")");
      }
    }
    if (dst_op->is_array()) {
      absl::StrAppend(&key, sep, "[", dst_op->name(), "]", latency);
    } else {
      absl::StrAppend(&key, sep, dst_op->name(), latency);
    }
    sep = "/";
  }
  return key;
}

// Generates a function that sets the operands for the operand "key" of the
// given opcode.
std::string Slot::GenerateOperandSetterFcn(absl::string_view getter_name,
                                           absl::string_view encoding_type,
                                           const Opcode *opcode) const {
  std::string output;
  std::string optional_inst;
  if (generator_version_ == 2) {
    optional_inst = "inst, ";
  }
  absl::StrAppend(&output, "void ", getter_name, "(Instruction *inst, ",
                  encoding_type,
                  " *enc, OpcodeEnum opcode, SlotEnum slot, int entry) {\n");
  // Generate code to set predicate operand, if the opcode has one.
  const std::string &op_name = opcode->predicate_op_name();
  if (!op_name.empty()) {
    std::string pred_op_enum =
        absl::StrCat("PredOpEnum::k", ToPascalCase(op_name));
    absl::StrAppend(&output, "  inst->SetPredicate(enc->GetPredicate", "(",
                    optional_inst, "slot, entry, opcode, ", pred_op_enum,
                    "));\n");
  }
  // Generate code to set the instruction's source operands.
  int source_no = 0;
  for (const auto &src_op : opcode->source_op_vec()) {
    // If the source operand is an array, then we need to iterate over the
    // vector of operands that GetSources returns.
    if (src_op.is_array) {
      std::string src_op_enum =
          absl::StrCat("ListSourceOpEnum::k", ToPascalCase(src_op.name));
      absl::StrAppend(&output,
                      "  {\n"
                      "    auto vec = enc->GetSources",
                      "(", optional_inst, "slot, entry, opcode, ", src_op_enum,
                      ", ", source_no,
                      ");\n"
                      "    for (auto *op : vec) inst->AppendSource(op);\n"
                      "  }\n");
    } else {
      std::string src_op_enum =
          absl::StrCat("SourceOpEnum::k", ToPascalCase(src_op.name));
      absl::StrAppend(&output, "  inst->AppendSource(enc->GetSource", "(",
                      optional_inst, "slot, entry, opcode, ", src_op_enum, ", ",
                      source_no++, "));\n");
    }
  }
  // Generate code to set the instruction's destination operands.
  int dest_no = 0;
  for (auto const *dst_op : opcode->dest_op_vec()) {
    std::string dest_op_enum;
    if (dst_op->is_array()) {
      dest_op_enum =
          absl::StrCat("ListDestOpEnum::k", dst_op->pascal_case_name());
    } else {
      dest_op_enum = absl::StrCat("DestOpEnum::k", dst_op->pascal_case_name());
    }
    std::string latency;
    if (dst_op->expression() == nullptr) {
      latency = absl::StrCat("enc->GetLatency(", optional_inst,
                             "slot, entry, opcode, ", dest_op_enum, ", ",
                             dest_no, ")");
    } else {
      auto result = dst_op->GetLatency();
      if (!result.ok()) {
        absl::StrAppend(&output, "#error \"Failed to get latency for operand '",
                        dst_op->name(), "'\"");
        dest_no++;
        continue;
      }
      latency = absl::StrCat(result.value());
      // If the operand is an array, then the latency is a vector.
      if (dst_op->is_array()) {
        latency = absl::StrCat("{", latency, "}");
      }
    }
    if (dst_op->is_array()) {
      absl::StrAppend(&output,
                      "  {\n"
                      "    auto vec = enc->GetDestinations",
                      "(", optional_inst, "slot, entry, opcode, ", dest_op_enum,
                      ", ", dest_no, ", ", latency,
                      ");\n"
                      "    for (auto *op : vec) inst->AppendDestination(op);\n"
                      "  }");
    } else {
      absl::StrAppend(&output, "  inst->AppendDestination(enc->GetDestination(",
                      optional_inst, "slot, entry, opcode, ", dest_op_enum,
                      ", ", dest_no, ", ", latency, "));\n");
      dest_no++;
    }
    dest_no++;
  }
  absl::StrAppend(&output, "}\n\n");
  return output;
}

std::string Slot::ListFuncGetterInitializations(
    absl::string_view encoding_type) {
  std::string output;
  if (instruction_map_.empty()) return output;
  std::string class_name = pascal_name() + "Slot";
  // For each instruction create two lambda functions. One that is used to
  // obtain the semantic function object for the instruction, the other a
  // lambda that sets the predicate, source and target operands. Both lambdas
  // use calls to virtual functions declared in the current class or a base
  // class thereof.
  std::string signature =
      absl::StrCat("(Instruction *inst, ", encoding_type,
                   " *enc, OpcodeEnum opcode, SlotEnum slot, int entry)");
  absl::StrAppend(&setter_functions_,
                  GenerateOperandSetterFcn(
                      absl::StrCat(pascal_name(), "SlotSetOperandsNull"),
                      encoding_type, default_instruction_->opcode()));
  absl::StrAppend(
      &output, "    {static_cast<int>(OpcodeEnum::kNone), {OperandSetter{",
      pascal_name(),
      "SlotSetOperandsNull},\n"
      "    ",
      GenerateDisassemblySetter(default_instruction_), ",\n", "    ",
      GenerateResourceSetter(default_instruction_, encoding_type), ",\n",
      "    ", GenerateAttributeSetter(default_instruction_), ",\n",
      "    SemFuncSetter{", default_instruction_->semfunc_code_string(), "}, ",
      default_instruction_->opcode()->instruction_size(), "}},\n");
  for (auto const &[unused, inst_ptr] : instruction_map_) {
    auto *instruction = inst_ptr;
    std::string opcode_name = instruction->opcode()->pascal_name();
    std::string opcode_enum = absl::StrCat("OpcodeEnum::k", opcode_name);
    absl::StrAppend(&output, "\n  // ***   k", opcode_name, "   ***\n");
    // For the opcode and any child opcodes, add the semantic function and
    // operand_setter_ lambda.
    std::string code_str;
    std::string sep = "";
    std::string operands_str;
    for (auto const *inst = instruction; inst != nullptr;
         inst = inst->child()) {
      // Construct operand getter lookup key.
      std::string key = CreateOperandLookupKey(inst->opcode());
      auto iter = operand_setter_name_map_->find(key);
      // If the key is not found, create a new getter function, otherwise
      // reuse the existing one.
      if (iter == operand_setter_name_map_->end()) {
        auto index = operand_setter_name_map_->size();
        std::string setter_name =
            absl::StrCat(class_name, "SetOperands", index);
        absl::StrAppend(&setter_functions_,
                        GenerateOperandSetterFcn(setter_name, encoding_type,
                                                 inst->opcode()));
        iter =
            operand_setter_name_map_->insert(std::make_pair(key, setter_name))
                .first;
      }
      absl::StrAppend(&operands_str, sep, iter->second);
      if (inst->semfunc_code_string().empty()) {
        // If there is no code string, use the default one.
        absl::StrAppend(&code_str, sep,
                        default_instruction_->semfunc_code_string());
      } else {
        absl::StrAppend(&code_str, sep, inst->semfunc_code_string());
      }
      sep = ", ";
    }
    absl::StrAppend(&output, "    {static_cast<int>(OpcodeEnum::k", opcode_name,
                    "), {OperandSetter{", operands_str, "},\n", "    ",
                    GenerateDisassemblySetter(instruction), ",\n", "    ",
                    GenerateResourceSetter(instruction, encoding_type), ",\n",
                    "    ", GenerateAttributeSetter(instruction), ",\n",
                    "    SemFuncSetter{", code_str, "}, ",
                    instruction->opcode()->instruction_size(), "}},\n");
  }
  return output;
}

// Generate the class declaration, including all getter functions for semantic
// functions and operand initializers.
std::string Slot::GenerateClassDeclaration(
    absl::string_view encoding_type) const {
  std::string output;
  if (!is_referenced()) return output;
  std::string class_name = pascal_name() + "Slot";
  absl::StrAppend(&output, "class ", class_name,
                  " {\n"
                  " public:\n"
                  "  explicit ",
                  class_name, "(ArchState *arch_state);\n");
  // Emit Decode function generated that decodes the slot and creates and
  // initializes an instruction object, as well as private data members.
  absl::StrAppend(
      &output, "  Instruction *Decode(uint64_t address, ", encoding_type,
      "* isa_encoding, SlotEnum, int entry);\n",
      "\n"
      " private:\n"
      "  ArchState *arch_state_;\n"
      "  absl::flat_hash_map<int, InstructionInfo> instruction_info_;\n",
      //"  std::array<InstructionInfo, ",
      // instruction_map_.size() + 1, "> instruction_info_", ";\n",
      "  static constexpr SlotEnum slot_ = SlotEnum::k", pascal_name(),
      ";\n"
      "};\n"
      "\n");
  return output;
}

// Write out the class definition for the Slot class including all initializer
// bodies.
std::string Slot::GenerateClassDefinition(absl::string_view encoding_type) {
  if (!is_referenced()) return "";
  std::string class_name = pascal_name() + "Slot";
  std::string output;
  // Constructor.
  absl::StrAppend(
      &output, class_name, "::", class_name,
      "(ArchState *arch_state) :\n"
      "  arch_state_(arch_state),\n",
      "  instruction_info_{{\n", ListFuncGetterInitializations(encoding_type),
      "}} {}\n",
      "\n"
      "Instruction *",
      class_name, "::Decode(uint64_t address, ", encoding_type,
      " *isa_encoding, SlotEnum slot, int entry) {\n"
      "  OpcodeEnum opcode = isa_encoding->GetOpcode(slot, entry);\n"
      "  int indx = static_cast<int>(opcode);\n"
      "  auto &inst_info = instruction_info_[indx];\n"
      "  Instruction *inst = new Instruction(address, arch_state_);\n"
      "  inst->set_size(inst_info.instruction_size);\n"
      "  inst->set_opcode(static_cast<int>(opcode));\n"
      "  inst->set_semantic_function(inst_info.semfunc[0]);\n"
      "  inst_info.operand_setter[0](inst, isa_encoding, opcode, slot, "
      "entry);\n"
      "  Instruction *parent = inst;\n"
      "  for (size_t i = 1; i < inst_info.operand_setter.size(); i++) {\n"
      "    Instruction *child = new Instruction(address, arch_state_);\n"
      "    child->set_semantic_function(inst_info.semfunc[i]);\n"
      "    inst_info.operand_setter[i](child, isa_encoding, opcode, slot, "
      "entry);\n"
      "    parent->AppendChild(child);\n"
      "    child->DecRef();\n"
      "    parent = child;\n"
      "  }\n"
      "  inst_info.resource_setter(inst, isa_encoding, slot, entry);\n"
      "  inst_info.disassembly_setter(inst);\n"
      "  inst_info.attribute_setter(inst);\n"
      "  return inst;\n"
      "}\n");
  // Prepend the setter functions.
  std::string combined_output = absl::StrCat(
      "namespace {\n\n", setter_functions_, "}  // namespace\n\n", output);
  return combined_output;
}

absl::Status Slot::CheckPredecessors(const Slot *base) const {
  if (predecessor_set_.contains(base))
    return absl::AlreadyExistsError(
        absl::StrCat("'", base->name(),
                     "' is already in the predecessor set of '", name(), "'"));
  for (auto const *pred : predecessor_set_) {
    auto status = pred->CheckPredecessors(base);
    if (!status.ok()) return status;
  }
  for (auto const *base_pred : base->predecessor_set_) {
    auto status = CheckPredecessors(base_pred);
    if (!status.ok()) return status;
  }
  return absl::OkStatus();
}

absl::Status Slot::AddBase(const Slot *base) {
  // First need to check if the current slot already inherits from base, or
  // any of base's predecessors. Only tree-like inheritance is supported.
  auto status = CheckPredecessors(base);
  if (!status.ok()) return status;
  predecessor_set_.insert(base);
  base_slots_.emplace_back(base);
  return absl::OkStatus();
}

absl::Status Slot::AddBase(const Slot *base,
                           TemplateInstantiationArgs *arguments) {
  auto status = CheckPredecessors(base);
  if (!status.ok()) return status;
  predecessor_set_.insert(base);
  base_slots_.emplace_back(base, arguments);
  return absl::OkStatus();
}

absl::Status Slot::AddConstant(const std::string &ident,
                               const std::string &type,
                               TemplateExpression *expression) {
  // Ignore the type for now - there is only int.
  (void)type;
  // Check if the name already exists or matches a template formal parameter.
  if (template_parameter_map_.contains(ident)) {
    return absl::AlreadyExistsError(
        absl::StrCat("Slot constant '", ident,
                     "' conflicts with template formal with same name"));
  }
  if (constant_map_.contains(ident)) {
    return absl::AlreadyExistsError(
        absl::StrCat("Redefinition of slot constant '", ident, "'"));
  }
  constant_map_.insert(std::make_pair(ident, expression));
  return absl::OkStatus();
}

TemplateExpression *Slot::GetConstExpression(const std::string &ident) const {
  auto iter = constant_map_.find(ident);
  if (iter == constant_map_.end()) return nullptr;
  return iter->second;
}

absl::Status Slot::AddTemplateFormal(const std::string &par_name) {
  if (template_parameter_map_.contains(par_name)) {
    // Push it into the vector, but not the map. Have the formal name refer to
    // the previous index. Signal error. This allows us to properly match the
    // number of parameters in each use of the templated slot, even though
    // there is an error in the parameter names.
    int indx = template_parameter_map_.at(par_name);
    template_parameters_.push_back(new TemplateFormal(par_name, indx));
    return absl::InternalError(
        absl::StrCat("Duplicate parameter name '", par_name, "'"));
  }
  int indx = template_parameters_.size();
  template_parameters_.push_back(new TemplateFormal(par_name, indx));
  template_parameter_map_.insert(std::make_pair(par_name, indx));
  return absl::OkStatus();
}

TemplateFormal *Slot::GetTemplateFormal(const std::string &name) const {
  auto iter = template_parameter_map_.find(name);
  if (iter == template_parameter_map_.end()) return nullptr;
  return template_parameters_[iter->second];
}

void Slot::AddInstructionAttribute(const std::string &name,
                                   TemplateExpression *expr) {
  attribute_map_.emplace(name, expr);
}

}  // namespace instruction_set
}  // namespace machine_description
}  // namespace sim
}  // namespace mpact
