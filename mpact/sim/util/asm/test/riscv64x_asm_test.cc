// Copyright 2025 Google LLC
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

#include <cstddef>
#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

#include "absl/base/no_destructor.h"
#include "absl/container/flat_hash_map.h"
#include "absl/log/check.h"
#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "elfio/elf_types.hpp"
#include "elfio/elfio.hpp"
#include "elfio/elfio_strings.hpp"
#include "elfio/elfio_symbols.hpp"
#include "googlemock/include/gmock/gmock.h"  // IWYU pragma: keep
#include "googletest/include/gtest/gtest.h"
#include "mpact/sim/util/asm/opcode_assembler_interface.h"
#include "mpact/sim/util/asm/resolver_interface.h"
#include "mpact/sim/util/asm/simple_assembler.h"
#include "mpact/sim/util/asm/test/riscv64x_bin_encoder_interface.h"
#include "mpact/sim/util/asm/test/riscv64x_encoder.h"
#include "util/regexp/re2/re2.h"

// This file contains tests for the simple assembler using a very reduced
// subset of the RISC-V ISA.

namespace {

using ::mpact::sim::riscv::isa64::RiscV64XBinEncoderInterface;
using ::mpact::sim::riscv::isa64::Riscv64xSlotMatcher;
using ::mpact::sim::util::assembler::OpcodeAssemblerInterface;
using ::mpact::sim::util::assembler::RelocationInfo;
using ::mpact::sim::util::assembler::ResolverInterface;
using ::mpact::sim::util::assembler::SimpleAssembler;

// This class implements the OpcodeAssemblerInterface using the slot matcher.
class RiscV64XAssembler : public OpcodeAssemblerInterface {
 public:
  RiscV64XAssembler(Riscv64xSlotMatcher* matcher)
      : label_re_("^(\\S+)\\s*:"), matcher_(matcher) {};
  ~RiscV64XAssembler() override = default;
  absl::StatusOr<size_t> Encode(
      uint64_t address, absl::string_view text,
      AddSymbolCallback add_symbol_callback, ResolverInterface* resolver,
      std::vector<uint8_t>& bytes,
      std::vector<RelocationInfo>& relocations) override {
    // First check to see if there is a label, if so, add it to the symbol table
    // with the current address.
    std::string label;
    if (RE2::Consume(&text, label_re_, &label)) {
      auto status =
          add_symbol_callback(label, address, 0, ELFIO::STT_NOTYPE, 0, 0);
      if (!status.ok()) return status;
    }
    // Call the slot matcher to get the encoded value.
    auto res = matcher_->Encode(address, text, 0, resolver, relocations);
    if (!res.status().ok()) return res.status();
    // Convert the value to a byte array.
    auto [value, size] = res.value();
    union {
      uint64_t i;
      uint8_t b[sizeof(uint64_t)];
    } u;
    u.i = value;
    for (int i = 0; i < size / 8; ++i) {
      bytes.push_back(u.b[i]);
    }
    return bytes.size();
  }

 private:
  RE2 label_re_;
  Riscv64xSlotMatcher* matcher_;
};

// Sample assembly code.
absl::NoDestructor<std::string> kTestAssembly(R"(
; text section
    .text
    .global main
main:
    addi a0, zero, 5
    lui a1, %hi(semihost_param)
    addi a1, a1, %lo(semihost_param)
    addi t0, zero, 2
    sd t0, 0(a1)
    lui t2, %hi(hello)
    addi t2, t2, %lo(hello)
    sd t2, 8(a1)
    addi t0, zero, 12
    sd t0, 0x10(a1)
    jal ra, semihost
    ; now exit
    addi a0, zero, 24
    lui t0, 0x20026
    addi t0, t0, 0x20026
    sd t0, 0(a1)
    jal ra, semihost
exit:
    j exit

semihost:
    slli zero, zero, 0x1f
    ebreak
    srai zero, zero, 7
    jr ra, 0

; data section

    .data
    .global hello
hello:
    .cstring "Hello World\n"
    .char '\n'

; bss

    .bss
    .global tohost
tohost:
    .space 16
semihost_param:
    .space 16
)");

// Test fixture. It creates the assembler and parses the assembly code.
class RiscV64XAssemblerTest : public ::testing::Test {
 protected:
  RiscV64XAssemblerTest()
      : matcher_(&bin_encoder_interface_), riscv_64x_assembler_(&matcher_) {
    CHECK_OK(matcher_.Initialize());
    // Create the assembler.
    assembler_ =
        new SimpleAssembler(";", ELFIO::ELFCLASS64, &riscv_64x_assembler_);
    assembler_->writer().set_os_abi(ELFIO::ELFOSABI_LINUX);
    assembler_->writer().set_machine(ELFIO::EM_RISCV);
    std::istringstream source(*kTestAssembly);
    // Parse the assembly code.
    auto status = assembler_->Parse(source);
    CHECK_OK(status) << status.message();
  }

