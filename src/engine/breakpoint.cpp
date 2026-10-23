#include <cstdint>
#include <engine/breakpoint.hpp>
namespace cdbg::engine {
breakpoint::breakpoint(std::uint64_t addr, long orig_instruction,
                       bool is_enabled)
    : addr(addr), orig_instruction(orig_instruction), enabled{is_enabled} {}
std::uint64_t breakpoint::get_addr() const { return addr; }
void breakpoint::set_addr(std::uint64_t addr) { this->addr = addr; }
void breakpoint::set_id(int id) { this->id = id; }
int breakpoint::get_id() const { return id; }
void breakpoint::activate() { this->enabled = true; }
void breakpoint::deactivate() { this->enabled = false; }
bool breakpoint::is_enabled() const { return enabled; }
long breakpoint::get_orig_instruction() const { return orig_instruction; }
void breakpoint::set_orig_instruction(long instruction) {
    orig_instruction = instruction;
}
} // namespace cdbg::engine
