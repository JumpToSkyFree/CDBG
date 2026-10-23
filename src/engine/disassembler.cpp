#include "platform_types.hpp"
#include <cstdint>
#include <memory>
#include <vector>
#if defined(__linux__)
#include <engine/internal/disassembler_linux_impl.hpp>
#endif
#include <engine/disassembler.hpp>

namespace cdbg::engine {

disassembler::disassembler(architecture arch)
    : impl_(std::make_unique<impl>(arch)) {}

disassembler::~disassembler() {}

disassembler::architecture disassembler::current_architecture() const {
    return impl_->architecture();
}

std::expected<std::vector<disassembler::instruction>,
              error<disassembler::disassembler_error>>
disassembler::disassemble(std::uint64_t address,
                          std::uint8_t *bytes, std::size_t size) const {
    return impl_->disassemble(address, bytes, size);
}

} // namespace cdbg::engine
