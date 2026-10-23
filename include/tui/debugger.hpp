#ifndef CDBG_TUI_DEBUGGER_HPP
#define CDBG_TUI_DEBUGGER_HPP

#include "engine/process.hpp"
#include "error.hpp"
#include "tui/source_view.hpp"
#include <ftxui/component/component_base.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/deprecated.hpp>
#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>
#include <expected>
#include <optional>
#include <string>

namespace cdbg::tui {
class debugger_tui {
    public:
        enum debugger_error {
            tui_failed_init
        };

        debugger_tui(engine::process&& proc);
        ~debugger_tui();

        debugger_tui(const debugger_tui&) = delete;
        debugger_tui& operator=(const debugger_tui&) = delete;

        std::expected<void, engine::error<debugger_error>> run_loop();

    private:
        ftxui::Element win;
        ftxui::ScreenInteractive screen_ = ftxui::ScreenInteractive::Fullscreen();
        ftxui::Component root_;
        std::shared_ptr<engine::process> proc;
        std::shared_ptr<engine::symbols> syms;
        tui::source_view source_view;
        bool has_debug_info{false};
        std::optional<std::string> last_error_;
};
}

#endif // CDBG_TUI_DEBUGGER_HPP
