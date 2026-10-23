#include "tui/source_view.hpp"
#include "engine/disassembler.hpp"
#include "engine/process.hpp"
#include "engine/source_loader.hpp"
#include "engine/symbols.hpp"
#include "ftxui/dom/elements.hpp"
#include "tui/line_window.hpp"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <format>
#include <vector>
#include <ftxui/component/component_base.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/dom/deprecated.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/color.hpp>
#include <memory>
#include <utility>

namespace cdbg::tui {

size_t get_digit_count(size_t max_line_num) {
    if (max_line_num == 0) return 1;
    return static_cast<size_t>(std::log10(max_line_num)) + 1;
}

source_view::source_view(std::shared_ptr<engine::symbols> syms, std::shared_ptr<engine::process> proc) : nrows(0), ncols(0), syms(syms), disassembler(std::make_shared<engine::disassembler>()), proc(proc) {}
source_view::source_view(std::size_t nrows, std::size_t ncols, std::shared_ptr<engine::symbols> syms, std::shared_ptr<engine::process> proc) : nrows(nrows), ncols(ncols), syms(syms), disassembler(std::make_shared<engine::disassembler>()), proc(proc) {}
source_view::~source_view() {}
ftxui::Element source_view::Render() {
    if (has_debug_info) {
        return ftxui::hbox(make_line_number(),
                           make_debug_info_source()
        );
    } else {
        return ftxui::hbox(make_address_line(),
                           make_disassembled_source()
        );
    }
}
std::optional<std::size_t> source_view::get_address_index(engine::symbols::address_t pc) {
    auto ins = std::ranges::find_if(instructions, [pc](engine::disassembler::instruction instruction) {
        return instruction.address == pc;
    });
    if (ins != instructions.end()) {
        return std::distance(instructions.begin(), ins);
    }
    return std::nullopt;
}
void source_view::update() {
    auto pc = proc->get_pc();
    if (!pc.has_value()) return;

    auto src = syms->get_source_by_addr(*pc);
    if (src) {
        has_debug_info = true;
        if (!loader_cache.contains(src->file)) {
            loader_cache.emplace(src->file, engine::source_loader(*src));
        } else {
            loader_cache.find(src->file)->second.set_source(*src);
        }
        current_file_ = src->file;
        current_src = *src;
        return;
    }

    // Module may carry DWARF, but this exact address has no line row
    // (PLT/thunk/library code). Fall back to the disassembler.
    has_debug_info = false;
    update_disassembly(*pc);
}

void source_view::update_disassembly(engine::symbols::address_t pc) {
    if (!instructions.empty() && pc == old_instruction_address) return;

    const auto insn_size =
        static_cast<std::size_t>(engine::disassembler::instruction_size());
    const auto count = std::max<std::size_t>(nrows, 1);
    const auto back = (count / 2) * insn_size;
    auto start = pc > back ? pc - back : 0;

    std::vector<std::uint8_t> buf(count * insn_size);
    if (!proc->read_memory(start, buf.data(), buf.size())) {
        start = pc;
        buf.resize(insn_size);
        if (!proc->read_memory(start, buf.data(), buf.size())) return;
    }

    auto code = disassembler->disassemble(start, buf.data(), buf.size());
    if (!code) return;

    instructions = std::move(*code);
    old_instruction_address = pc;
}
std::size_t source_view::get_middle_instruction_line_index() {
    return nrows % 2 ? nrows / 2 + 1 : nrows / 2;
}
void source_view::update_dimensions(std::size_t nrows, std::size_t ncols) {
    this->nrows = nrows;
    this->ncols = ncols;
    update();
}
ftxui::Element source_view::make_address_line() {
    auto pc = this->proc->get_pc();
    if (!pc.has_value()) return ftxui::vbox({});

    ftxui::Elements lines;
    for (const auto &instruction : instructions) {
        auto address = ftxui::text(std::format(" {:#x} ", instruction.address));
        if (instruction.address == *pc) {
            lines.push_back(address | ftxui::bgcolor(ftxui::Color::Orange3) |
                            ftxui::color(ftxui::Color::Black));
        } else {
            lines.push_back(address | ftxui::color(ftxui::Color::DarkOrange3) |
                            ftxui::dim);
        }
    }
    return ftxui::vbox(std::move(lines)) | ftxui::align_right;
}
ftxui::Element source_view::make_disassembled_source() {
    auto pc = this->proc->get_pc();
    if (!pc.has_value()) return ftxui::vbox({});

    ftxui::Elements lines;
    for (const auto &instruction : instructions) {
        auto row = ftxui::text(instruction.mnemonic + " " + instruction.op_str);
        if (instruction.address == *pc) {
            lines.push_back(row | ftxui::bgcolor(ftxui::Color::Orange3) |
                            ftxui::color(ftxui::Color::Black));
        } else {
            lines.push_back(row);
        }
    }
    return ftxui::vbox(std::move(lines)) | ftxui::flex_grow;
}
ftxui::Element source_view::make_line_number() {
    auto it = loader_cache.find(current_file_);
    if (current_file_.empty() || it == loader_cache.end())
        return ftxui::vbox({});

    const auto window =
        make_line_window(current_src.line, nrows, it->second.total_lines());
    if (window.count == 0)
        return ftxui::vbox({});

    ftxui::Elements lines;
    auto digits = get_digit_count(it->second.max_line_number()) + 1;
    const int margin = 2;
    const int cell = digits + margin * 2;

    for (std::size_t row = 0; row < window.count; ++row) {
        auto number = ftxui::text(std::to_string(window.start + row + 1))
            | ftxui::align_right
            | ftxui::size(ftxui::WIDTH, ftxui::EQUAL, digits)
            | ftxui::hcenter
            | ftxui::size(ftxui::WIDTH, ftxui::EQUAL, cell);
        if (window.current_row && *window.current_row == row) {
            lines.push_back(number | ftxui::bgcolor(ftxui::Color::Orange3) |
                            ftxui::color(ftxui::Color::Black));
        } else {
            lines.push_back(number | ftxui::color(ftxui::Color::DarkOrange3) |
                            ftxui::dim);
        }
    }
    return ftxui::vbox(std::move(lines) | ftxui::flex | ftxui::align_right);
}
ftxui::Element source_view::make_debug_info_source() {
    auto it = loader_cache.find(current_file_);
    if (current_file_.empty() || it == loader_cache.end())
        return ftxui::vbox({});

    const auto window =
        make_line_window(current_src.line, nrows, it->second.total_lines());
    if (window.count == 0)
        return ftxui::vbox({});

    ftxui::Elements lines;
    auto txt_lines = it->second.load_lines(window.start, window.end());
    std::size_t row = 0;
    for (auto txt_line : txt_lines) {
        if (window.current_row && *window.current_row == row) {
            lines.push_back(ftxui::text(txt_line) |
                            ftxui::bgcolor(ftxui::Color::Orange3) |
                            ftxui::color(ftxui::Color::Black));
        } else {
            lines.push_back(ftxui::text(txt_line));
        }
        ++row;
    }
    return ftxui::vbox(std::move(lines)) | ftxui::flex_grow;
}
std::string source_view::get_current_file() const {
    return this->current_file_;
}
void source_view::clean_cache(std::vector<engine::symbols::frame_info> &stack_trace) {
    for (auto loader : loader_cache) {
        bool exists{false};
        auto _ = std::find_if(stack_trace.begin(), stack_trace.end(), [&loader, &exists](engine::symbols::frame_info& frame) {
            if (loader.first == frame.location.value().file) {
                exists = true;
                return true;
            }
            return false;
        });
        if (!exists) loader_cache.erase(loader.first);
    }
}
}
