#ifndef CDBG_ENGINE_BACKEND_HPP
#define CDBG_ENGINE_BACKEND_HPP

#include <cstddef>
#include <cstdint>
#include <error.hpp>
#include <optional>
#include <platform_types.hpp>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>
#include <expected>

namespace cdbg::engine::backend {

struct none {};
struct stopped_at_breakpoint {
    std::uint64_t addr;
};
struct stopped_at_step {};
struct stopped_by_signal {
    int signum;
};
struct thread_created {
    process_id_t id;
};
struct exited {
    int exit_code;
};
struct signaled {
    int signum;
};

using event = std::variant<none, stopped_at_breakpoint, stopped_at_step,
                           stopped_by_signal, exited, signaled, thread_created>;

enum backend_error {
    failed,
    wait_failed,
    spawn_process_failed,
    attach_failed,
    reading_debug_info_failed,
    no_process_attached,
    continue_failed,
    source_not_found,
    step_budget_exceeded,
};

struct registers {
    std::unordered_map<std::string, std::uint64_t> data;

    registers() {}
    registers(std::unordered_map<std::string, std::uint64_t> &&d)
        : data(std::move(d)) {}
    ~registers() = default;

    std::expected<std::uint64_t, error<backend_error>>
    get(const std::string &name) const {
        auto it = data.find(name);
        if (it == data.end())
            return std::unexpected(error<backend_error>{failed, std::nullopt});
        return it->second;
    }

    void set(std::string name, std::uint64_t value) { data[name] = value; }
};

std::expected<void, error<backend_error>>
read_memory(process_id_t pid, std::uint64_t addr, void *buf, std::size_t len);
std::expected<void, error<backend_error>>
write_memory(process_id_t pid, std::uint64_t addr, const void *buf,
             std::size_t len);

std::expected<registers, error<backend_error>> get_regs(process_id_t pid);
std::expected<void, error<backend_error>> set_regs(process_id_t pid, const registers &regs);
std::expected<std::uint64_t, error<backend_error>> get_pc(process_id_t pid);
std::expected<void, error<backend_error>> set_pc(process_id_t pid, std::uint64_t addr);
std::expected<std::uint64_t, error<backend_error>> get_sp(process_id_t pid);

std::expected<void, error<backend_error>> cont(process_id_t pid, int signal = 0);
std::expected<void, error<backend_error>> single_step(process_id_t pid);

std::expected<void, error<backend_error>> set_breakpoint(process_id_t pid,
                                            std::uint64_t addr, long &orig);
std::expected<void, error<backend_error>> remove_breakpoint(process_id_t pid,
                                               std::uint64_t addr, long orig);

std::expected<event, error<backend_error>> wait(process_id_t pid);
event poll(process_id_t pid);

bool is_process_alive(process_id_t pid);
std::expected<std::vector<process_id_t>, error<backend_error>>
list_thread_ids(process_id_t pid);
std::expected<void, error<backend_error>> attach(process_id_t pid);
std::expected<void, error<backend_error>> traceme();
std::expected<void, error<backend_error>> set_options(process_id_t pid, int options);
std::expected<void, error<backend_error>> kill_process(process_id_t pid);

} // namespace cdbg::engine::backend

#endif
