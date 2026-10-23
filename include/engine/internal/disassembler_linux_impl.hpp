#ifndef CDBG_DISASSEMBLER_LINUX_IMPL_HPP
#define CDBG_DISASSEMBLER_LINUX_IMPL_HPP

#include <capstone/capstone.h>
#include <cstddef>
#include <cstdint>
#include <engine/disassembler.hpp>
#include <expected>
#include <vector>

namespace cdbg::engine {

struct disassembler::impl {
    csh handle_{0};
    disassembler::architecture arch_;

    explicit impl(disassembler::architecture arch) : arch_(arch) {
        cs_arch csarch = CS_ARCH_ARM64;
        if (arch == disassembler::architecture::x86_64)
            csarch = CS_ARCH_X86;
        cs_open(csarch, CS_MODE_LITTLE_ENDIAN, &handle_);
    }

    ~impl() {
        if (handle_ != 0)
            cs_close(&handle_);
    }

    disassembler::architecture architecture() const { return arch_; }

    std::expected<std::vector<disassembler::instruction>,
                  error<disassembler::disassembler_error>>
    disassemble(std::uint64_t address,
                std::uint8_t *bytes, std::size_t size) const {
        if (handle_ == 0)
            return std::unexpected(error<disassembler::disassembler_error>{
                disassembler::unsupported_architecture, std::nullopt});
        cs_insn *insn = nullptr;
        std::size_t count =
            cs_disasm(handle_, bytes, size, address, 0, &insn);
        if (count == 0) {
            cs_err err = cs_errno(handle_);
            return std::unexpected(error<disassembler::disassembler_error>{
                disassembler::decode_failed,
                native_error_info{static_cast<int>(err), cs_strerror(err),
                                  "cs_disasm"}});
        }
        std::vector<disassembler::instruction> res;
        res.reserve(count);
        for (std::size_t i = 0; i < count; ++i) {
            res.push_back(disassembler::instruction{
                insn[i].address, insn[i].size, insn[i].mnemonic,
                insn[i].op_str});
        }
        cs_free(insn, count);
        return res;
    }
};

}

#endif
