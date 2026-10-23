#include <algorithm>
#include <cassert>
#include <expected>
#include <ftxui/component/component.hpp>
#include <ftxui/screen/screen.hpp>
#include <ftxui/screen/terminal.hpp>
#include <variant>
#include "tui/debugger.hpp"
#include "engine/backend.hpp"
#include "engine/process.hpp"
#include "engine/symbols.hpp"
#include "tui/source_view.hpp"

namespace cdbg::tui {
constexpr int k_cell_aspect = 2;
debugger_tui::debugger_tui(engine::process&& _proc) : proc(std::make_shared<engine::process>(std::move(_proc))), syms(std::make_shared<engine::symbols>(this->proc->get_process_id())), source_view(syms, proc){
    assert(syms->initialize().has_value());
    auto main_func = syms->get_function_by_name("main");
    assert(main_func.has_value());
    long orig;
    auto _ = this->proc->set_breakpoint(main_func->low, orig);
    assert(this->proc->cont().has_value());
    auto event = this->proc->wait();
    assert(event.has_value());
    auto *bp = std::get_if<engine::backend::stopped_at_breakpoint>(&*event);
    assert(bp);
    _ = this->proc->remove_breakpoint(main_func->low, orig);
    auto pc = proc->get_pc();
    if (!pc.has_value()) {
        screen_.Exit();
        return;
    }
    source_view.update();

    root_ = ftxui::Renderer([this]() -> ftxui::Element {
        ftxui::Dimensions term = ftxui::Terminal::Size();
        int avail_rows = std::max(3, int(term.dimy * 0.7));
        int avail_cols = std::max(3, int(term.dimx * 0.7));
        int rows_by_width = avail_cols / k_cell_aspect;
        int side_rows = std::max(3, std::min(avail_rows, rows_by_width));
        int side_cols = side_rows * k_cell_aspect;
        source_view.update_dimensions(side_rows, side_cols);

        std::string title = "Disassembler";
        if (auto pc = proc->get_pc()) {
            if (auto src = syms->get_source_by_addr(*pc))
                title = src->file;
        }

        ftxui::Element view =
            ftxui::window(ftxui::text(" " + title + " "),
                          ftxui::vbox(source_view.Render()) | ftxui::yframe) |
            ftxui::color(ftxui::Color::Orange3) |
            ftxui::size(ftxui::WIDTH, ftxui::EQUAL, side_cols) |
            ftxui::size(ftxui::HEIGHT, ftxui::EQUAL, side_rows) |
            ftxui::center;

        if (!last_error_)
            return view;

        return ftxui::vbox(
            std::move(view),
            ftxui::text(" error: " + *last_error_) |
            ftxui::color(ftxui::Color::Red));
    });
    root_ = ftxui::CatchEvent(root_, [this](ftxui::Event e) -> bool {
        if (e == ftxui::Event::Escape || e == ftxui::Event::Character('q')) {
            screen_.Exit();
            return false;
        }

        if (e == ftxui::Event::Character('i')) {
            last_error_.reset();
            if (auto status = proc->single_step(); !status) {
                last_error_ = "step failed";
                return false;
            }
            auto event = proc->wait();
            if (!event) {
                last_error_ = "wait failed";
                return false;
            }
            if (std::get_if<engine::backend::exited>(&*event) ||
                std::get_if<engine::backend::signaled>(&*event)) {
                screen_.Exit();
                return false;
            }
            source_view.update();
            return false;
        }

        const bool step_over = e == ftxui::Event::Character('n');
        const bool step_into = e == ftxui::Event::Character('s');
        if (!step_over && !step_into)
            return false;

        last_error_.reset();
        auto result = step_over ? proc->step_over_source(syms)
                                : proc->single_step_source(syms);
        if (!result) {
            last_error_ =
                "step failed (error " +
                std::to_string(static_cast<int>(result.error().code)) + ")";
            source_view.update();
            return false;
        }
        if (std::get_if<engine::backend::exited>(&result->event) ||
            std::get_if<engine::backend::signaled>(&result->event)) {
            screen_.Exit();
            return false;
        }

        source_view.update();
        return false;
    });
}
debugger_tui::~debugger_tui() {}
std::expected<void, engine::error<debugger_tui::debugger_error>>
debugger_tui::run_loop() {
    screen_.Loop(root_);
    return {};
}
}
