#ifndef CDBG_ENGINE_THREAD_HPP
#define CDBG_ENGINE_THREAD_HPP

#include <cstddef>
#include <cstdint>
#include <engine/backend.hpp>
#include <expected>

namespace cdbg::engine {

class thread {
  public:
    explicit thread(process_id_t tid) : tid_(tid) {}
    thread() = default;
    thread(const thread &) = default;
    thread &operator=(const thread &) = default;

    process_id_t id() const { return tid_; }

    std::expected<backend::registers, error<backend::backend_error>> get_regs() const {
        return backend::get_regs(tid_);
    }
    std::expected<void, error<backend::backend_error>>
    set_regs(const backend::registers &regs) const {
        return backend::set_regs(tid_, regs);
    }
    std::expected<std::uint64_t, error<backend::backend_error>> get_pc() const {
        return backend::get_pc(tid_);
    }
    std::expected<void, error<backend::backend_error>>
    set_pc(std::uint64_t addr) const {
        return backend::set_pc(tid_, addr);
    }
    std::expected<void, error<backend::backend_error>>
    read_memory(std::uint64_t addr, void *buf, std::size_t len) const {
        return backend::read_memory(tid_, addr, buf, len);
    }
    std::expected<void, error<backend::backend_error>>
    write_memory(std::uint64_t addr, const void *buf, std::size_t len) const {
        return backend::write_memory(tid_, addr, buf, len);
    }
    std::expected<void, error<backend::backend_error>>
    set_breakpoint(std::uint64_t addr, long &orig) const {
        return backend::set_breakpoint(tid_, addr, orig);
    }
    std::expected<void, error<backend::backend_error>>
    remove_breakpoint(std::uint64_t addr, long orig) const {
        return backend::remove_breakpoint(tid_, addr, orig);
    }
    std::expected<backend::event, error<backend::backend_error>> wait() const {
        return backend::wait(tid_);
    }
    backend::event poll() const { return backend::poll(tid_); }
    std::expected<void, error<backend::backend_error>> cont(int signal = 0) const {
        return backend::cont(tid_, signal);
    }
    std::expected<void, error<backend::backend_error>> single_step() const {
        return backend::single_step(tid_);
    }

  private:
    process_id_t tid_{0};
};

}

#endif