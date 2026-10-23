#include "engine/source_loader.hpp"
#include <fstream>
#include <generator>

namespace cdbg::engine {
source_loader::source_loader() {}
source_loader::source_loader(const symbols::source &src) : src(src) {
    std::ifstream file_stream(src.file, std::ios::binary);
    if (!file_stream.is_open()) return;

    line_offsets.push_back(file_stream.tellg());
    std::string dummy;
    while (std::getline(file_stream, dummy)) {
        line_offsets.push_back(file_stream.tellg());
    }

    if (!line_offsets.empty() && file_stream.eof()) {
        line_offsets.pop_back();
    }
}
void source_loader::set_source(const symbols::source &src) {
    this->src = src;
}
std::generator<std::string> source_loader::load_lines(std::size_t start, std::size_t end) {
    if (start >= end) co_return;
    std::ifstream file(src.file);
    if (!file.is_open()) co_return;

    std::string line;
    for (std::size_t current_line{0}; std::getline(file, line); ++current_line) {
        if (current_line >= end) co_return;
        if (current_line >= start) co_yield line;
    }
}
size_t source_loader::total_lines() const {
    return line_offsets.size();
}
size_t source_loader::max_line_number() const {
    return line_offsets.empty() ? 0 : line_offsets.size() - 1;
}
}
