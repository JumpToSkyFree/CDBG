#include "helpers.hpp"
#include <catch2/catch.hpp>
#include <cstdint>
#include <cstring>
#include <engine/backend.hpp>
#include <engine/symbols.hpp>
#include <fmt/base.h>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

using namespace cdbg::engine;

TEST_CASE_METHOD(SymbolsFixture,
                 "check if program contains debug info for a stripped binary",
                 "[symbols] [integration]") {
    REQUIRE(syms->has_debug_info());
}
TEST_CASE_METHOD(SymbolsFixture, "list modules and get specific module",
                 "[symbols] [module]") {
    auto modules = syms->list_modules();
    REQUIRE(modules.has_value());
    REQUIRE(modules->size() > 0);
    SECTION("list modules") { REQUIRE(modules->size() > 0); }
    SECTION("get module by name") {
        auto module =
            syms->get_module_by_name(modules->at(0).module_name);
        REQUIRE(module.has_value());
        REQUIRE(module->high_address == modules->at(0).high_address);
    }
    SECTION("get module by address falling in range") {
        auto pc = proc.get_pc();
        REQUIRE(pc.has_value());
        auto module = syms->get_module_by_addr(pc.value());
        REQUIRE(module.has_value());
        REQUIRE(module->info_level == symbols::debug_level_info::full);
    }
}
TEST_CASE_METHOD(SymbolsFixture, "get source from address",
                 "[symbols] [source]") {
    auto pc = proc.get_pc();
    REQUIRE(pc.has_value());
    auto source = syms->get_source_by_addr(pc.value());
    REQUIRE(source.has_value());
}
TEST_CASE_METHOD(SymbolsFixture, "function resolution, get function by name",
                 "[symbols] [function]") {
    auto func = syms->get_function_by_name("test_const");
    REQUIRE(func.has_value());
    REQUIRE(func->return_type.is_constant);
    REQUIRE(func->return_type.name == "int");
}
TEST_CASE_METHOD(SymbolsFixture, "function resolution, get function by address",
                 "[symbols] [function]") {
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
    REQUIRE(proc.single_step().has_value());
    auto step_ev = proc.wait();
    REQUIRE(std::get_if<backend::stopped_at_step>(&*step_ev));
    auto pc = proc.get_pc();
    REQUIRE(pc.has_value());
    auto _func = syms->get_function_by_addr(*pc);
    REQUIRE(_func.has_value());
    REQUIRE(_func->name == "main");
    REQUIRE(_func->return_type.name == "int");
}
TEST_CASE_METHOD(SymbolsFixture,
                 "variables resolution, get variables by address",
                 "[symbols] [variable]") {
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
    REQUIRE(proc.single_step().has_value());
    auto step_ev = proc.wait();
    REQUIRE(std::get_if<backend::stopped_at_step>(&*step_ev));
    auto pc = proc.get_pc();
    REQUIRE(pc.has_value());
    auto variables =
        syms->get_all_variables_at_address(*proc.get_pc(), proc.get_process_id());
    REQUIRE(variables.has_value());
    REQUIRE(variables->size() > 0);
    REQUIRE(variables->at(0).name == "var");
    REQUIRE(variables->at(0).type.name == "int");
}
TEST_CASE_METHOD(SymbolsFixture,
                 "parameter resolution, get parameters by address",
                 "[symbols] [parameter]") {
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
    REQUIRE(proc.single_step().has_value());
    auto step_ev = proc.wait();
    REQUIRE(std::get_if<backend::stopped_at_step>(&*step_ev));
    auto pc = proc.get_pc();
    REQUIRE(pc.has_value());
    auto parameters = syms->get_all_parameters_at_address(*proc.get_pc(),
                                                         proc.get_process_id());
    REQUIRE(parameters.has_value());
    REQUIRE(parameters->size() > 0);
    REQUIRE(parameters->at(0).name == "argc");
    REQUIRE(parameters->at(0).size > 0);
    REQUIRE(parameters->at(0).type.name == "int");
    REQUIRE(parameters->at(0).address > 0);
    REQUIRE(parameters->at(1).name == "argv");
    REQUIRE(parameters->at(1).type.name == "char");
    REQUIRE(parameters->at(1).size > 0);
    REQUIRE(parameters->at(1).type.is_pointer);
}

