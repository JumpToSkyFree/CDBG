#include "platform_types.hpp"
#include <memory>
#include <string_view>
#if defined(__linux__)
#include <engine/internal/symbols_linux_impl.hpp>
#endif
#include <engine/symbols.hpp>

namespace cdbg::engine {
symbols::module::module() {}
symbols::module::~module() {}
symbols::symbols(process_id_t pid) : impl_(std::make_unique<impl>(pid)) {}
std::expected<void, error<symbols::symbols_error>> symbols::initialize() {
    return this->impl_->initialize();
}
bool symbols::has_debug_info() const { return this->impl_->has_debug_info(); }
bool symbols::has_debug_info(symbols::address_t pc) const { return this->impl_->has_debug_info(pc); }
std::expected<symbols::modules, error<symbols::symbols_error>>
symbols::list_modules() {
    return this->impl_->list_modules();
}
std::expected<symbols::module, error<symbols::symbols_error>>
symbols::get_module_by_name(std::string_view module) {
    return this->impl_->get_module_by_name(module);
}
std::expected<symbols::module, error<symbols::symbols_error>>
symbols::get_module_by_addr(address_t addr) {
    return this->impl_->get_module_by_addr(addr);
}
std::expected<symbols::source, error<symbols::symbols_error>>
symbols::get_source_by_addr(address_t addr) {
    return this->impl_->get_source_by_addr(addr);
}
std::expected<symbols::function, error<symbols::symbols_error>>
symbols::get_function_by_name(std::string_view name) {
    return this->impl_->get_function_by_name(name);
}
std::expected<symbols::function, error<symbols::symbols_error>>
symbols::get_function_by_addr(symbols::address_t addr) {
    return this->impl_->get_function_by_addr(addr);
}
std::expected<std::vector<symbols::variable>, error<symbols::symbols_error>>
symbols::get_all_variables_at_address(symbols::address_t pc,
                                     process_id_t tid) {
    return this->impl_->get_all_variables_at_address(pc, tid);
}
std::expected<std::vector<symbols::function>, error<symbols::symbols_error>>
symbols::list_functions_in_module(const module &module) {
    return this->impl_->list_functions_in_module(module);
}
std::expected<std::vector<symbols::parameter>, error<symbols::symbols_error>>
symbols::get_all_parameters_at_address(symbols::address_t pc,
                                      process_id_t tid) {
    return this->impl_->get_all_function_parameters_at_address(pc, tid);
}
std::expected<symbols::address_t, error<symbols::symbols_error>>
symbols::get_address_by_source(const symbols::source &src) {
    return this->impl_->get_address_by_source(src);
}
std::expected<std::vector<symbols::frame_info>, error<symbols::symbols_error>>
symbols::get_stack(process_id_t id) {
    return this->impl_->get_stack(id);
}
symbols::~symbols() {}
} // namespace cdbg::engine
