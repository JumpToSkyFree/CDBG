#ifndef CDBG_ENGINE_BREAKPOINT_HPP
#define CDBG_ENGINE_BREAKPOINT_HPP

#include <cstdint>
namespace cdbg::engine {
class breakpoint {
  public:
    breakpoint() = default;
    breakpoint(breakpoint &&) = default;
    breakpoint(const breakpoint &) = default;
    breakpoint &operator=(breakpoint &&) = default;
    breakpoint &operator=(const breakpoint &) = default;
    ~breakpoint() = default;

  public:
    breakpoint(std::uint64_t addr, long orig_instruction, bool is_enabled);
    std::uint64_t get_addr() const;
    void set_addr(std::uint64_t addr);
    void set_id(int id);
    int get_id() const;
    void activate();
    void deactivate();
    bool is_enabled() const;
    long get_orig_instruction() const;
    void set_orig_instruction(long instruction);

  private:
    std::uint64_t addr;
    long orig_instruction{0x00};
    bool enabled{false};
    int id{-1};
};
} // namespace cdbg::engine

#endif // !CDBG_ENGINE_BREAKPOINT_HPP
