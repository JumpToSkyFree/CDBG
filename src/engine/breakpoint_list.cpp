#include <algorithm>
#include <engine/breakpoint.hpp>
#include <engine/breakpoint_list.hpp>
#include <vector>

namespace cdbg::engine {
breakpoint_list::breakpoint_list() {}
breakpoint_list::~breakpoint_list() {}
breakpoint *breakpoint_list::breakpoint_list::add(std::uint64_t address,
                                                  long orig_instruction,
                                                  bool is_enabled) {
    if (!address)
        return nullptr;
    breakpoint bp = breakpoint(address, orig_instruction, is_enabled);
    bp.set_id(counter++);
    this->breakpoints.push_back(bp);
    return &this->breakpoints.back();
}
breakpoint *breakpoint_list::find_by_addr(std::uint64_t address) {
    auto it = std::find_if(
        breakpoints.begin(), breakpoints.end(),
        [address](const breakpoint &bp) { return bp.get_addr() == address; });
    if (it == breakpoints.cend()) {
        return nullptr;
    }
    return &*it;
}
breakpoint *breakpoint_list::find_by_id(int id) {
    auto it =
        std::find_if(breakpoints.begin(), breakpoints.end(),
                     [id](const breakpoint &bp) { return bp.get_id() == id; });
    if (it == breakpoints.end()) {
        return nullptr;
    }
    return &*it;
}
void breakpoint_list::remove(int id) {
    breakpoints.erase(std::remove_if(breakpoints.begin(), breakpoints.end(),
                                     [id](const breakpoint &bp) {
                                         return bp.get_id() == id;
                                     }),
                      breakpoints.end());
}
} // namespace cdbg::engine
