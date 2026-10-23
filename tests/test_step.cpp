#include "helpers.hpp"
#include <catch2/catch.hpp>
#include <cstddef>
#include <engine/backend.hpp>
#include <engine/process.hpp>
#include <engine/symbols.hpp>
#include <memory>
#include <variant>

using namespace cdbg::engine;

TEST_CASE("single_step_source advances through complex program without failing",
          "[step] [integration]") {
    process proc(complex_program_path(), nullptr);
    REQUIRE(proc.initialize().has_value());

    auto syms = std::make_shared<symbols>(proc.get_process_id());
    REQUIRE(syms->initialize().has_value());

    auto main_func = syms->get_function_by_name("main");
    REQUIRE(main_func.has_value());

    long orig;
    REQUIRE(proc.set_breakpoint(main_func->low, orig).has_value());
    REQUIRE(proc.cont().has_value());
    auto event = proc.wait();
    REQUIRE(event.has_value());
    auto *bp = std::get_if<backend::stopped_at_breakpoint>(&*event);
    REQUIRE(bp);
    REQUIRE(proc.set_pc(bp->addr).has_value());
    REQUIRE(proc.remove_breakpoint(bp->addr, orig).has_value());

    std::size_t source_stops = 0;
    bool reached_exit = false;
    for (std::size_t i = 0; i < 64 && !reached_exit; ++i) {
        auto result = proc.single_step_source(syms);
        REQUIRE(result.has_value());
        if (std::get_if<backend::exited>(&result->event))
            reached_exit = true;
        if (result->location)
            ++source_stops;
    }

    // Stepping must make progress: it should land on real source lines even
    // when a call temporarily enters code without line rows (PLT/libc).
    REQUIRE(source_stops > 0);
}

TEST_CASE("step_over_source skips called functions", "[step] [integration]") {
    process proc(complex_program_path(), nullptr);
    REQUIRE(proc.initialize().has_value());

    auto syms = std::make_shared<symbols>(proc.get_process_id());
    REQUIRE(syms->initialize().has_value());

    auto main_func = syms->get_function_by_name("main");
    REQUIRE(main_func.has_value());

    long orig;
    REQUIRE(proc.set_breakpoint(main_func->low, orig).has_value());
    REQUIRE(proc.cont().has_value());
    auto event = proc.wait();
    REQUIRE(event.has_value());
    auto *bp = std::get_if<backend::stopped_at_breakpoint>(&*event);
    REQUIRE(bp);
    REQUIRE(proc.set_pc(bp->addr).has_value());
    REQUIRE(proc.remove_breakpoint(bp->addr, orig).has_value());

    // Walk (step-into) to the `fill(values, 8);` call on line 98.
    bool at_fill_call = false;
    for (int i = 0; i < 200 && !at_fill_call; ++i) {
        auto pc = proc.get_pc();
        REQUIRE(pc.has_value());
        auto src = syms->get_source_by_addr(*pc);
        if (src && src->line == 98) {
            at_fill_call = true;
            break;
        }
        auto step = proc.single_step_source(syms);
        REQUIRE(step.has_value());
        REQUIRE_FALSE(std::get_if<backend::exited>(&step->event));
    }
    REQUIRE(at_fill_call);

    // Step over the call: no stop inside fill, stay in main.
    auto over = proc.step_over_source(syms);
    REQUIRE(over.has_value());
    REQUIRE_FALSE(std::get_if<backend::exited>(&over->event));

    auto pc = proc.get_pc();
    REQUIRE(pc.has_value());
    auto func = syms->get_function_by_addr(*pc);
    REQUIRE(func.has_value());
    REQUIRE(func->name == "main");

    auto src = syms->get_source_by_addr(*pc);
    REQUIRE(src.has_value());
    REQUIRE(src->line > 98);
}
