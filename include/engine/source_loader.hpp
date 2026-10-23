#ifndef CDBG_SOURCE_LOADER_HPP
#define CDBG_SOURCE_LOADER_HPP

#include <cstddef>
#include <engine/symbols.hpp>
#include <generator>
#include <string>
#include <ftxui/component/component.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/dom/node.hpp>

namespace cdbg::engine {

class source_loader {
    public:
        source_loader();
        explicit source_loader(const symbols::source& src);

        void set_source(const symbols::source& src);
        std::generator<std::string> load_lines(std::size_t start, std::size_t end);
        // TODO: load lines arranged into one string for parsing, color oprations.
        size_t total_lines() const;
        size_t max_line_number() const;
    private:
        symbols::source src;
        std::vector<std::streampos> line_offsets;

};
}

#endif // CDBG_SOURCE_LOADER_HPP
