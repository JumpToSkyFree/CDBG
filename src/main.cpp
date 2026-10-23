#include "engine/process.hpp"
#include "popl.hpp"
#include "tui/debugger.hpp"
#include <cassert>
#include <iostream>
#include <string>

int main(int argc, char *argv[]) {
    popl::OptionParser op("Allowed options");
    bool is_verbose{false};
    std::string executable_path;
    auto help_option =
        op.add<popl::Switch>("h", "help", "produce help message");
    auto verbose_option =
        op.add<popl::Switch>("v", "verbose", "be verbose", &is_verbose);
    auto executable_path_option = op.add<popl::Value<std::string>>(
        "", "path", "executable path", "", &executable_path);
    op.parse(argc, argv);
    if (help_option->count() == 1)
        std::cout << op << "\n";
    else if (help_option->count() == 2)
        std::cout << op.help(popl::Attribute::advanced) << "\n";
    else if (help_option->count() > 2)
        std::cout << op.help(popl::Attribute::expert) << "\n";

    if (!executable_path.empty()) {
        cdbg::engine::process process(executable_path, nullptr);
        auto init = process.initialize();
        assert(init.has_value() && process.is_initialized() && process.is_process_alive());
        cdbg::tui::debugger_tui dbg(std::move(process));
        auto res = dbg.run_loop();
    }

    return EXIT_SUCCESS;
}
