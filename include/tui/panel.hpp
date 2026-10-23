#ifndef CDBG_PANEL_HPP
#define CDBG_PANEL_HPP

namespace cdbg::tui {
class Panel {
    public:
        Panel();
        ~Panel();
        Panel(const std::string& name);
};
}

#endif // CDBG_PANEL_HPP
