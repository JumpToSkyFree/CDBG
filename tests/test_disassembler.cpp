#include "helpers.hpp"
#include <cstdint>
#include <engine/disassembler.hpp>
#include <vector>

using cdbg::engine::disassembler;

TEST_CASE("disassembler decodes a known single instruction",
          "[disassembler]") {
    disassembler d;
    REQUIRE(d.current_architecture() == disassembler::host_architecture());

    std::uint64_t addr = 0x4000;
    std::vector<std::uint8_t> bytes;
    if (disassembler::host_architecture() ==
        disassembler::architecture::aarch64) {
        bytes = {0x1f, 0x20, 0x03, 0xd5};
    } else {
        bytes = {0x90};
    }

    auto insns = d.disassemble(addr, bytes.data(), bytes.size());
    REQUIRE(insns.has_value());
    REQUIRE(insns->size() == 1);
    REQUIRE(insns->at(0).address == addr);
    REQUIRE(insns->at(0).mnemonic == "nop");
}

TEST_CASE("disassembler reports an error for invalid input",
          "[disassembler]") {
    disassembler d;
    std::vector<std::uint8_t> bytes;
    if (disassembler::host_architecture() ==
        disassembler::architecture::aarch64) {
        bytes = {0x1f, 0x20, 0x03};
    } else {
        bytes = {0x0f};
    }

    auto insns = d.disassemble(0x1000, bytes.data(), bytes.size());
    REQUIRE(!insns.has_value());
    REQUIRE(insns.error().code == disassembler::decode_failed);
}

struct DisassemblerBackendFixture : BackendFixture {};

TEST_CASE_METHOD(DisassemblerBackendFixture,
                 "disassemble real code from a stopped process",
                 "[disassembler][backend]") {
    auto pc = proc.get_pc();
    REQUIRE(pc.has_value());
    std::vector<std::uint8_t> bytes;
    bytes.resize(16);
    REQUIRE(proc.read_memory(*pc, bytes.data(), bytes.size()).has_value());
    disassembler d;
    auto insns = d.disassemble(*pc, bytes.data(), bytes.size());
    REQUIRE(insns.has_value());
    REQUIRE(!insns->empty());
    REQUIRE(insns->at(0).address == *pc);
    REQUIRE(!insns->at(0).mnemonic.empty());
}