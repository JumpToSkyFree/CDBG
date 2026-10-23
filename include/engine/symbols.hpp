#ifndef CDBG_SYMBOLS_HPP
#define CDBG_SYMBOLS_HPP
#include "platform_types.hpp"
#include <cstddef>
#include <cstdint>
#include <engine/backend.hpp>
#include <error.hpp>
#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <vector>
namespace cdbg::engine {
class symbols {
    public:
        enum debug_level_info { empty, symbols_only, full };

        using address_t = std::uint64_t;
        struct source {
            std::string file;
            std::uint32_t line;
            std::int32_t pos;
        };
        struct module {
            module();
            ~module();
            module(const module &) = delete;
            module &operator=(const module &) = delete;
            module(module &&) = default;
            module &operator=(module &&) = default;

            std::string path;
            std::string module_name;
            address_t load_address{0};
            address_t low_address{0};
            address_t high_address{0};
            debug_level_info info_level{debug_level_info::empty};

            struct native_handler;
            std::unique_ptr<native_handler> native_handler_;
        };
        using modules = std::vector<module>;

        enum symbols_error {
            reading_debug_info_failed,
            symbols_not_initialized,
            module_not_found,
            function_not_found,
            source_not_found,
            registers_unavailable,
            frame_unwind_failed,
        };

        struct _type {
            bool is_pointer{};
            bool is_constant{};
            bool is_reference{};
            bool is_rvalue_reference{};
            bool is_struct{};
            std::string name;
        };

        struct variable {
            std::string name;
            address_t address;
            _type type;
            std::size_t size;
            std::optional<std::vector<std::uint8_t>> value;
        };

        struct parameter {
            std::string name;
            address_t address;
            _type type;
            std::size_t size;
            std::optional<std::vector<std::uint8_t>> value;
        };

        struct function {
            std::string name;
            _type return_type;
            address_t low, high;
            std::vector<parameter> params;
            std::optional<std::vector<std::uint8_t>> value;
        };

        struct frame_info {
            std::uint64_t pc;
            std::string function_name;
            std::optional<std::uint64_t> frame_base;
            std::optional<source> location;
        };

        using symbol = std::variant<variable, function, parameter>;

        symbols(const symbols &) = delete;
        symbols(symbols &&) = delete;
        symbols &operator=(const symbols &) = delete;
        symbols &operator=(symbols &&) = delete;

        explicit symbols(process_id_t pid);
        std::expected<void, error<symbols_error>> initialize();
        std::expected<modules, error<symbols_error>> list_modules();
        std::expected<module, error<symbols_error>>
        get_module_by_name(std::string_view module);
        std::expected<std::vector<function>, error<symbols_error>> list_functions_in_module(const module& module);
        std::expected<module, error<symbols_error>>
        get_module_by_addr(address_t addr);
        std::expected<source, error<symbols_error>>
        get_source_by_addr(address_t addr);
        std::expected<function, error<symbols_error>>
        get_function_by_name(std::string_view name);
        std::expected<function, error<symbols_error>>
        get_function_by_addr(address_t addr);
        std::expected<std::vector<variable>, error<symbols_error>>
        get_all_variables_at_address(symbols::address_t pc, process_id_t tid);
        std::expected<std::vector<parameter>, error<symbols_error>>
        get_all_parameters_at_address(symbols::address_t pc, process_id_t tid);
        std::expected<address_t, error<symbols_error>>
        get_address_by_source(const source &src);
        bool has_debug_info() const;
        bool has_debug_info(address_t pc) const;
        std::expected<std::vector<frame_info>, error<symbols_error>>
        get_stack(process_id_t id);

        ~symbols();

    private:
        struct impl;
        std::unique_ptr<impl> impl_;
};
} // namespace cdbg::engine
#endif
