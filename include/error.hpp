#ifndef CDBG_ERROR_HPP
#define CDBG_ERROR_HPP

#include <string>
#include <optional>

namespace cdbg::engine {

struct native_error_info {
    int native_code;
    std::string native_message;
    std::string source_call;
};

template<class T>
struct error {
    T code;
    std::optional<native_error_info> native;
};
}

#endif // CDBG_ERROR_HPP
