#include "platform_types.hpp"
#include <engine/backend.hpp>
#if defined(__linux__)
#include <algorithm>
#include <bits/types/struct_iovec.h>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <elf.h>
#include <elfutils/libdwfl.h>
#include <errno.h>
#include <filesystem>
#include <optional>
#include <sched.h>
#include <string>
#include <sys/ptrace.h>
#include <sys/types.h>
#include <sys/user.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

namespace cdbg::engine::backend {
namespace detail {

std::uint64_t breakpoint_pc_adjustment() {
#if defined(__aarch64__)
    return 0;
#elif defined(__x86_64)
    return 1;
#endif
}

std::optional<bool> is_instruction_trap(process_id_t pid,
                                        std::uint64_t address) {
#if defined(__aarch64__)
    std::uint32_t instruction;
    if (!read_memory(pid, address, &instruction, sizeof(std::uint32_t)))
        return std::nullopt;
    return instruction == 0xD4200000;
#elif defined(__x86_64)
    std::uint8_t instruction;
    if (!read_memory(pid, address, &instruction, sizeof(std::uint8_t)))
        return std::nullopt;
    return instruction == 0xCC;
#endif
    return std::nullopt;
}

event interpret_status(process_id_t pid, int status) {
    if (WIFEXITED(status)) {
        return exited{WEXITSTATUS(status)};
    }
    if (WIFSIGNALED(status)) {
        return signaled{WTERMSIG(status)};
    }
    if (!WIFSTOPPED(status)) {
        return stopped_by_signal{0};
    }

    int sig = WSTOPSIG(status);
    int event = status >> 8;

    if (sig == SIGTRAP) {
        if (event == (SIGTRAP | (PTRACE_EVENT_CLONE << 8))) {
            process_id_t nid{};
            if (ptrace(PTRACE_GETEVENTMSG, pid, nullptr, &nid) == -1)
                return none{};
            return thread_created{nid};
        }

        auto pc = get_pc(pid);
        if (!pc) {
            return none{};
        }

        auto is_trap = is_instruction_trap(pid, *pc);
        if (is_trap != std::nullopt) {
            if (*is_trap) {
                return stopped_at_breakpoint{*pc - breakpoint_pc_adjustment()};
            }
            return stopped_at_step{};
        }
    }

    if (sig == SIGCHLD) {
        ptrace(PTRACE_CONT, pid, nullptr, SIGCHLD);
        return none{};
    }

    return stopped_by_signal{sig};
}

}

std::expected<void, error<backend_error>>
read_memory(process_id_t pid, std::uint64_t addr, void *buf, std::size_t len) {
    if (!is_process_alive(pid)) {
        return std::unexpected(
            error<backend_error>{no_process_attached, std::nullopt});
    }
    auto *out = static_cast<std::uint8_t *>(buf);
    std::size_t bytes_read = 0;
    while (bytes_read < len) {
        std::uint64_t word_addr = addr + bytes_read;
        errno = 0;
        long word = ptrace(PTRACE_PEEKTEXT, pid, (void *)word_addr, nullptr);
        if (word == -1 && errno != 0) {
            return std::unexpected(error<backend_error>{
                failed,
                native_error_info{errno, ::strerror(errno),
                                  "ptrace(PTRACE_PEEKTEXT)"}});
        }
        std::size_t remaning = len - bytes_read;
        std::size_t chunk = std::min(remaning, sizeof(long));
        std::memcpy(out + bytes_read, &word, chunk);
        bytes_read += chunk;
    }
    return {};
}

std::expected<void, error<backend_error>>
write_memory(process_id_t pid, std::uint64_t addr, const void *buf,
             std::size_t len) {
    if (!is_process_alive(pid)) {
        return std::unexpected(
            error<backend_error>{no_process_attached, std::nullopt});
    }
    const auto *in = static_cast<const std::uint8_t *>(buf);
    std::size_t bytes_written = 0;
    while (bytes_written < len) {
        std::uint64_t word_addr = addr + bytes_written;
        std::size_t remaning = len - bytes_written;
        long word;
        if (remaning >= sizeof(long)) {
            std::memcpy(&word, in + bytes_written, sizeof(long));
        } else {
            errno = 0;
            word = ptrace(PTRACE_PEEKTEXT, pid, (void *)word_addr, nullptr);
            if (word == -1 && errno != 0) {
                return std::unexpected(error<backend_error>{
                    failed,
                    native_error_info{errno, ::strerror(errno),
                                      "ptrace(PTRACE_PEEKTEXT)"}});
            }
            std::memcpy(&word, in + bytes_written, remaning);
        }
        if (ptrace(PTRACE_POKEDATA, pid, (void *)word_addr, (void *)word) ==
            -1) {
            return std::unexpected(error<backend_error>{
                failed,
                native_error_info{errno, ::strerror(errno),
                                  "ptrace(PTRACE_POKEDATA)"}});
        }
        bytes_written += std::min(remaning, sizeof(long));
    }
    return {};
}

std::expected<registers, error<backend_error>> get_regs(process_id_t pid) {
    if (!is_process_alive(pid)) {
        return std::unexpected(
            error<backend_error>{no_process_attached, std::nullopt});
    }
    struct iovec iov;
    struct user_regs_struct raw;
    iov.iov_base = &raw;
    iov.iov_len = sizeof(raw);
    if (ptrace(PTRACE_GETREGSET, pid, (void *)NT_PRSTATUS, &iov) == -1) {
        return std::unexpected(error<backend_error>{
            failed,
            native_error_info{errno, ::strerror(errno),
                              "ptrace(PTRACE_GETREGSET)"}});
    }
    registers regs;
#if defined(__aarch64__)
    for (int i = 0; i < 31; i++) {
        regs.data["x" + std::to_string(i)] = raw.regs[i];
    }
    regs.data["sp"] = raw.sp;
    regs.data["pc"] = raw.pc;
    regs.data["pstate"] = raw.pstate;
#elif defined(__x86_64)
    regs.data["rip"] = raw.rip;
    regs.data["rsp"] = raw.rsp;
    regs.data["rbp"] = raw.rbp;
    regs.data["rax"] = raw.rax;
    regs.data["rbx"] = raw.rbx;
    regs.data["rcx"] = raw.rcx;
    regs.data["rdx"] = raw.rdx;
    regs.data["rsi"] = raw.rsi;
    regs.data["rdi"] = raw.rdi;
    regs.data["r8"] = raw.r8;
    regs.data["r9"] = raw.r9;
    regs.data["r10"] = raw.r10;
    regs.data["r11"] = raw.r11;
    regs.data["r12"] = raw.r12;
    regs.data["r13"] = raw.r13;
    regs.data["r14"] = raw.r14;
    regs.data["r15"] = raw.r15;
    regs.data["eflags"] = raw.eflags;
    regs.data["orig_rax"] = raw.orig_rax;
#endif
    return regs;
}

std::expected<void, error<backend_error>>
set_regs(process_id_t pid, const registers &regs) {
    if (!is_process_alive(pid)) {
        return std::unexpected(
            error<backend_error>{no_process_attached, std::nullopt});
    }

    struct user_regs_struct raw{};

    struct iovec iov{&raw, sizeof(raw)};
    if (ptrace(PTRACE_GETREGSET, pid, (void *)NT_PRSTATUS, &iov) == -1) {
        return std::unexpected(error<backend_error>{
            failed,
            native_error_info{errno, ::strerror(errno),
                              "ptrace(PTRACE_GETREGSET)"}});
    }

#if defined(__aarch64__)
    for (int i = 0; i < 31; i++) {
        if (auto v = regs.get("x" + std::to_string(i))) {
            raw.regs[i] = *v;
        }
    }
    if (auto v = regs.get("sp"))
        raw.sp = *v;
    if (auto v = regs.get("pc"))
        raw.pc = *v;
    if (auto v = regs.get("pstate"))
        raw.pstate = *v;
#elif defined(__x86_64)
    if (auto v = regs.get("rip"))
        raw.rip = *v;
    if (auto v = regs.get("rsp"))
        raw.rsp = *v;
    if (auto v = regs.get("rbp"))
        raw.rbp = *v;
    if (auto v = regs.get("rax"))
        raw.rax = *v;
    if (auto v = regs.get("rbx"))
        raw.rbx = *v;
    if (auto v = regs.get("rcx"))
        raw.rcx = *v;
    if (auto v = regs.get("rdx"))
        raw.rdx = *v;
    if (auto v = regs.get("rsi"))
        raw.rsi = *v;
    if (auto v = regs.get("rdi"))
        raw.rdi = *v;
    if (auto v = regs.get("r8"))
        raw.r8 = *v;
    if (auto v = regs.get("r9"))
        raw.r9 = *v;
    if (auto v = regs.get("r10"))
        raw.r10 = *v;
    if (auto v = regs.get("r11"))
        raw.r11 = *v;
    if (auto v = regs.get("r12"))
        raw.r12 = *v;
    if (auto v = regs.get("r13"))
        raw.r13 = *v;
    if (auto v = regs.get("r14"))
        raw.r14 = *v;
    if (auto v = regs.get("r15"))
        raw.r15 = *v;
    if (auto v = regs.get("eflags"))
        raw.eflags = *v;
#endif
    iov = {&raw, sizeof(raw)};
    if (ptrace(PTRACE_SETREGSET, pid, (void *)NT_PRSTATUS, &iov) == -1) {
        return std::unexpected(error<backend_error>{
            failed,
            native_error_info{errno, ::strerror(errno),
                              "ptrace(PTRACE_SETREGSET)"}});
    }
    return {};
}

std::expected<std::uint64_t, error<backend_error>>
get_pc(process_id_t pid) {
    auto regs = get_regs(pid);
    if (!regs) {
        return std::unexpected(regs.error());
    }
#if defined(__aarch64__)
    return regs->get("pc");
#elif defined(__x86_64)
    return regs->get("rip");
#endif
    return std::unexpected(error<backend_error>{failed, std::nullopt});
}

std::expected<void, error<backend_error>>
set_pc(process_id_t pid, std::uint64_t addr) {
    auto regs = get_regs(pid);
    if (!regs) {
        return std::unexpected(regs.error());
    }
#if defined(__aarch64__)
    regs->set("pc", addr);
#elif defined(__x86_64)
    regs->set("rip", addr);
#endif
    return set_regs(pid, *regs);
}

std::expected<std::uint64_t, error<backend_error>>
get_sp(process_id_t pid) {
    auto regs = get_regs(pid);
    if (!regs) {
        return std::unexpected(regs.error());
    }
#if defined(__aarch64__)
    return regs->get("sp");
#elif defined(__x86_64)
    return regs->get("rsp");
#endif
    return std::unexpected(error<backend_error>{failed, std::nullopt});
}

std::expected<void, error<backend_error>> cont(process_id_t pid, int signal) {
    if (!is_process_alive(pid)) {
        return std::unexpected(error<backend_error>{continue_failed,
                                                    std::nullopt});
    }
    if (ptrace(PTRACE_CONT, pid, nullptr, signal) == -1) {
        return std::unexpected(error<backend_error>{
            failed,
            native_error_info{errno, ::strerror(errno),
                              "ptrace(PTRACE_CONT)"}});
    }
    return {};
}

std::expected<void, error<backend_error>>
single_step(process_id_t pid) {
    if (!is_process_alive(pid)) {
        return std::unexpected(
            error<backend_error>{no_process_attached, std::nullopt});
    }
    if (ptrace(PTRACE_SINGLESTEP, pid, nullptr, 0) == -1) {
        return std::unexpected(error<backend_error>{
            failed,
            native_error_info{errno, ::strerror(errno),
                              "ptrace(PTRACE_SINGLESTEP)"}});
    }
    return {};
}

std::expected<void, error<backend_error>>
set_breakpoint(process_id_t pid, std::uint64_t addr, long &orig) {
    if (auto res = read_memory(pid, addr, &orig, 1); !res) {
        return std::unexpected(res.error());
    }

#if defined(__x86_64)
    std::uint8_t original_byte;
    if (auto res =
            read_memory(pid, addr, &original_byte, sizeof(original_byte));
        !res) {
        return std::unexpected(res.error());
    }
    orig = original_byte;

    std::uint8_t trap = 0xCC;
    if (auto res = write_memory(pid, addr, &trap, sizeof(trap)); !res) {
        return std::unexpected(res.error());
    }
#elif defined(__aarch64__)
    std::uint32_t original_instr;
    if (auto res =
            read_memory(pid, addr, &original_instr, sizeof(original_instr));
        !res) {
        return std::unexpected(res.error());
    }
    orig = original_instr;

    std::uint32_t brk = 0xD4200000;
    if (auto res = write_memory(pid, addr, &brk, sizeof(brk)); !res) {
        return std::unexpected(res.error());
    }
#endif
    return {};
}

std::expected<void, error<backend_error>>
remove_breakpoint(process_id_t pid, std::uint64_t addr, long orig) {
    if (!is_process_alive(pid)) {
        return std::unexpected(error<backend_error>{continue_failed,
                                                    std::nullopt});
    }
#if defined(__x86_64)
    std::uint8_t byte = static_cast<std::uint8_t>(orig);
    if (auto res = write_memory(pid, addr, &byte, sizeof(byte)); !res) {
        return std::unexpected(res.error());
    }
#elif defined(__aarch64__)
    std::uint32_t instr = static_cast<std::uint32_t>(orig);
    if (auto res = write_memory(pid, addr, &instr, sizeof(instr)); !res) {
        return std::unexpected(res.error());
    }
#endif
    return {};
}

std::expected<event, error<backend_error>> wait(process_id_t pid) {
    int status;
    if (waitpid(pid, &status, 0) == -1) {
        return std::unexpected(error<backend_error>{
            wait_failed,
            native_error_info{errno, ::strerror(errno), "waitpid"}});
    }
    return detail::interpret_status(pid, status);
}

event poll(process_id_t pid) {
    int status;
    if (waitpid(pid, &status, WNOHANG) <= 0) {
        return none{};
    }
    return detail::interpret_status(pid, status);
}

bool is_process_alive(process_id_t pid) {
    if (pid <= 0)
        return false;
    int status;
    pid_t result = waitpid(pid, &status, WNOHANG);
    if (result == 0) {
        return true;
    }
    if (result == pid) {
        return false;
    }
    return false;
}

std::expected<std::vector<process_id_t>, error<backend_error>>
list_thread_ids(process_id_t pid) {
    if (pid <= 0)
        return std::unexpected(
            error<backend_error>{no_process_attached, std::nullopt});
    std::vector<process_id_t> ids;
    std::string task_dir = "/proc/" + std::to_string(pid) + "/task/";

    for (auto &entry : std::filesystem::directory_iterator(task_dir)) {
        if (entry.is_directory()) {
            ids.push_back(std::stoi(entry.path().filename().string()));
        }
    }
    return ids;
}

std::expected<void, error<backend_error>> attach(process_id_t pid) {
    if (ptrace(PTRACE_ATTACH, pid, nullptr, nullptr) == -1) {
        return std::unexpected(error<backend_error>{
            attach_failed,
            native_error_info{errno, ::strerror(errno),
                              "ptrace(PTRACE_ATTACH)"}});
    }
    return {};
}

std::expected<void, error<backend_error>> traceme() {
    if (ptrace(PTRACE_TRACEME, 0, nullptr, nullptr) == -1) {
        return std::unexpected(error<backend_error>{
            failed,
            native_error_info{errno, ::strerror(errno),
                              "ptrace(PTRACE_TRACEME)"}});
    }
    return {};
}

std::expected<void, error<backend_error>>
set_options(process_id_t pid, int options) {
    if (ptrace(PTRACE_SETOPTIONS, pid, nullptr, options) == -1) {
        return std::unexpected(error<backend_error>{
            failed,
            native_error_info{errno, ::strerror(errno),
                              "ptrace(PTRACE_SETOPTIONS)"}});
    }
    return {};
}

std::expected<void, error<backend_error>> kill_process(process_id_t pid) {
    if (pid <= 0)
        return std::unexpected(
            error<backend_error>{no_process_attached, std::nullopt});
    if (::kill(pid, SIGKILL) == -1) {
        return std::unexpected(error<backend_error>{
            failed,
            native_error_info{errno, ::strerror(errno), "kill"}});
    }
    return {};
}

}

#elif defined(_WIN32)
#error "WIN32 is not supported yet."
#endif
