#ifndef CDBG_ENGINE_BREAKPOINT_LIST_HPP
#define CDBG_ENGINE_BREAKPOINT_LIST_HPP
#include <cstdint>
#include <engine/breakpoint.hpp>
#include <vector>
namespace cdbg::engine {
class breakpoint_list {
    std::vector<breakpoint> breakpoints;
    int counter{0};

  public:
    breakpoint_list(const breakpoint_list &) = delete;
    breakpoint_list &operator=(const breakpoint_list &) = delete;
    breakpoint_list(breakpoint_list &&);
    breakpoint_list &operator=(breakpoint_list &&);
    breakpoint_list();
    ~breakpoint_list();
    breakpoint *add(std::uint64_t address, long orig_instruction,
                    bool is_enabled);
    breakpoint *find_by_addr(std::uint64_t address);
    breakpoint *find_by_id(int id);
    void remove(int id);
};
} // namespace cdbg::engine
#endif // CDBG_ENGINE_BREAKPOINT_LIST_HPP
