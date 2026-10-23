#include <catch2/catch.hpp>
#include <cstddef>
#include <tui/line_window.hpp>

using namespace cdbg::tui;

TEST_CASE("line window keeps the current line visible", "[line_window]") {
    SECTION("top of file") {
        auto w = make_line_window(1, 40, 120);
        REQUIRE(w.start == 0);
        REQUIRE(w.count == 40);
        REQUIRE(w.end() == 40);
        REQUIRE(w.current_row.has_value());
        REQUIRE(*w.current_row == 0);
    }
    SECTION("middle of file centers the current line") {
        auto w = make_line_window(36, 40, 120);
        REQUIRE(w.start == 15);
        REQUIRE(w.count == 40);
        REQUIRE(w.end() == 55);
        REQUIRE(w.current_row.has_value());
        REQUIRE(*w.current_row == 20);
        REQUIRE(w.start + *w.current_row == 35);
    }
    SECTION("window is clamped to the end of the file") {
        auto w = make_line_window(36, 40, 20);
        REQUIRE(w.count <= 20);
        REQUIRE(w.start + w.count == 20);
        REQUIRE(w.current_row.has_value());
        REQUIRE(*w.current_row < w.count);
    }
    SECTION("current line beyond EOF stays visible") {
        auto w = make_line_window(150, 40, 20);
        REQUIRE(w.start + w.count == 20);
        REQUIRE(w.current_row.has_value());
        REQUIRE(*w.current_row < w.count);
    }
    SECTION("zero rows yields an empty window") {
        auto w = make_line_window(10, 0, 100);
        REQUIRE(w.count == 0);
        REQUIRE_FALSE(w.current_row.has_value());
    }
    SECTION("unknown current line has no highlight") {
        auto w = make_line_window(0, 10, 100);
        REQUIRE(w.start == 0);
        REQUIRE(w.count == 10);
        REQUIRE_FALSE(w.current_row.has_value());
    }
    SECTION("file shorter than the window") {
        auto w = make_line_window(3, 40, 5);
        REQUIRE(w.start == 0);
        REQUIRE(w.count == 5);
        REQUIRE(w.current_row.has_value());
        REQUIRE(*w.current_row == 2);
    }
}
