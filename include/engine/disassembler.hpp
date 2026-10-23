#ifndef CDBG_DISASSEMBLER_HPP
#define CDBG_DISASSEMBLER_HPP

#include <cstdint>
#include <error.hpp>
#include <expected>
#include <memory>
#include <string>
#include <vector>

namespace cdbg::engine {

class disassembler {
  public:
    enum class architecture {
        x86_64,
        aarch64,
    };

    static architecture host_architecture() {
#if defined(__aarch64__)
        return architecture::aarch64;
#elif defined(__x86_64__)
        return architecture::x86_64;
#else
#error "Unknown architecture."
#endif
    }

    static std::uint8_t instruction_size() {
#if defined(__aarch64__)
        return 4;
#elif defined(__x86_64__)
        return 15;
#else
#error "Unknown architecture, instruction size cannot be identified."
#endif
    }

    struct instruction {
        std::uint64_t address{0};
        std::uint16_t size{0};
        std::string mnemonic;
        std::string op_str;
    };

    enum disassembler_error { unsupported_architecture, decode_failed };

    explicit disassembler(architecture arch = host_architecture());
    ~disassembler();

    disassembler(const disassembler &) = delete;
    disassembler &operator=(const disassembler &) = delete;

    architecture current_architecture() const;

    std::expected<std::vector<instruction>, error<disassembler_error>>
    disassemble(std::uint64_t address,
                uint8_t *bytes, std::size_t size) const;

  private:
    struct impl;
    std::unique_ptr<impl> impl_;
};

}

#endif
