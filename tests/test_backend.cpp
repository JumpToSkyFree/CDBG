#include "helpers.hpp"
#include <bits/floatn.h>
#include <catch2/catch.hpp>
#include <cstdint>
#include <engine/backend.hpp>
#include <engine/process.hpp>
#include <fmt/base.h>
#include <optional>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#include <variant>
using namespace cdbg::engine;

TEST_CASE("initialize backend with a path to an executable program",
          "[backend]") {
    SECTION("initialize with path to an executable successfully") {
        process p(simple_program_path(), nullptr);
        REQUIRE(p.initialize().has_value());
    }
    SECTION("initialize with path to a non-existing executable") {
        process p("XXXXXX", nullptr);
        auto init = p.initialize();
        REQUIRE_FALSE(init.has_value());
        REQUIRE(init.error().code ==
                backend::backend_error::spawn_process_failed);
        REQUIRE_FALSE(p.is_process_alive());
    }
}

TEST_CASE_METHOD(BackendFixture, "backend registers read/write data",
                 "[backend] [registers]") {
    SECTION("reading registers") {
        auto regs_ = proc.get_regs();
        REQUIRE(regs_.has_value());
        REQUIRE(regs_->data.size() > 0);
        REQUIRE(proc.get_pc().has_value());
        REQUIRE(*proc.get_pc() != 0x00);
        REQUIRE(proc.get_sp().has_value());
        REQUIRE(*proc.get_sp() != 0);
#if defined(__aarch64__)
        REQUIRE(regs_->data.contains("pc"));
        REQUIRE(regs_->data.at("pc") != 0x00);
#elif defined(__x86_64)
        REQUIRE(regs_->data.contains("rip"));
        REQUIRE(regs_->data.at("rip") != 0x00);
#endif
    }
    SECTION("writing registers") {
        std::uint64_t current_address = *proc.get_pc();
        REQUIRE(proc.set_pc(current_address + 1).has_value());
        REQUIRE(*proc.get_pc() == current_address + 1);
    }
}

TEST_CASE_METHOD(BackendFixture, "backend memory read/write ",
                 "[backend] [io]") {
    std::uint8_t data[8] = {};
    REQUIRE(proc.read_memory(*proc.get_pc(), data, 8).has_value());
    data[3] = 0xFA;
    REQUIRE(proc.write_memory(*proc.get_pc(), data, 8).has_value());
    REQUIRE(proc.read_memory(*proc.get_pc(), data, 8).has_value());
    REQUIRE(data[3] == 0xFA);
}

TEST_CASE_METHOD(BackendFixture, "backend breakpoint operations",
                 "[backend] [io]") {
    SECTION("setting breakpoint at address") {
        long orig;
        auto pc = proc.get_pc();
        REQUIRE(pc.has_value());
        REQUIRE(proc.set_breakpoint(*pc, orig).has_value());
#if defined(__aarch64__)
        std::uint32_t instruction;
        pc = proc.get_pc();
        REQUIRE(pc.has_value());
        REQUIRE(proc.read_memory(*pc, &instruction, sizeof(std::uint32_t))
                    .has_value());
        REQUIRE(instruction == 0xD4200000);
#elif defined(__x86_64)
        std::uint8_t instruction;
        pc = proc.get_pc();
        REQUIRE(pc.has_value());
        REQUIRE(proc.read_memory(*pc, &instruction, sizeof(std::uint8_t))
                    .has_value());
        REQUIRE(instruction == 0xCC);
#endif
    }
}

TEST_CASE_METHOD(BackendFixture, "backend wait/poll operations") {
    long orig;
    auto pc = *proc.get_pc();
    REQUIRE(proc.set_breakpoint(pc, orig).has_value());
    REQUIRE(proc.cont().has_value());
    auto ev = proc.wait();
    REQUIRE(ev.has_value());
    auto *bp = std::get_if<backend::stopped_at_breakpoint>(&*ev);
    REQUIRE(bp);
    std::uint64_t bp_addr = bp->addr;
    REQUIRE(proc.set_pc(bp_addr).has_value());
    REQUIRE(proc.remove_breakpoint(bp_addr, orig).has_value());
    REQUIRE(proc.single_step().has_value());
    auto step_ev = proc.wait();
    REQUIRE(step_ev.has_value());

    REQUIRE(proc.cont().has_value());
    ev = proc.wait();
    REQUIRE(ev.has_value());
    REQUIRE(std::get_if<backend::exited>(&*ev));
}

TEST_CASE_METHOD(BackendFixture, "list threads, get threads informations") {
    auto threads = proc.list_threads();
    REQUIRE(threads.has_value());
    REQUIRE(!threads->empty());
}

