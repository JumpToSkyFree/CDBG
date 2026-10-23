#ifndef CDBG_SYMBOLS_LINUX_IMPL_HPP
#define CDBG_SYMBOLS_LINUX_IMPL_HPP

#include "engine/backend.hpp"
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dwarf.h>
#include <elfutils/libdw.h>
#include <elfutils/libdwfl.h>
#include <engine/symbols.hpp>
#include <expected>
#include <filesystem>
#include <fmt/base.h>
#include <functional>
#include <optional>
#include <platform_types.hpp>
#include <string>
#include <vector>

namespace cdbg::engine {
struct symbols::module::native_handler {
    Dwfl_Module *module_;
    native_handler() {}
    ~native_handler() {}
};
struct symbols::impl {
    Dwfl_Callbacks callbacks{};
    Dwfl *dwfl_{nullptr};
    process_id_t pid{0};
    impl(process_id_t pid) : pid(pid) {}
    void _for_each_cu(Dwfl_Module *mod,
                      const std::function<void(Dwarf_Die &)> &fn,
                      Dwarf_Addr &bias) {
        Dwarf_Die *cu{nullptr};
        while ((cu = dwfl_module_nextcu(mod, cu, &bias))) {
            fn(*cu);
        }
    }
    Dwarf_Die *_get_cu_die(Dwfl_Module *mod, address_t addr) {
        Dwarf_Addr bias;
        return dwfl_module_addrdie(mod, addr, &bias);
    }
    using die_visitor = std::function<bool(Dwarf_Die &die)>;
    void _walk_die_tree(Dwarf_Die &die, const die_visitor &visitor) {
        if (!visitor(die)) {
            return;
        }

        Dwarf_Die child;
        if (dwarf_child(&die, &child) == 0) {
            do {
                _walk_die_tree(child, visitor);
            } while (dwarf_siblingof(&child, &child) == 0);
        }
    }
    std::optional<std::size_t> get_type_size(Dwarf_Die type_die) {
        int tag = dwarf_tag(&type_die);
        if (tag == DW_TAG_typedef || tag == DW_TAG_const_type ||
            tag == DW_TAG_volatile_type) {
            Dwarf_Attribute attr;
            if (dwarf_attr(&type_die, DW_AT_type, &attr)) {
                Dwarf_Die inner;
                if (!dwarf_formref_die(&attr, &inner))
                    return std::nullopt;
                return get_type_size(inner);
            }
            return std::nullopt;
        }
        if (tag == DW_TAG_pointer_type) {
            return 8;
        }
        Dwarf_Word size = dwarf_bytesize(&type_die);
        if (size == 0) {
            return std::nullopt;
        }
        return size;
    }
    std::optional<std::vector<std::uint8_t>>
    get_raw_value(process_id_t tid, address_t addr, std::size_t size) {
        if (size == 0 || size > 1 << 20) {
            return std::nullopt;
        }
        std::vector<std::uint8_t> ret(size);
        if (!backend::read_memory(tid, addr, ret.data(), size)) {
            return std::nullopt;
        }
        return ret;
    }
    std::optional<std::uint64_t>
    get_frame_base(Dwarf_Die &func_die, Dwarf_Addr pc,
                   const backend::registers &regs) {
        Dwarf_Attribute fb_attr;
        if (!dwarf_attr(&func_die, DW_AT_frame_base, &fb_attr))
            return std::nullopt;
        Dwarf_Op *expr;
        std::size_t len;
        if (dwarf_getlocation(&fb_attr, &expr, &len) != 0 || len == 0)
            return std::nullopt;

        if (expr[0].atom == DW_OP_call_frame_cfa) {
            Dwfl_Module *mod = dwfl_addrmodule(dwfl_, pc);
            Dwarf_Addr cfi_bias = 0;
            Dwarf_CFI *cfi = mod ? dwfl_module_eh_cfi(mod, &cfi_bias) : nullptr;
            if (!cfi && mod)
                cfi = dwfl_module_dwarf_cfi(mod, &cfi_bias);
            if (!cfi)
                return std::nullopt;

            Dwarf_Frame *frame = nullptr;
            if (dwarf_cfi_addrframe(cfi, pc - cfi_bias, &frame) != 0)
                return std::nullopt;

            Dwarf_Op *cfa_ops = nullptr;
            std::size_t nops = 0;
            auto result = std::optional<std::uint64_t>{};
            if (dwarf_frame_cfa(frame, &cfa_ops, &nops) == 0)
                result = evaluate_ops(cfa_ops, nops, regs);
            free(frame);
            return result;
        }
        return evaluate_ops(expr, len, regs);
    }
    std::optional<address_t> resolve_address(address_t addr, Dwarf_Die &sym_die,
                                             const backend::registers &regs,
                                             std::uint64_t bias) {
        Dwarf_Attribute loc_attr;
        if (!dwarf_attr(&sym_die, DW_AT_location, &loc_attr)) {
            return std::nullopt;
        }

        Dwarf_Op *expr;
        std::size_t size;
        if (dwarf_getlocation(&loc_attr, &expr, &size) != 0 || size == 0) {
            return std::nullopt;
        }
        if (expr[0].atom == DW_OP_addr) {
            return expr[0].number + bias;
        }

        if (expr[0].atom == DW_OP_fbreg) {
            Dwarf_Addr bias_;
            Dwarf_Die *cu = dwfl_addrdie(dwfl_, addr, &bias_);
            if (!cu)
                return std::nullopt;
            Dwarf_Die *scopes{nullptr};
            int n = dwarf_getscopes(cu, addr - bias_, &scopes);
            if (n <= 0) {
                free(scopes);
                return std::nullopt;
            }
            Dwarf_Die func_die;
            for (int i = 0; i < n; i++) {
                if (dwarf_tag(&scopes[i]) == DW_TAG_subprogram) {
                    func_die = scopes[i];
                }
            }
            free(scopes);
            auto frame_base = get_frame_base(func_die, addr, regs);
            if (!frame_base)
                return std::nullopt;
            return *frame_base + static_cast<std::int64_t>(expr[0].number);
        }
        return std::nullopt;
    }
    void extract_symbols(
        std::int32_t filter, address_t pc,
        std::function<void(Dwarf_Die *die, const symbols::_type &type,
                           Dwarf_Addr bias, const char *die_name,
                           const std::optional<std::size_t> &size)>
            iterator_cb) {
        Dwarf_Addr bias{0x00};
        Dwarf_Die *cu = dwfl_addrdie(dwfl_, pc, &bias);
        if (!cu)
            return;
        Dwarf_Die *scopes{nullptr};
        int n = dwarf_getscopes(cu, pc - bias, &scopes);
        if (n <= 0) {
            free(scopes);
            return;
        }
        for (int i = 0; i < n; i++) {
            Dwarf_Die child;
            if (dwarf_child(&scopes[i], &child) != 0) {
                continue;
            }
            do {
                int tag = dwarf_tag(&child);
                if (tag == filter) {
                    const char *die_name = dwarf_diename(&child);
                    if (!die_name) {
                        continue;
                    }
                    symbols::_type type_{};
                    Dwarf_Attribute type_attr;
                    Dwarf_Die type_die;
                    if (!dwarf_attr(&child, DW_AT_type, &type_attr)) {
                        continue;
                    }
                    if (!dwarf_formref_die(&type_attr, &type_die)) {
                        continue;
                    }
                    _die_type_to_type(type_die, type_);
                    auto size_ = get_type_size(type_die);
                    if (!size_) {
                        continue;
                    }
                    iterator_cb(&child, type_, bias, die_name, size_);
                }
            } while (dwarf_siblingof(&child, &child) == 0);
        }
        free(scopes);
    }
    std::expected<std::vector<symbols::function>, error<symbols_error>>
    list_functions_in_module(const symbols::module &module) {
        if (!dwfl_)
            return std::unexpected(error<symbols_error>{
                symbols_error::symbols_not_initialized, std::nullopt});
        if (!module.native_handler_) return std::unexpected(error<symbols_error>{
            symbols_error::module_not_found, std::nullopt
        });

        std::vector<symbols::function> ret;
        Dwarf_Addr bias;
        _for_each_cu(module.native_handler_->module_, [this, &ret](Dwarf_Die &die) {
            _walk_die_tree(die, [this, &ret](Dwarf_Die &die) -> bool {
                function ref;
                if (dwarf_tag(&die) == DW_TAG_subprogram) {
                    symbols::function func;
                    func.name = dwarf_diename(&die);
                    dwarf_lowpc(&die, &func.low);
                    dwarf_highpc(&die, &func.high);
                    {
                        Dwarf_Attribute type_attr;
                        if (!dwarf_attr(&die, DW_AT_type, &type_attr)) {
                            return true;
                        }
                        Dwarf_Die type_die;
                        dwarf_formref_die(&type_attr, &type_die);
                        _die_type_to_type(type_die, func.return_type);
                    }
                    _walk_die_tree(die, [this, &func](Dwarf_Die & die) -> bool {
                        symbols::parameter param;
                        if (dwarf_tag(&die) == DW_TAG_formal_parameter) {
                            param.address = 0x00;
                            const char* name = dwarf_diename(&die);
                            if (name) param.name = name;
                            {
                                Dwarf_Attribute type_attr;
                                Dwarf_Die type_die;
                                if (!dwarf_attr(&die, DW_AT_type, &type_attr)) {
                                    return true;
                                }
                                if (!dwarf_formref_die(&type_attr, &type_die)) {
                                    return true;
                                }
                                _die_type_to_type(type_die, param.type);
                                auto size_ = get_type_size(type_die);
                                if(size_.has_value()) param.size = *size_;
                            }
                        }
                        func.params.push_back(param);
                        return true;
                    });
                    ret.push_back(func);
                }
                return true;
            });
        }, bias);
        return ret;
    }
    std::expected<std::vector<symbols::parameter>, error<symbols_error>>
    get_all_function_parameters_at_address(address_t pc, process_id_t tid) {
        if (!dwfl_)
            return std::unexpected(error<symbols_error>{
                symbols_error::symbols_not_initialized, std::nullopt});
        auto regs = backend::get_regs(tid);
        if (!regs)
            return std::unexpected(error<symbols_error>{
                symbols_error::registers_unavailable, regs.error().native});
        std::vector<symbols::parameter> ret;
        extract_symbols(
            DW_TAG_formal_parameter, pc,
            [this, &ret, &pc, &regs, tid](
                Dwarf_Die *die, const symbols::_type &type, Dwarf_Addr bias,
                const char *die_name, const std::optional<std::size_t> &size) {
                auto address = resolve_address(pc, *die, *regs, bias);
                if (!address)
                    return;
                symbols::parameter param{
                    .name = die_name,
                    .address = *address,
                    .type = type,
                    .size = *size,
                    .value = this->get_raw_value(tid, *address, *size)};
                ret.push_back(param);
            });
        return ret;
    }
    std::expected<std::vector<symbols::variable>, error<symbols_error>>
    get_all_variables_at_address(address_t pc, process_id_t tid) {
        if (!dwfl_)
            return std::unexpected(error<symbols_error>{
                symbols_error::symbols_not_initialized, std::nullopt});
        auto regs = backend::get_regs(tid);
        if (!regs)
            return std::unexpected(error<symbols_error>{
                symbols_error::registers_unavailable, regs.error().native});
        std::vector<symbols::variable> ret;
        extract_symbols(
            DW_TAG_variable, pc,
            [this, &regs, &ret, &pc, tid](
                Dwarf_Die *die, const symbols::_type &type, Dwarf_Addr bias,
                const char *die_name, const std::optional<std::size_t> &size) {
                auto address = resolve_address(pc, *die, *regs, bias);
                if (!address)
                    return;
                symbols::variable var{
                    .name = die_name,
                    .address = *address,
                    .type = type,
                    .size = *size,
                    .value = this->get_raw_value(tid, *address, *size)};
                ret.push_back(var);
            });
        return ret;
    }
    std::expected<symbols::function, error<symbols_error>>
    get_function_by_name(std::string_view name) {
        if (!dwfl_)
            return std::unexpected(error<symbols_error>{
                symbols_error::symbols_not_initialized, std::nullopt});
        struct _find_ctx {
            symbols::impl *this_;
            std::optional<Dwarf_Die> result{std::nullopt};
            std::string name_;
            Dwarf_Addr bias;
        } find_ctx = {.this_ = this, .name_{name}, .bias = 0x00};
        dwfl_getmodules(
            dwfl_,
            [](Dwfl_Module *mod, void **, const char *, Dwarf_Addr,
               void *arg) -> int {
                auto find_ctx = static_cast<_find_ctx *>(arg);
                if (find_ctx->this_->determine_level(mod) !=
                    debug_level_info::full) {
                    return DWARF_CB_ABORT;
                }
                find_ctx->this_->_for_each_cu(
                    mod,
                    [&](Dwarf_Die &cu) {
                        if (find_ctx->result)
                            return;
                        find_ctx->this_->_walk_die_tree(
                            cu, [&](Dwarf_Die &die) -> bool {
                                if (dwarf_tag(&die) == DW_TAG_subprogram) {
                                    const char *die_name = dwarf_diename(&die);
                                    if (die_name &&
                                        die_name == find_ctx->name_) {
                                        find_ctx->result = die;
                                        return false;
                                    }
                                }
                                return true;
                            });
                    },
                    find_ctx->bias);
                return find_ctx->result ? DWARF_CB_ABORT : DWARF_CB_OK;
            },
            &find_ctx, 0);
        if (!find_ctx.result) {
            return std::unexpected(error<symbols_error>{
                symbols_error::function_not_found, std::nullopt});
        }
        symbols::function func;
        func.name = dwarf_diename(&*find_ctx.result);
        dwarf_lowpc(&*find_ctx.result, &func.low);
        dwarf_highpc(&*find_ctx.result, &func.high);
        // TODO: implement a function that will walk the type tree of a
        // variable/parameter and set it.
        // walk_die_tree(*find_ctx.result, [&](Dwarf_Die &die) -> bool {
        //     if (dwarf_tag(&die) == DW_TAG_formal_parameter) {
        //         func.params.push_back(
        //             symbols::parameter{dwarf_diename(&die), 0x00, {""}, ""});
        //     }
        //     return false;
        // });
        {
            Dwarf_Attribute type_attr;
            if (!dwarf_attr(&*find_ctx.result, DW_AT_type, &type_attr)) {
                return std::unexpected(error<symbols_error>{
                    symbols_error::function_not_found, std::nullopt});
            }
            Dwarf_Die type_die;
            dwarf_formref_die(&type_attr, &type_die);
            _die_type_to_type(type_die, func.return_type);
        }
        return func;
    }
    static std::optional<std::uint64_t>
    dwarf_reg_value(const backend::registers &regs, int regno) {
#if defined(__aarch64__)
        if (regno <= 30) {
            auto v = regs.get("x" + std::to_string(regno));
            if (v)
                return *v;
        }
        if (regno == 31) {
            auto v = regs.get("sp");
            if (v)
                return *v;
        }
#elif defined(__x86_64)
        static const char *n[16] = {"rax", "rdx", "rcx", "rbx", "rsi", "rdi",
                                    "rbp", "rsp", "r8",  "r9",  "r10", "r11",
                                    "r12", "r13", "r14", "r15"};
        if (regno < 16) {
            auto v = regs.get(n[regno]);
            if (v)
                return *v;
        }
#endif
        return std::nullopt;
    }
    static std::optional<std::uint64_t>
    evaluate_ops(Dwarf_Op *ops, size_t nops, const backend::registers &regs) {
        std::vector<std::uint64_t> stack;
        for (size_t i = 0; i < nops; i++) {
            int a = ops[i].atom;
            if (a >= DW_OP_breg0 && a <= DW_OP_breg31) {
                auto r = dwarf_reg_value(regs, a - DW_OP_breg0);
                if (!r)
                    return std::nullopt;
                stack.push_back(*r + ops[i].number);
            } else if (a ==
                       DW_OP_bregx) { // register in number, offset in number2
                auto r = dwarf_reg_value(regs, (int)ops[i].number);
                if (!r)
                    return std::nullopt;
                stack.push_back(*r + (std::int64_t)ops[i].number2);
            } else if (a >= DW_OP_lit0 && a <= DW_OP_lit31) {
                stack.push_back(a - DW_OP_lit0);
            } else if (a == DW_OP_constu || a == DW_OP_const1u ||
                       a == DW_OP_const2u || a == DW_OP_const4u ||
                       a == DW_OP_const8u) {
                stack.push_back(ops[i].number);
            } else if (a == DW_OP_consts || a == DW_OP_const1s ||
                       a == DW_OP_const2s || a == DW_OP_const4s ||
                       a == DW_OP_const8s) {
                stack.push_back((std::uint64_t)(std::int64_t)ops[i].number);
            } else if (a == DW_OP_plus_uconst) {
                stack.back() += ops[i].number;
            } else if (a == DW_OP_plus) {
                auto b = stack.back();
                stack.pop_back();
                b += stack.back();
                stack.back() = b;
            } else if (a == DW_OP_minus) {
                auto b = stack.back();
                stack.pop_back();
                b = stack.back() - b;
                stack.back() = b;
            } else if (a == DW_OP_deref || a == DW_OP_deref_size)
                return std::nullopt;
            else
                return std::nullopt;
        }
        if (stack.empty())
            return std::nullopt;
        return stack.back();
    }
    std::expected<symbols::function, error<symbols_error>>
    get_function_by_addr(symbols::address_t address) {
        if (!dwfl_) {
            return std::unexpected(error<symbols_error>{
                symbols_error::symbols_not_initialized, std::nullopt});
        }
        Dwarf_Addr bias;
        Dwarf_Die *cu = dwfl_addrdie(dwfl_, address, &bias);
        if (!cu)
            return std::unexpected(error<symbols_error>{
                symbols_error::function_not_found, std::nullopt});
        Dwarf_Die *scopes{nullptr};
        int n = dwarf_getscopes(cu, address - bias, &scopes);
        if (n <= 0) {
            free(scopes);
            return std::unexpected(error<symbols_error>{
                symbols_error::function_not_found, std::nullopt});
        }
        Dwarf_Die func_die;
        for (int i = 0; i < n; i++) {
            if (dwarf_tag(&scopes[i]) == DW_TAG_subprogram) {
                func_die = scopes[i];
            }
        }
        free(scopes);
        symbols::function func;
        func.name = dwarf_diename(&func_die);
        dwarf_lowpc(&func_die, &func.low);
        dwarf_highpc(&func_die, &func.high);
        {
            Dwarf_Attribute type_attr;
            if (!dwarf_attr(&func_die, DW_AT_type, &type_attr)) {
                return std::unexpected(error<symbols_error>{
                    symbols_error::function_not_found, std::nullopt});
            }
            Dwarf_Die type_die;
            dwarf_formref_die(&type_attr, &type_die);
            _die_type_to_type(type_die, func.return_type);
        }
        return func;
    }
    void _die_type_to_type(Dwarf_Die &die, symbols::_type &type) {
        int tag = dwarf_tag(&die);
        switch (tag) {
            case DW_TAG_base_type: {
                const char *name = dwarf_diename(&die);
                if (!name) {
                    type.name = "?";
                } else {
                    type.name = name;
                }
                return;
            }
            case DW_TAG_formal_parameter: {
                Dwarf_Attribute attr;
                if (dwarf_attr(&die, DW_AT_type, &attr)) {
                    Dwarf_Die _die;
                    dwarf_formref_die(&attr, &_die);
                    _die_type_to_type(_die, type);
                }
                return;
            }
            case DW_TAG_pointer_type: {
                type.is_pointer = true;
                Dwarf_Attribute attr;
                if (dwarf_attr(&die, DW_AT_type, &attr)) {
                    Dwarf_Die _die;
                    dwarf_formref_die(&attr, &_die);
                    _die_type_to_type(_die, type);
                } else {
                    type.name = "void";
                }
                return;
            }
            case DW_TAG_const_type: {
                type.is_constant = true;
                Dwarf_Attribute attr;
                if (dwarf_attr(&die, DW_AT_type, &attr)) {
                    Dwarf_Die _die;
                    dwarf_formref_die(&attr, &_die);
                    _die_type_to_type(_die, type);
                } else {
                    type.name = "void";
                }
                return;
            }
            case DW_TAG_structure_type: {
                type.is_struct = true;
                const char *name = dwarf_diename(&die);
                if (!name) {
                    type.name = "?";
                } else {
                    type.name = name;
                }
                return;
            }
            case DW_TAG_reference_type: {
                type.is_reference = true;
                Dwarf_Attribute attr;
                if (dwarf_attr(&die, DW_AT_type, &attr)) {
                    Dwarf_Die _die;
                    dwarf_formref_die(&attr, &_die);
                    _die_type_to_type(_die, type);
                } else {
                    type.name = "void";
                }
                return;
            }
            case DW_TAG_rvalue_reference_type: {
                type.is_rvalue_reference = true;
                Dwarf_Attribute attr;
                if (dwarf_attr(&die, DW_AT_type, &attr)) {
                    Dwarf_Die referenced;
                    dwarf_formref_die(&attr, &referenced);
                    _die_type_to_type(referenced, type);
                } else {
                    type.name = "void";
                }
                return;
            }
                // case DW_TAG_typedef: {
                // const char *name = dwarf_diename(&die);
                // TODO: implement a function that will extract a structured
                // data of a function pointer signature.
                // }
        }
    }

