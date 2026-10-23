#ifndef CDBG_PROCESS_LINUX_IMPL_HPP
#define CDBG_PROCESS_LINUX_IMPL_HPP

#include "engine/backend.hpp"
#include <cstddef>
#include <cstdint>
#include <engine/process.hpp>
#include <expected>
#include <pty.h>
#include <signal.h>
#include <string.h>
#include <string>
#include <sys/ptrace.h>
#include <sys/wait.h>
#include <unistd.h>
#include <utility>
#include <vector>

namespace cdbg::engine {

struct process::impl {
    std::string executable_path_;
    char **argv_{nullptr};
    process_id_t pid_{-1};
    bool initialized_{false};
    int master{-1};
    std::string slave_path_buffer;

    impl(std::string executable_path, char **argv)
        : executable_path_(std::move(executable_path)), argv_(argv) {}

    impl(process_id_t pid) : pid_(pid) {}

    ~impl() {
        if (!initialized_)
            return;
        ::kill(pid_, SIGKILL);
        int status;
        ::waitpid(pid_, &status, 0);
        if (master >= 0)
            ::close(master);
    }

    std::expected<void, error<backend::backend_error>> initialize() {
        if (pid_ > 0) {
            if (!backend::attach(pid_))
                return std::unexpected(error<backend::backend_error>{
                    backend::attach_failed, std::nullopt});
            int status;
            if (::waitpid(pid_, &status, 0) == -1)
                return std::unexpected(error<backend::backend_error>{
                    backend::attach_failed,
                    native_error_info{errno, ::strerror(errno), "waitpid"}});
            if (!WIFSTOPPED(status))
                return std::unexpected(error<backend::backend_error>{
                    backend::attach_failed, std::nullopt});
            if (!backend::set_options(pid_, PTRACE_O_TRACECLONE))
                return std::unexpected(error<backend::backend_error>{
                    backend::failed, std::nullopt});
            initialized_ = true;
            return {};
        }

        if (executable_path_.empty())
            return std::unexpected(
                error<backend::backend_error>{backend::failed, std::nullopt});

        if(!slave_path_buffer.capacity())
            // TODO: Unknown slave path size, hardcoded 256, documentation claims unknown size.
            slave_path_buffer.reserve(256);

        pid_t child = ::forkpty(&master, slave_path_buffer.data(), nullptr, nullptr);
        if (child < 0)
            return std::unexpected(error<backend::backend_error>{
                backend::spawn_process_failed,
                native_error_info{errno, ::strerror(errno), "fork"}});

        if (child == 0) {
            if (ptrace(PTRACE_TRACEME, 0, nullptr, nullptr) == -1)
                _exit(127);
            if (argv_) {
                execvp(executable_path_.c_str(), argv_);
            } else {
                char *args[2] = {const_cast<char *>(executable_path_.c_str()),
                                 nullptr};
                execvp(executable_path_.c_str(), args);
            }
            _exit(127);
        }

        pid_ = child;
        int status;
        if (::waitpid(child, &status, 0) == -1)
            return std::unexpected(error<backend::backend_error>{
                backend::spawn_process_failed, std::nullopt});
        if (!WIFSTOPPED(status))
            return std::unexpected(error<backend::backend_error>{
                backend::spawn_process_failed, std::nullopt});
        if (!backend::set_options(child, PTRACE_O_TRACECLONE)) {
            ::kill(child, SIGKILL);
            return std::unexpected(
                error<backend::backend_error>{backend::failed, std::nullopt});
        }
        initialized_ = true;
        return {};
    }

    bool is_initialized() const { return initialized_; }
    bool is_process_alive() const { return backend::is_process_alive(pid_); }
    process_id_t get_process_id() const { return pid_; }
    const std::string &executable_path() const { return executable_path_; }

    std::expected<std::vector<thread>, error<backend::backend_error>>
    list_threads() {
        auto ids = backend::list_thread_ids(pid_);
        if (!ids)
            return std::unexpected(ids.error());
        std::vector<thread> res;
        for (auto id : *ids)
            res.emplace_back(id);
        return res;
    }

    std::expected<std::uint64_t, error<backend::backend_error>> get_pc() const {
        return backend::get_pc(pid_);
    }
    std::expected<std::uint64_t, error<backend::backend_error>> get_sp() const {
        return backend::get_sp(pid_);
    }
    std::expected<backend::registers, error<backend::backend_error>>
    get_regs() const {
        return backend::get_regs(pid_);
    }
    std::expected<void, error<backend::backend_error>>
    set_regs(const backend::registers &regs) const {
        return backend::set_regs(pid_, regs);
    }
    std::expected<void, error<backend::backend_error>>
    set_pc(std::uint64_t addr) const {
        return backend::set_pc(pid_, addr);
    }
    std::expected<void, error<backend::backend_error>>
    read_memory(std::uint64_t addr, void *buf, std::size_t len) const {
        return backend::read_memory(pid_, addr, buf, len);
    }
    std::expected<void, error<backend::backend_error>>
    write_memory(std::uint64_t addr, const void *buf, std::size_t len) const {
        return backend::write_memory(pid_, addr, buf, len);
    }
    std::expected<void, error<backend::backend_error>>
    set_breakpoint(std::uint64_t addr, long &orig) const {
        return backend::set_breakpoint(pid_, addr, orig);
    }
    std::expected<void, error<backend::backend_error>>
    remove_breakpoint(std::uint64_t addr, long orig) const {
        return backend::remove_breakpoint(pid_, addr, orig);
    }
    std::expected<backend::event, error<backend::backend_error>> wait() const {
        return backend::wait(pid_);
    }
    backend::event poll() const { return backend::poll(pid_); }
    std::expected<void, error<backend::backend_error>> cont(int signal = 0) const {
        return backend::cont(pid_, signal);
    }
    std::expected<void, error<backend::backend_error>>
    single_step() const {
        return backend::single_step(pid_);
    }
    std::expected<std::size_t, error<backend::backend_error>>
    read_terminal(void *buf, std::size_t len) const {
        if (master < 0)
            return std::unexpected(
                error<backend::backend_error>{backend::failed, std::nullopt});
        ssize_t n = ::read(master, buf, len);
        if (n == -1)
            return std::unexpected(error<backend::backend_error>{
                backend::failed,
                native_error_info{errno, ::strerror(errno), "read"}});
        return static_cast<std::size_t>(n);
    }
    std::expected<std::size_t, error<backend::backend_error>>
    write_terminal(const void *buf, std::size_t len) const {
        if (master < 0)
            return std::unexpected(
                error<backend::backend_error>{backend::failed, std::nullopt});
        ssize_t n = ::write(master, buf, len);
        if (n == -1)
            return std::unexpected(error<backend::backend_error>{
                backend::failed,
                native_error_info{errno, ::strerror(errno), "write"}});
        return static_cast<std::size_t>(n);
    }
};
}

#endif
