#ifndef CDBG_ENGINE_PROCESS_HPP
#define CDBG_ENGINE_PROCESS_HPP

#include <cstddef>
#include <cstdint>
#include <engine/backend.hpp>
#include <engine/symbols.hpp>
#include <engine/thread.hpp>
#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace cdbg::engine {

class symbols;

struct step_result {
    backend::event event;
    std::optional<symbols::source> location;
};

class process {
    public:
        explicit process(std::string executable_path, char **argv = nullptr);
        explicit process(process_id_t pid);

        process(const process &) = delete;
        process &operator=(const process &) = delete;
        process(process&&) noexcept;
        process& operator=(process&&) noexcept;
        ~process();

        std::expected<void, error<backend::backend_error>> initialize();
        bool is_initialized() const;
        bool is_process_alive() const;
        process_id_t get_process_id() const;
        std::expected<std::size_t, error<backend::backend_error>>
        read_terminal(void *buf, std::size_t len) const;
        std::expected<std::size_t, error<backend::backend_error>>
        write_terminal(const void *buf, std::size_t len) const;
        const std::string &executable_path() const;

        std::expected<std::vector<thread>, error<backend::backend_error>>
        list_threads();

        std::expected<std::uint64_t, error<backend::backend_error>> get_pc() const;
        std::expected<std::uint64_t, error<backend::backend_error>> get_sp() const;
        std::expected<backend::registers, error<backend::backend_error>>
        get_regs() const;
        std::expected<void, error<backend::backend_error>>
        set_regs(const backend::registers &regs) const;
        std::expected<void, error<backend::backend_error>>
        set_pc(std::uint64_t addr) const;
        std::expected<void, error<backend::backend_error>>
        read_memory(std::uint64_t addr, void *buf, std::size_t len) const;
        std::expected<void, error<backend::backend_error>>
        write_memory(std::uint64_t addr, const void *buf, std::size_t len) const;
        std::expected<void, error<backend::backend_error>>
        set_breakpoint(std::uint64_t addr, long &orig) const;
        std::expected<void, error<backend::backend_error>>
        remove_breakpoint(std::uint64_t addr, long orig) const;
        std::expected<backend::event, error<backend::backend_error>> wait() const;
        backend::event poll() const;
        std::expected<void, error<backend::backend_error>> cont(int signal = 0) const;
        std::expected<void, error<backend::backend_error>> single_step() const;
        std::expected<step_result, error<backend::backend_error>>
        single_step_source(std::shared_ptr<symbols> symbols) const;
        std::expected<step_result, error<backend::backend_error>>
        step_over_source(std::shared_ptr<symbols> symbols) const;

    private:
        struct impl;
        std::unique_ptr<impl> impl_;
};

}

#endif
