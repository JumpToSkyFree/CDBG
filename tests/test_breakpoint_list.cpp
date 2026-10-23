#include "catch2/catch.hpp"
#include <engine/breakpoint_list.hpp>

TEST_CASE("breakpoint_list lifecycle", "[breakpoint]") {
    cdbg::engine::breakpoint_list list;
    std::uint64_t address = 0x401136;
    auto *bp = list.add(address, 0xCC, true);
    SECTION("can be found by address") {
        auto _bp = list.find_by_addr(0x401136);
        REQUIRE(_bp);
        REQUIRE(&*_bp == bp);
    }
    SECTION("can be found by id") {
        auto _bp = list.find_by_id(0);
        REQUIRE(_bp);
        REQUIRE(&*_bp == bp);
    }
    SECTION("removed") {
        list.remove(0);
        REQUIRE(list.find_by_id(0) == nullptr);
    }
}
