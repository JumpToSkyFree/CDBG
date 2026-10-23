#include "helpers.hpp"
#include <bits/floatn.h>
#include <catch2/catch.hpp>
#include <engine/backend.hpp>
#include <engine/process.hpp>
#include <fmt/base.h>
#include <sys/wait.h>
#include <engine/source_loader.hpp>
#include <unistd.h>

using namespace cdbg::engine;

TEST_CASE_METHOD(SymbolsFixture, "initialize source loader", "[symbols]") {
    auto func = syms->get_function_by_name("main");
    REQUIRE(func.has_value());
    long orig;
    REQUIRE(proc.set_breakpoint(func->low, orig).has_value());
    REQUIRE(proc.cont().has_value());
    auto ev = proc.wait();
    REQUIRE(ev.has_value());
    auto *bp = std::get_if<backend::stopped_at_breakpoint>(&*ev);
    REQUIRE(bp);
    REQUIRE(proc.set_pc(bp->addr).has_value());
    REQUIRE(proc.remove_breakpoint(bp->addr, orig).has_value());

    auto pc = proc.get_pc();
    REQUIRE(pc.has_value());
    auto src = syms->get_source_by_addr(*pc);
    REQUIRE(src.has_value());

    source_loader loader(src.value());
    auto lines = loader.load_lines(0, 17);
    std::vector<std::string> collected;
    for (std::string line : lines) collected.push_back(std::move(line));
    REQUIRE(collected.size());
}
