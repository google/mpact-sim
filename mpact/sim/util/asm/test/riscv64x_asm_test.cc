#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

#include "absl/base/no_destructor.h"
#include "absl/log/check.h"
#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "elfio/elf_types.hpp"
#include "elfio/elfio.hpp"
#include "elfio/elfio_symbols.hpp"
#include "googlemock/include/gmock/gmock.h"  // IWYU pragma: keep
#include "googletest/include/gtest/gtest.h"
#include "mpact/sim/util/asm/opcode_assembler_interface.h"
#include "mpact/sim/util/asm/resolver_interface.h"
#include "mpact/sim/util/asm/simple_assembler.h"
#include "mpact/sim/util/asm/test/riscv64x_bin_encoder_interface.h"
#include "mpact/sim/util/asm/test/riscv64x_encoder.h"

// This file contains tests for the simple assembler using a very reduced
// subset of the RISC-V ISA.

namespace {

using ::mpact::sim::riscv::isa64::RiscV64XBinEncoderInterface;
using ::mpact::sim::riscv::isa64::Riscv64xSlotMatcher;
using ::mpact::sim::util::assembler::OpcodeAssemblerInterface;
using ::mpact::sim::util::assembler::ResolverInterface;
using ::mpact::sim::util::assembler::SimpleAssembler;

// This class implements the OpcodeAssemblerInterface using the slot matcher.
class RiscV64XAssembler : public OpcodeAssemblerInterface {
 public:
  RiscV64XAssembler(Riscv64xSlotMatcher* matcher) : matcher_(matcher) {};
  ~RiscV64XAssembler() override = default;
  absl::Status Encode(uint64_t address, absl::string_view text,
                      ResolverInterface* resolver,
                      std::vector<uint8_t>& bytes) override {
    // Call the slot matcher to get the encoded value.
    auto res = matcher_->Encode(address, text, 0, resolver);
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
    return absl::OkStatus();
  }

 private:
  Riscv64xSlotMatcher* matcher_;
};

// Sample assembly code.
absl::NoDestructor<std::string> kTestAssembly(R"(
; text section
    .text
    .global main
    .entry main
main:
    addi a0, zero, 5
    lui a1, semihost_param
    addi a1, a1, semihost_param
    addi t0, zero, 2
    sd t0, 0(a1)
    lui t2, hello
    addi t2, t2, hello
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
    assembler_ = new SimpleAssembler(ELFCLASS64, ELFOSABI_LINUX, ET_EXEC,
                                     EM_RISCV, 0x1000, &riscv_64x_assembler_);
    std::istringstream source(*kTestAssembly);
    // Parse the assembly code.
    auto status = assembler_->Parse(source);
    CHECK_OK(status) << status.message();
  }

  ~RiscV64XAssemblerTest() override { delete assembler_; }

  // Access the ELF writer.
  ELFIO::elfio& elf() { return assembler_->writer(); }

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
  auto* text = elf().sections[".text"];
  EXPECT_EQ(text->get_type(), SHT_PROGBITS);
  EXPECT_EQ(text->get_flags(), SHF_ALLOC | SHF_EXECINSTR);
  EXPECT_EQ(text->get_link(), SHN_UNDEF);
  EXPECT_EQ(text->get_size(), /*num inst*/ 21 * /*bytes per inst*/ 4);
}

TEST_F(RiscV64XAssemblerTest, Data) {
  auto* data = elf().sections[".data"];
  EXPECT_EQ(data->get_type(), SHT_PROGBITS);
  EXPECT_EQ(data->get_flags(), SHF_ALLOC | SHF_WRITE);
  EXPECT_EQ(data->get_link(), SHN_UNDEF);
  // Hello world is 12 bytes, plus the null terminator.
  // Add one .char declaration.
  EXPECT_EQ(data->get_size(), 14);
}

TEST_F(RiscV64XAssemblerTest, Bss) {
  auto* bss = elf().sections[".bss"];
  EXPECT_EQ(bss->get_type(), SHT_NOBITS);
  EXPECT_EQ(bss->get_flags(), SHF_ALLOC | SHF_WRITE);
  EXPECT_EQ(bss->get_link(), SHN_UNDEF);
  // Two .space declarations, each 16 bytes.
  EXPECT_EQ(bss->get_size(), 32);
}

TEST_F(RiscV64XAssemblerTest, Symbols) {
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
  EXPECT_EQ(type, STT_NOTYPE);
  // Verify that exit is valued 0x1000 + 16 * 4, local and located in the text
  // section.
  symbols.get_symbol("exit", value, size, bind, type, section_index, other);
  EXPECT_EQ(value, 0x1000 + 16 * 4);
  EXPECT_EQ(bind, STB_LOCAL);
  EXPECT_EQ(section_index, elf().sections[".text"]->get_index());
  EXPECT_EQ(type, STT_NOTYPE);
  // Verify that hello is global and located in the data section at 0x2000.
  symbols.get_symbol("hello", value, size, bind, type, section_index, other);
  EXPECT_EQ(value, 0x2000);
  EXPECT_EQ(section_index, elf().sections[".data"]->get_index());
  EXPECT_EQ(bind, STB_GLOBAL);
  EXPECT_EQ(type, STT_NOTYPE);
  // Verify that semihost_param is global and located in the bss section at
  // 0x2000 + 14 + alignment to 16 byte boundary, plus 16 bytes.
  symbols.get_symbol("semihost_param", value, size, bind, type, section_index,
                     other);
  EXPECT_EQ(value, 0x2000 + 16 + 16);
  EXPECT_EQ(section_index, elf().sections[".bss"]->get_index());
  EXPECT_EQ(bind, STB_LOCAL);
  EXPECT_EQ(type, STT_NOTYPE);
}

// Verify that the first 16 instructions were assembled correctly.
TEST_F(RiscV64XAssemblerTest, TextContent) {
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

}  // namespace
