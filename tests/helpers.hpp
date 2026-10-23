#include "engine/symbols.hpp"
#include <catch2/catch.hpp>
#include <cstdlib>
#include <engine/process.hpp>
#include <memory>
#include <string>

inline std::string simple_program_path() {
    const char *path = std::getenv("SIMPLE_PROGRAM_PATH");
    REQUIRE(path != nullptr);
    return path;
}

inline std::string complex_program_path() {
    const char *path = std::getenv("COMPLEX_PROGRAM_PATH");
    REQUIRE(path != nullptr);
    return path;
}

class BackendFixture {
  protected:
    cdbg::engine::process proc;
    BackendFixture() : proc(simple_program_path(), nullptr) {
        REQUIRE(proc.initialize().has_value());
    }
};

class SymbolsFixture : public BackendFixture {
  protected:
    std::unique_ptr<cdbg::engine::symbols> syms;
    SymbolsFixture() {
        syms = std::make_unique<cdbg::engine::symbols>(proc.get_process_id());
        REQUIRE(syms->initialize().has_value());
    }
};