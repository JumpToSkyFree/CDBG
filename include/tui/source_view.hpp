#ifndef CDBG_SOURCE_VIEW_HPP
#define CDBG_SOURCE_VIEW_HPP

#include "engine/disassembler.hpp"
#include "engine/process.hpp"
#include "engine/symbols.hpp"
#include "engine/source_loader.hpp"
#include <ftxui/component/component.hpp>
#include <ftxui/component/component_base.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>


namespace cdbg::tui {

class source_view : ftxui::ComponentBase {
    public:
        source_view(std::shared_ptr<engine::symbols> syms, std::shared_ptr<engine::process> proc);
        source_view(std::size_t nrows, std::size_t ncols, std::shared_ptr<engine::symbols> syms, std::shared_ptr<engine::process> proc);
        ~source_view();

        ftxui::Element Render() override;
        // bool OnEvent(ftxui::Event event) override;
        bool Focusable() const override { return true; };

        // FIX: must return sucess/error if the cache can't be cleared or updated.
        void update();
        void update_disassembly(engine::symbols::address_t pc);
        void update_dimensions(std::size_t nrows, std::size_t ncols);
        std::string get_current_file() const;

    private:
        ftxui::Element make_line_number();
        ftxui::Element make_assembly_view();
        ftxui::Element make_debug_info_source();
        ftxui::Element make_address_line();
        ftxui::Element make_disassembled_source();
        std::optional<std::size_t> get_address_index(engine::symbols::address_t pc);
        std::size_t get_middle_instruction_line_index();
        void clean_cache(std::vector<engine::symbols::frame_info>& stack_trace);

        std::size_t nrows{0}, ncols{0};
        std::unordered_map<std::string, engine::source_loader> loader_cache;
        engine::symbols::source current_src;
        std::string current_file_;
        std::shared_ptr<engine::symbols> syms;
        std::shared_ptr<engine::disassembler> disassembler;
        std::shared_ptr<engine::process> proc;
        std::vector<engine::disassembler::instruction> instructions;
        bool has_debug_info{false};
        engine::symbols::address_t old_instruction_address{0};
};
}

#endif // CDBG_SOURCE_VIEW_HPP