TEST_CASE_METHOD(BackendFixture, "thread register and memory operations",
                 "[backend] [thread]") {
    auto threads = proc.list_threads();
    REQUIRE(threads.has_value());
    REQUIRE(threads->size() == 1);
    thread t = threads->at(0);
    REQUIRE(t.id() == proc.get_process_id());
    REQUIRE(t.get_pc().has_value());
    REQUIRE(*t.get_pc() == *proc.get_pc());

    auto regs = t.get_regs();
    REQUIRE(regs.has_value());
    std::uint64_t pc = *t.get_pc();
#if defined(__aarch64__)
    regs->set("pc", pc + 4);
    REQUIRE(t.set_regs(*regs).has_value());
    REQUIRE(*t.get_pc() == pc + 4);
    REQUIRE(t.set_pc(pc).has_value());
#elif defined(__x86_64)
    regs->set("rip", pc + 1);
    REQUIRE(t.set_regs(*regs).has_value());
    REQUIRE(*t.get_pc() == pc + 1);
    REQUIRE(t.set_pc(pc).has_value());
#endif

    std::uint8_t data[8] = {};
    REQUIRE(t.read_memory(*t.get_pc(), data, 8).has_value());
    data[3] = 0xFA;
    REQUIRE(t.write_memory(*t.get_pc(), data, 8).has_value());
    REQUIRE(t.read_memory(*t.get_pc(), data, 8).has_value());
    REQUIRE(data[3] == 0xFA);
}

TEST_CASE_METHOD(BackendFixture, "thread control functions like a backend",
                 "[backend] [thread]") {
    auto threads = proc.list_threads();
    REQUIRE(threads.has_value());
    REQUIRE(threads->size() == 1);
    thread t = threads->at(0);

    long orig;
    REQUIRE(t.set_breakpoint(*t.get_pc(), orig).has_value());
    REQUIRE(t.cont().has_value());
    auto ev = t.wait();
    REQUIRE(ev.has_value());
    auto *bp = std::get_if<backend::stopped_at_breakpoint>(&*ev);
    REQUIRE(bp);
    REQUIRE(bp->addr == *t.get_pc());
    REQUIRE(t.set_pc(bp->addr).has_value());
    REQUIRE(t.remove_breakpoint(bp->addr, orig).has_value());
    REQUIRE(t.single_step().has_value());
    auto step_ev = t.wait();
    REQUIRE(step_ev.has_value());
    REQUIRE(std::get_if<backend::stopped_at_step>(&*step_ev));
    REQUIRE(t.cont().has_value());
    ev = t.wait();
    REQUIRE(ev.has_value());
    REQUIRE(std::get_if<backend::exited>(&*ev));
}

TEST_CASE_METHOD(BackendFixture, "backend poll operations", "[backend]") {
    REQUIRE(proc.get_pc().has_value());
    backend::event none_ev = proc.poll();
    REQUIRE(std::get_if<backend::none>(&none_ev));

    REQUIRE(proc.single_step().has_value());
    backend::event ev;
    for (int i = 0; i < 1000 && !std::get_if<backend::stopped_at_step>(&ev);
         i++)
        ev = proc.poll();
    REQUIRE(std::get_if<backend::stopped_at_step>(&ev));
}

TEST_CASE("kill process", "[backend]") {
    process p(simple_program_path(), nullptr);
    REQUIRE(p.initialize().has_value());
    REQUIRE(p.is_process_alive());
    REQUIRE(backend::kill_process(p.get_process_id()).has_value());
    int status;
    REQUIRE(::waitpid(p.get_process_id(), &status, 0) == p.get_process_id());
    REQUIRE_FALSE(p.is_process_alive());
    REQUIRE_FALSE(p.get_regs().has_value());
}

TEST_CASE("attach to a process by pid", "[backend]") {
    SECTION("attach to a running untraced process") {
        pid_t child = ::fork();
        REQUIRE(child >= 0);
        if (child == 0) {
            execl("/bin/sleep", "sleep", "30", static_cast<char *>(nullptr));
            _exit(1);
        }
        process attached(static_cast<process_id_t>(child));
        auto init = attached.initialize();
        if (!init.has_value())
            ::kill(child, SIGKILL);
        REQUIRE(init.has_value());
        REQUIRE(attached.is_process_alive());
        REQUIRE(attached.get_pc().has_value());
        REQUIRE(attached.get_regs().has_value());
    }
    SECTION("attach to a non-existing process") {
        process invalid(999999);
        auto init = invalid.initialize();
        REQUIRE_FALSE(init.has_value());
        REQUIRE(init.error().code == backend::backend_error::attach_failed);
    }
}

// TODO: Test catch signal when a new thread is created.
