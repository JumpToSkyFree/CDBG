#include "engine/internal/disassembler_linux_impl.hpp"
#include "engine/disassembler.hpp"

namespace cdbg::engine {
std::vector<instruction> disassembler::disassemble(std::uint64_t address, std::uint8_t *bytes,
                                     std::uint64_t size) {
    return this->impl_->disassemble(address, bytes, size);
}
}