    std::expected<void, error<symbols_error>> initialize() {
        callbacks = {};
        callbacks.find_elf = dwfl_linux_proc_find_elf;
        callbacks.find_debuginfo = dwfl_standard_find_debuginfo;
        callbacks.debuginfo_path = NULL;
        if (!(dwfl_ = dwfl_begin(&callbacks)))
            return std::unexpected(error<symbols_error>{
                symbols_error::reading_debug_info_failed, std::nullopt});
        dwfl_report_begin(dwfl_);
        int res = dwfl_linux_proc_report(dwfl_, pid);
        if (res != 0) {
            dwfl_report_end(dwfl_, nullptr, nullptr);
            return std::unexpected(error<symbols_error>{
                symbols_error::reading_debug_info_failed, std::nullopt});
        }
        dwfl_report_end(dwfl_, NULL, NULL);
        if (dwfl_linux_proc_attach(dwfl_, pid, true) != 0) {
            return std::unexpected(error<symbols_error>{
                symbols_error::reading_debug_info_failed, std::nullopt});
        }
        return {};
    }
    std::expected<source, error<symbols_error>>
    get_source_by_addr(address_t addr) {
        if (!dwfl_) {
            return std::unexpected(error<symbols_error>{
                symbols_error::symbols_not_initialized, std::nullopt});
        }
        auto module_ = get_module_by_addr(addr);
        if (!module_.has_value()) {
            return std::unexpected(module_.error());
        }
        if (determine_level(module_->native_handler_->module_) !=
            debug_level_info::full) {
            return std::unexpected(error<symbols_error>{
                symbols_error::source_not_found, std::nullopt});
        }
        Dwfl_Line *line{nullptr};
        if (!(line = dwfl_module_getsrc(module_->native_handler_->module_,
                                        addr))) {
            return std::unexpected(error<symbols_error>{
                symbols_error::source_not_found, std::nullopt});
        }
        int src_line, src_pos;
        const char *path =
            dwfl_lineinfo(line, nullptr, &src_line, &src_pos, nullptr, nullptr);
        if(!path) return std::unexpected(error<symbols_error>{symbols::symbols_error::source_not_found, std::nullopt});
        source src = {
            .file = std::move(path),
            .line = src_line,
            .pos = src_pos,
        };
        return src;
    }
    std::expected<module, error<symbols_error>>
    get_module_by_addr(address_t addr) {
        if (!dwfl_) {
            return std::unexpected(error<symbols_error>{
                symbols_error::symbols_not_initialized, std::nullopt});
        }
        Dwfl_Module *mod = dwfl_addrmodule(dwfl_, addr);
        if (!mod)
            return std::unexpected(error<symbols_error>{
                symbols_error::module_not_found, std::nullopt});

        Dwarf_Addr low, high, bias;
        const char *mainfile = nullptr;
        const char *name = dwfl_module_info(mod, nullptr, &low, &high, &bias,
                                            nullptr, &mainfile, nullptr);
        if (!name)
            return std::unexpected(error<symbols_error>{
                symbols_error::module_not_found, std::nullopt});

        module result;
        result.module_name = name;
        result.path = mainfile ? mainfile : "";
        result.load_address = bias;
        result.low_address = low;
        result.high_address = high;
        result.info_level = determine_level(mod);
        result.native_handler_ =
            std::make_unique<symbols::module::native_handler>();
        result.native_handler_->module_ = mod;
        return result;
    }
    bool has_debug_info(address_t pc) const {
        if (!dwfl_)
            return false;
        Dwfl_Module *module = dwfl_addrmodule(this->dwfl_, pc);
        if (!module) {
            return false;
        }
        Dwarf_Addr bias{0};
        Dwarf *dbg = dwfl_module_getdwarf(module, &bias);
        return dbg != nullptr;
    }
    bool has_debug_info() const {
        if (!dwfl_)
            return false;
        Dwfl_Module *module = nullptr;
        dwfl_getmodules(
            dwfl_,
            [](Dwfl_Module *mod, void **, const char *, Dwarf_Addr,
               void *arg) -> int {
                *static_cast<Dwfl_Module **>(arg) = mod;
                return DWARF_CB_ABORT;
            },
            &module, 0);
        if (!module) {
            return false;
        }
        Dwarf_Addr bias{0};
        Dwarf *dbg = dwfl_module_getdwarf(module, &bias);
        return dbg != nullptr;
    }
    symbols::debug_level_info determine_level(Dwfl_Module *mod) {
        Dwarf_Addr bias;
        if (dwfl_module_getdwarf(mod, &bias) != nullptr) {
            return debug_level_info::full;
        }

        int symbol_count = dwfl_module_getsymtab(mod);
        if (symbol_count > 0) {
            return debug_level_info::symbols_only;
        }

        return debug_level_info::empty;
    }
    std::expected<address_t, error<symbols_error>>
    get_address_by_source(const symbols::source &src) {
        auto modules = this->list_modules();
        if (!modules.has_value())
            return std::unexpected(modules.error());
        if (modules->empty())
            return std::unexpected(error<symbols_error>{
                symbols_error::module_not_found, std::nullopt});
        std::optional<address_t> found;
        for (auto &module : *modules) {
            Dwarf_Addr bias;
            Dwarf *dbg;
            if ((dbg = dwfl_module_getdwarf(module.native_handler_->module_,
                                            &bias)) == nullptr)
                return std::unexpected(error<symbols_error>{
                    symbols_error::source_not_found, std::nullopt});

            _for_each_cu(
                module.native_handler_->module_,
                [&](Dwarf_Die &cu) {
                    if (found)
                        return;

                    Dwarf_Lines *lines;
                    std::size_t nlines;
                    if (dwarf_getsrclines(&cu, &lines, &nlines) != 0)
                        return;

                    for (std::size_t i = 0; i < nlines; i++) {
                        Dwarf_Line *l = dwarf_onesrcline(lines, i);
                        if (!l)
                            continue;

                        int line_no;
                        if (dwarf_lineno(l, &line_no) != 0 ||
                            line_no != src.line)
                            continue;

                        const char *src_file =
                            dwarf_linesrc(l, nullptr, nullptr);
                        if (!src_file)
                            continue;

                        if (std::filesystem::path(src_file).filename() !=
                            std::filesystem::path(src.file).filename())
                            continue;

                        Dwarf_Addr addr;
                        if (dwarf_lineaddr(l, &addr) != 0)
                            continue;

                        found = addr + bias;
                        return;
                    }
                },
                bias);
        }
        if (!found.has_value()) {
            return std::unexpected(error<symbols_error>{
                symbols_error::source_not_found, std::nullopt});
        }
        return *found;
    }
    std::expected<symbols::modules, error<symbols_error>> list_modules() {
        if (!dwfl_) {
            return std::unexpected(error<symbols_error>{
                symbols_error::symbols_not_initialized, std::nullopt});
        }
        symbols::modules modules_;
        dwfl_getmodules(
            dwfl_,
            [](Dwfl_Module *mod, void **, const char *, Dwarf_Addr,
               void *arg) -> int {
                std::unique_ptr<symbols::module::native_handler> n_handler_ =
                    std::make_unique<symbols::module::native_handler>();
                n_handler_->module_ = mod;
                auto modules_ = static_cast<symbols::modules *>(arg);
                address_t high, low, bias;
                const char *module_file{nullptr};
                const char *debug_file{nullptr};
                const char *name_cstr =
                    dwfl_module_info(mod, nullptr, &low, &high, &bias, nullptr,
                                     &module_file, &debug_file);
                std::string name = name_cstr ? name_cstr : "";
                symbols::module mod_;
                mod_.path = module_file ? module_file : "";
                mod_.module_name = name;
                mod_.load_address = bias;
                mod_.low_address = low;
                mod_.high_address = high;
                mod_.info_level = debug_level_info::empty;
                mod_.native_handler_ = std::move(n_handler_);
                modules_->push_back(std::move(mod_));
                return DWARF_CB_OK;
            },
            &modules_, 0);
        for (auto &module_ : modules_) {
            module_.info_level =
                determine_level(module_.native_handler_->module_);
        }
        return modules_;
    }

