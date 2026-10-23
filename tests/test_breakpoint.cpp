#include "catch2/catch.hpp"
#include "engine/breakpoint.hpp"

using namespace cdbg;

TEST_CASE("Breakpoint initialization and operations", "[breakpoint]") {
    engine::breakpoint bp(0x401136, 0xFA, false);
    bp.set_id(10);
    SECTION("is disabled") { REQUIRE_FALSE(bp.is_enabled()); }
    SECTION("is enabled") {
        bp.activate();
        REQUIRE(bp.is_enabled());
    }
    SECTION("is address not null") { REQUIRE(bp.get_addr() != 0x00); }
    SECTION("address is set") { REQUIRE(bp.get_addr() == 0x401136); }
    SECTION("id is set") { REQUIRE(bp.get_id() == 10); }
    SECTION("instruction is saved") {
        REQUIRE(bp.get_orig_instruction() == 0xFA);
    }
}
