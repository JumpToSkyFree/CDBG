#include "engine/backend.hpp"
#include "engine/disassembler.hpp"
#include "engine/symbols.hpp"
#include "platform_types.hpp"
#include <memory>
#include <optional>
#include <utility>
#if defined(__linux__)
#include <engine/internal/process_linux_impl.hpp>
#endif
#include <engine/process.hpp>

namespace cdbg::engine {

process::process(std::string executable_path, char **argv)
    : impl_(std::make_unique<impl>(std::move(executable_path), argv)) {}

process::process(process_id_t pid) : impl_(std::make_unique<impl>(pid)) {}

process::~process() {}

process::process(process &&) noexcept = default;
process &process::operator=(process &&) noexcept = default;

std::expected<void, error<backend::backend_error>> process::initialize() {
    return impl_->initialize();
}
bool process::is_initialized() const { return impl_->is_initialized(); }
bool process::is_process_alive() const { return impl_->is_process_alive(); }
process_id_t process::get_process_id() const { return impl_->get_process_id(); }
std::expected<std::size_t, error<backend::backend_error>>
process::read_terminal(void *buf, std::size_t len) const {
    return impl_->read_terminal(buf, len);
}
std::expected<std::size_t, error<backend::backend_error>>
process::write_terminal(const void *buf, std::size_t len) const {
    return impl_->write_terminal(buf, len);
}
const std::string &process::executable_path() const {
    return impl_->executable_path();
}

std::expected<std::vector<thread>, error<backend::backend_error>>
process::list_threads() {
    return impl_->list_threads();
}

std::expected<symbols::address_t, error<backend::backend_error>> process::get_pc() const {
    return impl_->get_pc();
}
std::expected<symbols::address_t, error<backend::backend_error>> process::get_sp() const {
    return impl_->get_sp();
}
std::expected<backend::registers, error<backend::backend_error>>
process::get_regs() const {
    return impl_->get_regs();
}
std::expected<void, error<backend::backend_error>>
process::set_regs(const backend::registers &regs) const {
    return impl_->set_regs(regs);
}
std::expected<void, error<backend::backend_error>>
process::set_pc(std::uint64_t addr) const {
    return impl_->set_pc(addr);
}
std::expected<void, error<backend::backend_error>>
process::read_memory(std::uint64_t addr, void *buf, std::size_t len) const {
    return impl_->read_memory(addr, buf, len);
}
std::expected<void, error<backend::backend_error>>
process::write_memory(std::uint64_t addr, const void *buf, std::size_t len) const {
    return impl_->write_memory(addr, buf, len);
}
std::expected<void, error<backend::backend_error>>
process::set_breakpoint(std::uint64_t addr, long &orig) const {
    return impl_->set_breakpoint(addr, orig);
}
std::expected<void, error<backend::backend_error>>
process::remove_breakpoint(std::uint64_t addr, long orig) const {
    return impl_->remove_breakpoint(addr, orig);
}
std::expected<backend::event, error<backend::backend_error>> process::wait() const {
    return impl_->wait();
}
backend::event process::poll() const { return impl_->poll(); }
std::expected<void, error<backend::backend_error>>
process::cont(int signal) const {
    return impl_->cont(signal);
}
std::expected<void, error<backend::backend_error>> process::single_step() const {
    return impl_->single_step();
}
namespace {
constexpr std::size_t k_max_source_steps = 1'000'000;

using source_location = std::optional<symbols::source>;

// A missing line row (PLT/thunk/libc) is a normal stop, not a failure.
// Any other symbols error means the debug info is genuinely unusable.
std::expected<source_location, error<backend::backend_error>>
locate_source(symbols &syms, std::uint64_t addr) {
    auto src = syms.get_source_by_addr(addr);
    if (src) return source_location{*src};
    if (src.error().code == symbols::source_not_found)
        return source_location{std::nullopt};
    return std::unexpected(error<backend::backend_error>{
        backend::reading_debug_info_failed, src.error().native});
}

bool event_terminates(const backend::event &event) {
    return std::get_if<backend::exited>(&event) ||
           std::get_if<backend::signaled>(&event);
}

bool event_is_interrupt(const backend::event &event) {
    return event_terminates(event) ||
           std::get_if<backend::stopped_by_signal>(&event) ||
           std::get_if<backend::thread_created>(&event);
}

bool is_call_instruction(const process &proc, disassembler &dis,
                         std::uint64_t pc, std::uint32_t &size) {
    const std::size_t max_len = disassembler::instruction_size();
    // Try the longest possible encoding first, shrinking near page boundaries.
    static constexpr std::size_t lengths[] = {15, 8, 4, 2, 1};
    for (std::size_t len : lengths) {
        if (len > max_len) continue;
        std::uint8_t buf[15]{};
        if (!proc.read_memory(pc, buf, len)) continue;
        auto code = dis.disassemble(pc, buf, len);
        if (!code || code->empty()) continue;

        size = code->front().size;
        const auto &mnemonic = code->front().mnemonic;
        if (dis.current_architecture() == disassembler::architecture::aarch64)
            return mnemonic.starts_with("bl");
        return mnemonic.starts_with("call");
    }
    return false;
}
} // namespace

std::expected<step_result, error<backend::backend_error>>
process::single_step_source(std::shared_ptr<symbols> symbols) const {
    auto pc = get_pc();
    if (!pc) return std::unexpected(pc.error());

    auto entry = locate_source(*symbols, *pc);
    if (!entry) return std::unexpected(entry.error());
    const auto old = *entry;

    for (std::size_t i = 0; i < k_max_source_steps; ++i) {
        if (auto status = single_step(); !status)
            return std::unexpected(status.error());
        auto event = wait();
        if (!event) return std::unexpected(event.error());

        // Exit / signal / thread events are legitimate stop outcomes.
        if (event_is_interrupt(*event))
            return step_result{*event, std::nullopt};

        auto new_pc = get_pc();
        if (!new_pc) return std::unexpected(new_pc.error());
        auto location = locate_source(*symbols, *new_pc);
        if (!location) return std::unexpected(location.error());

        // A breakpoint (or anything else) reached while stepping is a stop.
        if (!std::get_if<backend::stopped_at_step>(&*event))
            return step_result{*event, *location};

        // Stepped into code with no line row (PLT stub, libc, ...): stop and
        // let the UI fall back to the disassembler.
        if (!old.has_value() || !location->has_value())
            return step_result{*event, *location};

        // Source location changed -> that's the next line.
        if ((*location)->line != old->line ||
            (*location)->file != old->file)
            return step_result{*event, *location};
    }

    return std::unexpected(
        error<backend::backend_error>{backend::step_budget_exceeded,
                                      std::nullopt});
}

std::expected<step_result, error<backend::backend_error>>
process::step_over_source(std::shared_ptr<symbols> symbols) const {
    auto pc = get_pc();
    if (!pc) return std::unexpected(pc.error());

    auto entry = locate_source(*symbols, *pc);
    if (!entry) return std::unexpected(entry.error());
    const auto old = *entry;

    disassembler dis;

    for (std::size_t i = 0; i < k_max_source_steps; ++i) {
        auto at = get_pc();
        if (!at) return std::unexpected(at.error());

        std::uint32_t insn_size{0};
        if (is_call_instruction(*this, dis, *at, insn_size)) {
            const std::uint64_t return_addr = *at + insn_size;
            long orig{0};
            auto bp = set_breakpoint(return_addr, orig);
            if (bp) {
                auto cont_status = cont();
                if (!cont_status) {
                    (void)remove_breakpoint(return_addr, orig);
                    return std::unexpected(cont_status.error());
                }

                auto event = wait();
                if (!event) {
                    (void)remove_breakpoint(return_addr, orig);
                    return std::unexpected(event.error());
                }

                // Process died: nothing to restore.
                if (event_terminates(*event))
                    return step_result{*event, std::nullopt};

                auto removed = remove_breakpoint(return_addr, orig);
                if (!removed) return std::unexpected(removed.error());

                if (auto *hit =
                        std::get_if<backend::stopped_at_breakpoint>(&*event);
                    hit && hit->addr != return_addr) {
                    // A user breakpoint interrupted the step.
                    auto location = locate_source(*symbols, hit->addr);
                    if (!location) return std::unexpected(location.error());
                    return step_result{*event, *location};
                }

                if (!std::get_if<backend::stopped_at_breakpoint>(&*event) &&
                    !std::get_if<backend::stopped_at_step>(&*event))
                    return step_result{*event, std::nullopt};

                // Back from the call. Re-execute from the original
                // instruction; on x86 the trap leaves pc one byte past it.
                if (auto status = set_pc(return_addr); !status)
                    return std::unexpected(status.error());

                auto new_pc = get_pc();
                if (!new_pc) return std::unexpected(new_pc.error());
                auto location = locate_source(*symbols, *new_pc);
                if (!location) return std::unexpected(location.error());
                if (!old.has_value() || !location->has_value())
                    return step_result{*event, *location};
                if ((*location)->line != old->line ||
                    (*location)->file != old->file)
                    return step_result{*event, *location};
                continue;
            }
            // Could not arm a temporary breakpoint: fall back to stepping.
        }

        if (auto status = single_step(); !status)
            return std::unexpected(status.error());
        auto event = wait();
        if (!event) return std::unexpected(event.error());

        if (event_is_interrupt(*event))
            return step_result{*event, std::nullopt};

        auto new_pc = get_pc();
        if (!new_pc) return std::unexpected(new_pc.error());
        auto location = locate_source(*symbols, *new_pc);
        if (!location) return std::unexpected(location.error());

        if (!std::get_if<backend::stopped_at_step>(&*event))
            return step_result{*event, *location};
        if (!old.has_value() || !location->has_value())
            return step_result{*event, *location};
        if ((*location)->line != old->line ||
            (*location)->file != old->file)
            return step_result{*event, *location};
    }

    return std::unexpected(
        error<backend::backend_error>{backend::step_budget_exceeded,
                                      std::nullopt});
}
} // namespace cdbg::engine
