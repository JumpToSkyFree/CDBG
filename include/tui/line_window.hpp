#ifndef CDBG_TUI_LINE_WINDOW_HPP
#define CDBG_TUI_LINE_WINDOW_HPP

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace cdbg::tui {

// Describes which source lines are visible in a fixed-height viewport and
// which row within that viewport corresponds to the current line. Using one
// source of truth for the gutter and the source text keeps them aligned.
struct line_window {
    std::size_t start{0};                   // first visible 0-based line
    std::size_t count{0};                   // number of rows to render
    std::optional<std::size_t> current_row; // highlight row within the window

    std::size_t end() const { return start + count; } // half-open
};

// current_line is 1-based; 0 means "unknown" (no highlight).
// total_lines clamps the window so the gutter and the source text always
// render the same number of rows, even near the end of the file.
inline line_window make_line_window(std::uint32_t current_line,
                                    std::size_t rows,
                                    std::size_t total_lines) {
    if (rows == 0 || total_lines == 0)
        return {};

    const std::size_t line0 = current_line ? current_line - 1 : 0;
    const std::size_t half = rows / 2;
    std::size_t start = line0 > half ? line0 - half : 0;

    if (start >= total_lines)
        start = total_lines > rows ? total_lines - rows : 0;

    const std::size_t count = std::min(rows, total_lines - start);

    std::optional<std::size_t> current_row;
    if (current_line)
        current_row = std::min(line0 - start, count - 1);

    return line_window{start, count, current_row};
}

} // namespace cdbg::tui

#endif // CDBG_TUI_LINE_WINDOW_HPP