TEST_CASE_METHOD(SymbolsFixture,
                 "resolve a local parameter's value at a breakpoint",
                 "[symbols] [variable] [value]") {
    auto func = syms->get_function_by_name("test_ref");
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

    const int str_decl_line = 6;
    bool inside = false;
    for (int i = 0; i < 64 && !inside; i++) {
        REQUIRE(proc.single_step().has_value());
        auto step_ev = proc.wait();
        REQUIRE(std::get_if<backend::stopped_at_step>(&*step_ev));
        auto pc = proc.get_pc();
        REQUIRE(pc.has_value());
        if (*pc < func->low || *pc >= func->high)
            break;
        auto src = syms->get_source_by_addr(*pc);
        if (src && src->line > str_decl_line)
            inside = true;
    }
    REQUIRE(inside);

    auto params = syms->get_all_parameters_at_address(*proc.get_pc(),
                                                      proc.get_process_id());
    REQUIRE(params.has_value());
    REQUIRE(params->size());

    auto it = std::find_if(params->begin(), params->end(),
                           [](auto &param) { return param.name == "str"; });
    REQUIRE(it != params->end());
    REQUIRE(it->address != 0);
    REQUIRE(it->value.has_value());
    REQUIRE(it->type.is_pointer);
    REQUIRE(it->value->size() == sizeof(std::uint64_t));

    std::uint64_t str_addr{};
    std::memcpy(&str_addr, it->value->data(), sizeof(str_addr));
    REQUIRE(str_addr != 0);
    std::vector<std::uint8_t> buf(16);
    REQUIRE(proc.read_memory(str_addr, buf.data(), buf.size()).has_value());
    std::string s(reinterpret_cast<const char *>(buf.data()));
    REQUIRE(s == "HELLO WORLD");
}

TEST_CASE_METHOD(SymbolsFixture,
                 "resolve a local variable's value at a breakpoint",
                 "[symbols][variable][integration]") {
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

    const int x_decl_line = 12;
    bool initialised = false;
    for (int i = 0; i < 64 && !initialised; i++) {
        REQUIRE(proc.single_step().has_value());
        auto step_ev = proc.wait();
        REQUIRE(std::get_if<backend::stopped_at_step>(&*step_ev));
        auto pc = proc.get_pc();
        REQUIRE(pc.has_value());
        auto src = syms->get_source_by_addr(*pc);
        if (src && src->line > x_decl_line)
            initialised = true;
    }
    REQUIRE(initialised);

    auto vars = syms->get_all_variables_at_address(*proc.get_pc(),
                                                   proc.get_process_id());
    REQUIRE(vars.has_value());
    REQUIRE(vars->size());

    auto it = std::find_if(vars->begin(), vars->end(),
                           [](auto &v) { return v.name == "x"; });
    REQUIRE(it != vars->end());
    REQUIRE(it->address != 0);
    REQUIRE(it->value.has_value());
    REQUIRE(it->value->size() == it->size);
    REQUIRE((*it->value)[0] == 0x78);
    REQUIRE((*it->value)[1] == 0x00);
}

TEST_CASE_METHOD(SymbolsFixture, "get address by source",
                 "[symbols][source][breakpoint]") {
    symbols::source src = {.file = "simple_program.cpp", .line = 7, .pos = 0};
    auto addr = syms->get_address_by_source(src);
    REQUIRE(addr.has_value());
    REQUIRE(*addr != 0x00);

    auto back = syms->get_source_by_addr(*addr);
    REQUIRE(back.has_value());
    REQUIRE(back->line == 7);
    REQUIRE(back->file ==
            "/home/jumptoskyfree/Projects/CDBG/tests/simple_program.cpp");
}

TEST_CASE_METHOD(SymbolsFixture, "get_stack resolves caller through callee",
                 "[symbols][stack][integration]") {
    auto func = syms->get_function_by_name("test_ref");
    REQUIRE(func.has_value());

    long orig;
    REQUIRE(proc.set_breakpoint(func->low, orig).has_value());
    REQUIRE(proc.cont().has_value());
    auto ev = proc.wait();
    REQUIRE(ev.has_value());
    REQUIRE(std::get_if<backend::stopped_at_breakpoint>(&*ev));
    REQUIRE(proc.remove_breakpoint(func->low, orig).has_value());

    auto frames = syms->get_stack(proc.get_process_id());
    REQUIRE(frames.has_value());
    REQUIRE(frames->size() >= 2);
    REQUIRE(frames->at(0).function_name == "test_ref");
    REQUIRE(frames->at(1).function_name == "main");
}

TEST_CASE_METHOD(SymbolsFixture, "list all functions in module", "[symbols][module][function]") {
    auto modules = syms->list_modules();
    REQUIRE(modules.has_value());
    REQUIRE(modules->size() > 0);
    auto functions = syms->list_functions_in_module(modules.value().at(0));
    REQUIRE(functions.has_value());
    REQUIRE(functions.value().size() > 0);
    REQUIRE(functions.value().at(0).params.size());
}