  ~RiscV64XAssemblerTest() override { delete assembler_; }

  // Access the ELF writer.
  ELFIO::elfio& elf() { return assembler_->writer(); }
  SimpleAssembler* assembler() const { return assembler_; }

 private:
  RiscV64XBinEncoderInterface bin_encoder_interface_;
  Riscv64xSlotMatcher matcher_;
  RiscV64XAssembler riscv_64x_assembler_;
  SimpleAssembler* assembler_;
};

// Test that the expected sections are present.
TEST_F(RiscV64XAssemblerTest, Sections) {
  auto sections = elf().sections;
  // Null section and the 6 sections listed below.
  EXPECT_EQ(sections.size(), 7);
  EXPECT_NE(sections[".text"], nullptr);
  EXPECT_NE(sections[".data"], nullptr);
  EXPECT_NE(sections[".bss"], nullptr);
  EXPECT_NE(sections[".shstrtab"], nullptr);
  EXPECT_NE(sections[".strtab"], nullptr);
  EXPECT_NE(sections[".symtab"], nullptr);
}

// Verify that the information about the text section is as expected.
TEST_F(RiscV64XAssemblerTest, Text) {
  auto status = assembler()->CreateExecutable(0x1000, "main");
  CHECK_OK(status) << status.message();
  auto* text = elf().sections[".text"];
  EXPECT_EQ(text->get_type(), ELFIO::SHT_PROGBITS);
  EXPECT_EQ(text->get_flags(), ELFIO::SHF_ALLOC | ELFIO::SHF_EXECINSTR);
  EXPECT_EQ(text->get_link(), ELFIO::SHN_UNDEF);
  EXPECT_EQ(text->get_size(), /*num inst*/ 21 * /*bytes per inst*/ 4);
}

TEST_F(RiscV64XAssemblerTest, Data) {
  auto status = assembler()->CreateExecutable(0x1000, "main");
  CHECK_OK(status) << status.message();
  auto* data = elf().sections[".data"];
  EXPECT_EQ(data->get_type(), ELFIO::SHT_PROGBITS);
  EXPECT_EQ(data->get_flags(), ELFIO::SHF_ALLOC | ELFIO::SHF_WRITE);
  EXPECT_EQ(data->get_link(), ELFIO::SHN_UNDEF);
  // Hello world is 12 bytes, plus the null terminator.
  // Add one .char declaration.
  EXPECT_EQ(data->get_size(), 14);
}

TEST_F(RiscV64XAssemblerTest, Bss) {
  auto status = assembler()->CreateExecutable(0x1000, "main");
  CHECK_OK(status) << status.message();
  auto* bss = elf().sections[".bss"];
  EXPECT_EQ(bss->get_type(), ELFIO::SHT_NOBITS);
  EXPECT_EQ(bss->get_flags(), ELFIO::SHF_ALLOC | ELFIO::SHF_WRITE);
  EXPECT_EQ(bss->get_link(), ELFIO::SHN_UNDEF);
  // Two .space declarations, each 16 bytes.
  EXPECT_EQ(bss->get_size(), 32);
}

TEST_F(RiscV64XAssemblerTest, RelocatableSymbols) {
  auto status = assembler()->CreateRelocatable();
  CHECK_OK(status) << status.message();
  auto* symtab = elf().sections[".symtab"];
  ELFIO::symbol_section_accessor symbols(elf(), symtab);
  ELFIO::Elf64_Addr value;
  ELFIO::Elf_Xword size;
  unsigned char bind;
  unsigned char type;
  ELFIO::Elf_Half section_index;
  unsigned char other;
  int num_symbols = symtab->get_size() / sizeof(ELFIO::Elf64_Sym);
  auto symspan = absl::MakeSpan(
      reinterpret_cast<const ELFIO::Elf64_Sym*>(symtab->get_data()),
      num_symbols);
  absl::flat_hash_map<std::string, int> symbol_map;
  auto* string_accessor =
      new ELFIO::string_section_accessor(elf().sections[".strtab"]);
  for (int i = 0; i < num_symbols; ++i) {
    auto name = string_accessor->get_string(symspan[i].st_name);
    symbol_map.insert({name, i});
  }
  // Verify that main is valued 0x0, global and located in the text section.
  symbols.get_symbol("main", value, size, bind, type, section_index, other);
  auto* sym = &symspan[symbol_map["main"]];
  EXPECT_EQ(sym->st_value, 0x0);
  EXPECT_EQ(ELF_ST_BIND(sym->st_info), ELFIO::STB_GLOBAL);
  EXPECT_EQ(sym->st_shndx, elf().sections[".text"]->get_index());
  EXPECT_EQ(ELF_ST_TYPE(sym->st_info), ELFIO::STT_NOTYPE);
  // Verify that exit is valued 16 * 4, local and located in the text
  // section.
  sym = &symspan[symbol_map["exit"]];
  EXPECT_EQ(sym->st_value, 16 * 4);
  EXPECT_EQ(ELF_ST_BIND(sym->st_info), ELFIO::STB_LOCAL);
  EXPECT_EQ(sym->st_shndx, elf().sections[".text"]->get_index());
  EXPECT_EQ(ELF_ST_TYPE(sym->st_info), ELFIO::STT_NOTYPE);
  // Verify that hello is global and located in the data section at 0x2000.
  symbols.get_symbol("hello", value, size, bind, type, section_index, other);
  sym = &symspan[symbol_map["hello"]];
  EXPECT_EQ(sym->st_value, 0);
  EXPECT_EQ(sym->st_shndx, elf().sections[".data"]->get_index());
  EXPECT_EQ(ELF_ST_BIND(sym->st_info), ELFIO::STB_GLOBAL);
  EXPECT_EQ(ELF_ST_TYPE(sym->st_info), ELFIO::STT_NOTYPE);
  // Verify that semihost_param is global and located in the bss section at
  // 16 bytes.
  sym = &symspan[symbol_map["semihost_param"]];
  EXPECT_EQ(sym->st_value, 16);
  EXPECT_EQ(sym->st_shndx, elf().sections[".bss"]->get_index());
  EXPECT_EQ(ELF_ST_BIND(sym->st_info), ELFIO::STB_LOCAL);
  EXPECT_EQ(ELF_ST_TYPE(sym->st_info), ELFIO::STT_NOTYPE);
  delete string_accessor;
}

TEST_F(RiscV64XAssemblerTest, ExecutableSymbols) {
  auto status = assembler()->CreateExecutable(0x1000, "main");
  CHECK_OK(status) << status.message();
  auto* symtab = elf().sections[".symtab"];
  ELFIO::symbol_section_accessor symbols(elf(), symtab);
  ELFIO::Elf64_Addr value;
  ELFIO::Elf_Xword size;
  unsigned char bind;
  unsigned char type;
  ELFIO::Elf_Half section_index;
  unsigned char other;
  // Verify that main is valued 0x1000, global and located in the text section.
  symbols.get_symbol("main", value, size, bind, type, section_index, other);
  EXPECT_EQ(value, 0x1000);
  EXPECT_EQ(section_index, elf().sections[".text"]->get_index());
  EXPECT_EQ(type, ELFIO::STT_NOTYPE);
  // Verify that exit is valued 0x1000 + 16 * 4, local and located in the text
  // section.
  symbols.get_symbol("exit", value, size, bind, type, section_index, other);
  EXPECT_EQ(value, 0x1000 + 16 * 4);
  EXPECT_EQ(bind, ELFIO::STB_LOCAL);
  EXPECT_EQ(section_index, elf().sections[".text"]->get_index());
  EXPECT_EQ(type, ELFIO::STT_NOTYPE);
  // Verify that hello is global and located in the data section at 0x2000.
  symbols.get_symbol("hello", value, size, bind, type, section_index, other);
  EXPECT_EQ(value, 0x2000);
  EXPECT_EQ(section_index, elf().sections[".data"]->get_index());
  EXPECT_EQ(bind, ELFIO::STB_GLOBAL);
  EXPECT_EQ(type, ELFIO::STT_NOTYPE);
  // Verify that semihost_param is global and located in the bss section at
  // 0x2000 + 14 + alignment to 16 byte boundary, plus 16 bytes.
  symbols.get_symbol("semihost_param", value, size, bind, type, section_index,
                     other);
  EXPECT_EQ(value, 0x2000 + 16 + 16);
  EXPECT_EQ(section_index, elf().sections[".bss"]->get_index());
  EXPECT_EQ(bind, ELFIO::STB_LOCAL);
  EXPECT_EQ(type, ELFIO::STT_NOTYPE);
}

// Verify that the first 16 instructions were assembled correctly.
TEST_F(RiscV64XAssemblerTest, ExecutableTextContent) {
  auto status = assembler()->CreateExecutable(0x1000, "main");
  CHECK_OK(status) << status.message();
  auto* text = elf().sections[".text"];
  auto* data = text->get_data();
  auto* word_data = reinterpret_cast<const uint32_t*>(data);
  // Verify the first 16 instructions.
  EXPECT_EQ(word_data[0], 0x00500513);   // addi a0, zero, 5
  EXPECT_EQ(word_data[1], 0x000025b7);   // lui a1, semihost_param
  EXPECT_EQ(word_data[2], 0x02058593);   // addi a1, a1, semihost_param
  EXPECT_EQ(word_data[3], 0x00200293);   // addi t0, zero, 2
  EXPECT_EQ(word_data[4], 0x0055b023);   // sd t0, 0(a1)
  EXPECT_EQ(word_data[5], 0x000023b7);   // lui t2, hello
  EXPECT_EQ(word_data[6], 0x00038393);   // addi t2, t2, hello
  EXPECT_EQ(word_data[7], 0x0075b423);   // sd t2, 8(a1)
  EXPECT_EQ(word_data[8], 0x00c00293);   // addi t0, zero, 12
  EXPECT_EQ(word_data[9], 0x0055b823);   // sd t0, 0x10(a1)
  EXPECT_EQ(word_data[10], 0x01c000ef);  // jal ra, semihost
  EXPECT_EQ(word_data[11], 0x01800513);  // addi a0, zero, 24
  EXPECT_EQ(word_data[12], 0x000202b7);  // lui t0, 0x20026
  EXPECT_EQ(word_data[13], 0x02628293);  // addi t0, t0, 0x20026
  EXPECT_EQ(word_data[14], 0x0055b023);  // sd t0, 0(a1)
  EXPECT_EQ(word_data[15], 0x008000ef);  // jal ra, semihost
}

// Verify that the first 16 instructions were assembled correctly.
TEST_F(RiscV64XAssemblerTest, RelocatableTextContent) {
  auto status = assembler()->CreateRelocatable();
  CHECK_OK(status) << status.message();
  auto* text = elf().sections[".text"];
  auto* data = text->get_data();
  auto* word_data = reinterpret_cast<const uint32_t*>(data);
  // Verify the first 16 instructions. These will be slightly different from
  // the executable version since the symbol values are not relocated to their
  // final memory values.
  EXPECT_EQ(word_data[0], 0x00500513);   // addi a0, zero, 5
  EXPECT_EQ(word_data[1], 0x000005b7);   // lui a1, semihost_param
  EXPECT_EQ(word_data[2], 0x01058593);   // addi a1, a1, semihost_param
  EXPECT_EQ(word_data[3], 0x00200293);   // addi t0, zero, 2
  EXPECT_EQ(word_data[4], 0x0055b023);   // sd t0, 0(a1)
  EXPECT_EQ(word_data[5], 0x000003b7);   // lui t2, hello
  EXPECT_EQ(word_data[6], 0x00038393);   // addi t2, t2, hello
  EXPECT_EQ(word_data[7], 0x0075b423);   // sd t2, 8(a1)
  EXPECT_EQ(word_data[8], 0x00c00293);   // addi t0, zero, 12
  EXPECT_EQ(word_data[9], 0x0055b823);   // sd t0, 0x10(a1)
  EXPECT_EQ(word_data[10], 0x01c000ef);  // jal ra, semihost
  EXPECT_EQ(word_data[11], 0x01800513);  // addi a0, zero, 24
  EXPECT_EQ(word_data[12], 0x000202b7);  // lui t0, 0x20026
  EXPECT_EQ(word_data[13], 0x02628293);  // addi t0, t0, 0x20026
  EXPECT_EQ(word_data[14], 0x0055b023);  // sd t0, 0(a1)
  EXPECT_EQ(word_data[15], 0x008000ef);  // jal ra, semihost
}

TEST_F(RiscV64XAssemblerTest, TextRelocations) {
  auto status = assembler()->CreateRelocatable();
  CHECK_OK(status) << status.message();
  auto* rela_section = elf().sections[".rela.text"];
  EXPECT_NE(rela_section, nullptr);
  auto* rela_data = rela_section->get_data();
  auto rela =
      absl::MakeSpan(reinterpret_cast<const ELFIO::Elf64_Rela*>(rela_data),
                     rela_section->get_size() / sizeof(ELFIO::Elf64_Rela));
  EXPECT_EQ(rela.size(), 4);
}

}  // namespace