    std::expected<symbols::module, error<symbols_error>>
    get_module_by_name(std::string_view module) {
        if (!dwfl_) {
            return std::unexpected(error<symbols_error>{
                symbols_error::symbols_not_initialized, std::nullopt});
        }
        struct _data {
            symbols::module module_;
            std::string_view module_name;
            bool found{false};
        } data;
        data.module_name = std::move(module);
        dwfl_getmodules(
            dwfl_,
            [](Dwfl_Module *mod, void **, const char *, Dwarf_Addr,
               void *arg) -> int {
                auto data_ = static_cast<_data *>(arg);
                address_t high, low, bias;
                const char *module_file{nullptr};
                const char *debug_file{nullptr};
                const char *name_cstr =
                    dwfl_module_info(mod, nullptr, &low, &high, &bias, nullptr,
                                     &module_file, &debug_file);
                if (!name_cstr ||
                    std::strcmp(name_cstr, data_->module_name.data()) != 0) {
                    return DWARF_CB_OK;
                }
                std::unique_ptr<symbols::module::native_handler> n_handler_ =
                    std::make_unique<symbols::module::native_handler>();
                n_handler_->module_ = mod;
                std::string name = name_cstr ? name_cstr : "";
                data_->module_.path = module_file ? module_file : "";
                data_->module_.module_name = name;
                data_->module_.load_address = bias;
                data_->module_.low_address = low;
                data_->module_.high_address = high;
                data_->module_.info_level = debug_level_info::empty;
                data_->module_.native_handler_ = std::move(n_handler_);
                data_->found = true;
                return DWARF_CB_ABORT;
            },
            &data, 0);
        if (!data.found) {
            return std::unexpected(error<symbols_error>{
                symbols_error::module_not_found, std::nullopt});
        }
        return std::move(data.module_);
    }
    std::expected<std::vector<symbols::frame_info>, error<symbols_error>>
    get_stack(process_id_t id) {
        if (!dwfl_)
            return std::unexpected(error<symbols_error>{
                symbols_error::symbols_not_initialized, std::nullopt});
        struct frame_collector {
            symbols::impl *impl;
            std::vector<symbols::frame_info> frames;
        } collector = {.impl = this, .frames = {}};

        int result = dwfl_getthread_frames(
            dwfl_, id,
            [](Dwfl_Frame *frame, void *arg) -> int {
                auto *collector = static_cast<frame_collector *>(arg);

                Dwarf_Addr pc;
                bool is_activation;
                if (!dwfl_frame_pc(frame, &pc, &is_activation)) {
                    return DWARF_CB_ABORT;
                }

                frame_info fi;
                fi.pc = pc;

                auto func = collector->impl->get_function_by_addr(pc);
                if (func) {
                    fi.function_name = func->name;
                }
                auto src = collector->impl->get_source_by_addr(pc);
                if (src) {
                    fi.location = *src;
                }
                collector->frames.push_back(fi);
                return DWARF_CB_OK;
            },
            &collector);

        if (result != 0 && collector.frames.empty()) {
            return std::unexpected(error<symbols_error>{
                symbols_error::frame_unwind_failed, std::nullopt});
        }

        return collector.frames;
    }
    ~impl() {
        if (dwfl_) {
            dwfl_end(dwfl_);
            dwfl_ = nullptr;
        }
    }
};
} // namespace cdbg::engine
#endif // !CDBG_SYMBOLS_LINUX_IMPL_HPP
