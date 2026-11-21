//     /                              /
//    /                              /
//   /_____  _____  _____  _____    /  _____   _  _  _____
//  /     / /____  /____/ /____/   /  /____/  /\  / /  __
// /_____/ /____  /    / /   \    /  /    /  /  \/ /____/
// Copyright (C) 2025-2026 Zachary Mahan
// Licensed under the Apache License 2.0. See LICENSE for details.

#include "compiler/hir/def.hpp"
#include "compiler/ast/stmt.h"
#include "compiler/hir/context.hpp"
#include "compiler/hir/exec.hpp"
#include "compiler/hir/generic.hpp"
#include "compiler/hir/indexing.hpp"
#include "compiler/hir/scope.hpp"
#include "utils/ansi_codes.h"
#include <string>
#include <string_view>

namespace hir {

static std::string scope_id_str(ScopeId sid) { return "Scope#" + std::to_string(sid.raw()); }

static std::string preview_def_to_string(Context& ctx, DefId did);

static std::string preview_scope_to_string(Context& ctx, ScopeId scope) {
    std::string str;
    str += scope_id_str(scope);
    str += "{\n";
    ctx.scope(scope).for_each_local_namespace([&ctx, &str](ScopeIdMap::Entry e) {
        str += preview_def_to_string(ctx, e.val());
        str += '\n';
    });
    ctx.scope(scope).for_each_local_type([&ctx, &str](ScopeIdMap::Entry e) {
        str += preview_def_to_string(ctx, e.val());
        str += '\n';
    });
    ctx.scope(scope).for_each_local_variable([&ctx, &str](ScopeIdMap::Entry e) {
        str += preview_def_to_string(ctx, e.val());
        str += '\n';
    });
    str += "\n}";
    str += scope_id_str(scope);
    return str;
}

static std::string preview_def_to_string(Context& ctx, DefId did) {
    const auto vs = Ovld{
        [&ctx, did](const DefModule& d) -> std::string {
            std::string str;
            str += "mod ";
            str += ctx.symbol_id_slice_to_string(ctx.canonical_name(did));
            str += ' ';
            str += preview_scope_to_string(ctx, d.scope);
            return str;
        },
        [&ctx, did](const DefFunction& d) -> std::string {
            std::string str;
            if (ctx.def(did).compt) {
                str += "compt ";
            }
            str += "fn ";
            str += ctx.symbol_id_slice_to_string(ctx.canonical_name(did));
            str += d.maybe_generic_args.has_value()
                       ? gen_args_to_str(ctx, d.maybe_generic_args.as_id())
                       : "";
            str += '(';
            for (const auto tidx : d.param_types) {
                str += type_to_string(ctx, ctx.type_id(tidx));
                if (tidx != d.param_types.last_elem()) {
                    str += ", ";
                }
            }
            str += ") ";
            if (d.return_type.has_value()) {
                str += d.discardable ? "~> " : "-> ";
                str += type_to_string(ctx, d.return_type.as_id());
            }
            if (d.body) {
                str += ' ';
                str += exec_to_string(ctx, d.body.as_id());

            } else {
                str += ';';
            }
            return str;
        },
        [&ctx, did](const DefGenericFunction& d) -> std::string {
            std::string str;
            if (ctx.def(did).compt) {
                str += "compt ";
            }
            str += "fn ";
            str += ctx.symbol(ctx.def(did).name);
            str += gen_params_to_string(ctx, d.generic_params);
            const auto* fn_node = ctx.def_ast_node(did);
            if (!fn_node) {
                str += '(';
                for (auto i{0uz}; i < d.param_cnt; ++i) {
                    str += "...";
                    if (i != d.param_cnt - 1) {
                        str += ", ";
                    }
                }
                str += ") -> [return_type]? {}";
            } else if (fn_node->type == AST_STMT_FN_DECL) {
                ast_stmt_fn_decl_t fn = *fn_node->stmt.fn_decl;
                str += '(';
                for (auto i{0uz}; i < d.param_cnt; ++i) {
                    str += Span{ctx, ctx.def(did).span.file_id, fn.params.start[i]->first,
                                fn.params.start[i]->last}
                               .as_sv(ctx);
                    if (i != d.param_cnt - 1) {
                        str += ", ";
                    }
                }
                str += ')';
                if (fn.return_type) {
                    str += ' ';
                    str += Span{ctx, ctx.def(did).span.file_id, fn.return_type->first,
                                fn.return_type->last}
                               .as_sv(ctx);
                }
                str += " {}";
            }
            return str;
        },
        [&ctx, did](const DefFunctionPrototype& d) -> std::string {
            std::string str;
            str += "fn ";
            str += ctx.symbol(ctx.def(did).name);
            str += '(';
            for (const auto tidx : d.param_types) {
                str += type_to_string(ctx, ctx.type_id(tidx));
                if (tidx != d.param_types.end()) {
                    str += ", ";
                }
            }
            str += ')';
            if (d.return_type.has_value()) {
                str += " -> ";
                str += type_to_string(ctx, d.return_type.as_id());
            }
            str += ';';
            return str;
        },
        [&ctx, did](const DefVariable& d) -> std::string {
            std::string str;
            if (ctx.def(did).compt) {
                str += "compt ";
            }
            str += type_to_string(ctx, d.type_id);
            str += ' ';
            str += ctx.symbol(ctx.def(did).name);
            if (d.compt_value) {
                str += " = ";
                str += exec_to_string(ctx, d.compt_value.as_id());
            }
            str += ';';
            return str;
        },
        [&ctx, did](const DefStruct& d) -> std::string {
            std::string str;
            str += "struct ";
            str += ctx.symbol(ctx.def(did).name);

            const auto contracts = d.contracts;
            if (contracts.len()) {
                str += " has ";

                for (const auto didx : contracts) {

                    str += ctx.symbol_id_slice_to_string(ctx.canonical_name(ctx.def_id(didx)));

                    if (didx != contracts.last_elem()) {
                        str += " + ";
                    }
                }
            }

            str += " {\n";

            const auto ordered_mems = d.ordered_members;

            for (const auto didx : ordered_mems) {
                str += "    ";
                str += preview_def_to_string(ctx, ctx.def_id(didx));
                str += '\n';
            }

            str += preview_scope_to_string(ctx, d.scope);

            str += "}\n";

            return str;
        },
        [&ctx, did](const DefGenericStruct& d) -> std::string {
            std::string str;
            str += "struct ";
            str += ctx.symbol(ctx.def(did).name);
            str += gen_params_to_string(ctx, d.generic_params);
            str += "{}";
            return str;
        },
        [&ctx, did](const DefVariant& d) -> std::string {
            std::string str;
            str += "variant ";
            str += ctx.symbol(ctx.def(did).name);
            if (!d.ordered_members.len()) {
                str += " {}";
                return str;
            }
            str += " {\n";
            for (const auto didx : d.ordered_members) {
                str += "    ";
                str += preview_def_to_string(ctx, ctx.def_id(didx));
                str += ",\n";
            }
            str += '}';
            return str;
        },
        [&ctx, did](const DefGenericVariant& d) -> std::string {
            std::string str;
            str += "variant ";
            str += ctx.symbol(ctx.def(did).name);
            str += gen_params_to_string(ctx, d.generic_params);
            str += "{}";
            return str;
        },
        [&ctx, did](const DefGenericVariantField& d) -> std::string {
            std::string str;
            str += "use ";
            str += ctx.symbol(ctx.def(d.generic_variant).name);
            str += "..";
            str += ctx.symbol(ctx.def(did).name);
            return str;
        },
        [&ctx, did](const DefVariantField& d) -> std::string {
            std::string str;
            str += ctx.symbol(ctx.def(did).name);
            str += '(';
            for (const auto didx : d.members) {
                const Def& member = ctx.def(didx);
                if (member.holds<DefVariable>()) {
                    str += type_to_string(ctx, member.as<DefVariable>().type_id);
                    str += ' ';
                }
                str += ctx.symbol(member.name);
                if (didx != d.members.last_elem()) {
                    str += ", ";
                }
            }
            str += ')';
            return str;
        },
        [&ctx, did](const DefUnion&) -> std::string {
            std::string str;
            str += "union ";
            str += ctx.symbol(ctx.def(did).name);
            str += "{}";
            return str;
        },
        [&ctx, did](const DefGenericContract& d) -> std::string {
            std::string str;
            str += "contract ";
            str += ctx.symbol(ctx.def(did).name);
            str += gen_params_to_string(ctx, d.generic_params);
            str += "{}";
            return str;
        },
        [&ctx, did](const DefContract&) -> std::string {
            std::string str;
            str += "contract ";
            str += ctx.symbol(ctx.def(did).name);
            str += "{}";
            return str;
        },
        [&ctx, did](const DefDeftype& d) -> std::string {
            std::string str{"deftype "};
            str += ctx.symbol(ctx.def(did).name);
            str += " = ";
            str += type_to_string_with_akas(ctx, d.type);
            str += ';';
            return str;
        },
        [](const DefScopeWrapper&) -> std::string { return "(scope wrapper)"; },
        [](const DefUnevaluated&) -> std::string { return "(unevaluated)"; },
        [](const DefMalformed&) -> std::string { return "(malformed)"; },
    };
    return ctx.def(did).visit(vs);
}

namespace {

class HirPrettyPrinter {
  public:
    HirPrettyPrinter(Context& ctx, bool colored) : ctx{ctx}, colored{colored} {
        if (colored) {
            out += ansi_reset();
        }
    }

    std::string take() {
        if (colored) {
            out += ansi_reset();
        }
        return std::move(out);
    }

    void print_def(DefId did, bool qualify_name) {
        const Def& def = ctx.def(did);
        const auto vs = Ovld{
            [&](const DefModule& d) {
                print_def_prefix(def);
                keyword("mod");
                out += ' ';
                out += def_name(did, qualify_name);
                out += ' ';
                print_braced_scope(d.scope, {});
            },
            [&](const DefFunction& d) { print_function(did, d, qualify_name); },
            [&](const DefGenericFunction& d) { print_generic_def(did, "fn", d.generic_params); },
            [&](const DefFunctionPrototype& d) {
                print_def_prefix(def);
                keyword("fn");
                out += ' ';
                fn_name(def_name(did, qualify_name));
                print_params(d.params);
                print_return_type(d.return_type, d.discardable);
                out += ';';
            },
            [&](const DefVariable& d) {
                print_def_prefix(def);
                if (def.compt) {
                    keyword("compt");
                    out += ' ';
                }
                if (def.alignment_preference) {
                    keyword("alignas");
                    open('(');
                    literal(std::to_string(def.alignment_preference));
                    close(')');
                    out += ' ';
                }
                type_name(type_to_string(ctx, d.type_id));
                out += ' ';
                out += ctx.symbol(def.name);
                if (d.compt_value) {
                    out += " = ";
                    print_exec(d.compt_value.as_id());
                }
                out += ';';
            },
            [&](const DefStruct& d) {
                print_def_prefix(def);
                keyword("struct");
                out += ' ';
                type_name(def_name_with_gen_args(did, d.maybe_generic_args));
                if (d.contracts.len()) {
                    out += ' ';
                    keyword("has");
                    out += ' ';
                    for (const auto didx : d.contracts) {
                        type_name(
                            ctx.symbol_id_slice_to_string(ctx.canonical_name(ctx.def_id(didx))));
                        if (didx != d.contracts.last_elem()) {
                            out += " + ";
                        }
                    }
                }
                out += ' ';
                print_braced_scope(d.scope, d.ordered_members);
            },
            [&](const DefGenericStruct& d) { print_generic_def(did, "struct", d.generic_params); },
            [&](const DefVariant& d) {
                print_def_prefix(def);
                keyword("variant");
                out += ' ';
                type_name(def_name_with_gen_args(did, d.maybe_generic_args));
                out += ' ';
                print_braced_scope(d.scope, d.ordered_members);
            },
            [&](const DefGenericVariant& d) {
                print_generic_def(did, "variant", d.generic_params);
            },
            [&](const DefGenericVariantField& d) {
                keyword("use");
                out += ' ';
                type_name(ctx.symbol(ctx.def(d.generic_variant).name));
                out += "..";
                type_name(ctx.symbol(def.name));
                out += ';';
            },
            [&](const DefVariantField& d) {
                type_name(ctx.symbol(def.name));
                print_params(d.members);
                out += ',';
            },
            [&](const DefUnion& d) {
                print_def_prefix(def);
                keyword("union");
                out += ' ';
                type_name(ctx.symbol(def.name));
                out += ' ';
                print_braced_scope(d.scope, d.ordered_members);
            },
            [&](const DefGenericContract& d) {
                print_generic_def(did, "contract", d.generic_params);
            },
            [&](const DefContract& d) {
                print_def_prefix(def);
                keyword("contract");
                out += ' ';
                type_name(def_name_with_gen_args(did, d.maybe_generic_args));
                out += ' ';
                print_braced_scope(d.scope, d.funcs);
            },
            [&](const DefDeftype& d) {
                print_def_prefix(def);
                keyword("deftype");
                out += ' ';
                type_name(ctx.symbol(def.name));
                out += " = ";
                type_name(type_to_string_with_akas(ctx, d.type));
                out += ';';
            },
            [&](const DefScopeWrapper& d) { print_braced_scope(d.scope, {}); },
            [&](const DefUnevaluated&) {
                open('(');
                out += "unevaluated ";
                out += ctx.symbol(def.name);
                close(')');
            },
            [&](const DefMalformed&) {
                open('(');
                out += "malformed ";
                out += ctx.symbol(def.name);
                close(')');
            },
        };
        def.visit(vs);
    }

    void print_scope_contents(ScopeId sid, IdSlice<DefId> skip, bool blank_line_between) {
        bool first = true;
        const auto emit = [&](ScopeIdMap::Entry e) {
            const DefId did = e.val();
            if (slice_contains(skip, did) || ctx.def(did).is_ordered()) {
                return;
            }
            if (!first) {
                newline();
                if (blank_line_between) {
                    newline();
                }
            }
            first = false;
            print_def(did, false);
        };
        const Scope& scope = ctx.scope(sid);
        scope.for_each_local_namespace(emit);
        scope.for_each_local_type(emit);
        scope.for_each_local_variable(emit);
    }

  private:
    static constexpr std::string_view TAB = "    ";

    Context& ctx;
    std::string out;
    HirSize depth{};
    HirSize block_depth{};
    bool colored;
    bool delim_red{true};

    void colorize(const char* color, std::string_view text) {
        if (colored) {
            out += color;
        }
        out += text;
        if (colored) {
            out += ansi_reset();
        }
    }

    void keyword(std::string_view text) { colorize(ansi_magenta_fg(), text); }
    void literal(std::string_view text) { colorize(ansi_green_fg(), text); }
    void type_name(std::string_view text) { colorize(ansi_yellow_fg(), text); }
    void fn_name(std::string_view text) { colorize(ansi_cyan_fg(), text); }

    const char* delim_color() const { return delim_red ? ansi_red_fg() : ansi_blue_fg(); }

    void open(char delim) {
        colorize(delim_color(), std::string_view{&delim, 1});
        delim_red = !delim_red;
    }

    void close(char delim) {
        delim_red = !delim_red;
        colorize(delim_color(), std::string_view{&delim, 1});
    }

    void newline() {
        out += '\n';
        for (HirSize i = 0; i < depth; ++i) {
            out += TAB;
        }
    }

    void indent() { ++depth; }
    void dedent() { --depth; }

    bool slice_contains(IdSlice<DefId> slice, DefId did) {
        for (const auto didx : slice) {
            if (ctx.def_id(didx) == did) {
                return true;
            }
        }
        return false;
    }

    std::string def_name(DefId did, bool qualify) {
        if (qualify) {
            return ctx.symbol_id_slice_to_string(ctx.canonical_name(did));
        }
        return std::string{ctx.symbol(ctx.def(did).name)};
    }

    std::string def_name_with_gen_args(DefId did, OptId<GenericArgIdSliceId> gen_args) {
        std::string name{ctx.symbol(ctx.def(did).name)};
        if (gen_args.has_value()) {
            name += gen_args_to_str(ctx, gen_args.as_id());
        }
        return name;
    }

    void print_def_prefix(const Def& def) {
        if (def.abi == abi_lang::c) {
            keyword("extern");
            out += ' ';
            literal("C");
            out += ' ';
        }
        if (!def.pub && !block_depth) {
            keyword("hid");
            out += ' ';
        }
        if (def.statik) {
            keyword("static");
            out += ' ';
        }
    }

    void print_params(IdSlice<DefId> params) {
        open('(');
        for (const auto didx : params) {
            const Def& param = ctx.def(didx);
            if (param.holds<DefVariable>()) {
                type_name(type_to_string(ctx, param.as<DefVariable>().type_id));
                out += ' ';
            }
            out += ctx.symbol(param.name);
            if (didx != params.last_elem()) {
                out += ", ";
            }
        }
        close(')');
    }

    void print_return_type(OptId<TypeId> return_type, bool discardable) {
        if (return_type.has_value()) {
            out += discardable ? " ~> " : " -> ";
            type_name(type_to_string(ctx, return_type.as_id()));
        }
    }

    void print_function(DefId did, const DefFunction& d, bool qualify_name) {
        const Def& def = ctx.def(did);
        print_def_prefix(def);
        if (def.compt) {
            keyword("compt");
            out += ' ';
        }
        keyword("fn");
        out += ' ';
        std::string name = def_name(did, qualify_name);
        if (d.maybe_generic_args.has_value()) {
            name += gen_args_to_str(ctx, d.maybe_generic_args.as_id());
        }
        fn_name(name);
        print_params(d.params);
        print_return_type(d.return_type, d.discardable);
        if (d.body) {
            out += ' ';
            print_body(d.body.as_id());
        } else {
            out += ';';
        }
    }

    void print_generic_def(DefId did, std::string_view kw, IdSlice<GenericParamId> generic_params) {
        const Def& def = ctx.def(did);
        const ast_stmt_t* node = ctx.def_ast_node(did);
        if (node && node->first && node->last) {
            const auto src = Span{ctx, def.span.file_id, node->first, node->last}.as_sv(ctx);
            for (const char c : src) {
                if (c == '\n') {
                    while (!out.empty() && (out.back() == ' ' || out.back() == '\t')) {
                        out.pop_back();
                    }
                    newline();
                } else {
                    out += c;
                }
            }
            return;
        }
        print_def_prefix(def);
        if (def.compt) {
            keyword("compt");
            out += ' ';
        }
        keyword(kw);
        out += ' ';
        const std::string name
            = std::string{ctx.symbol(def.name)} + gen_params_to_string(ctx, generic_params);
        if (kw == "fn") {
            fn_name(name);
        } else {
            type_name(name);
        }
        out += ' ';
        open('{');
        close('}');
    }

    void print_braced_scope(ScopeId sid, IdSlice<DefId> ordered) {
        open('{');
        const auto before = out.size();
        indent();
        for (const auto didx : ordered) {
            newline();
            print_def(ctx.def_id(didx), false);
        }
        const auto before_scope = out.size();
        newline();
        const auto after_newline = out.size();
        print_scope_contents(sid, ordered, false);
        if (out.size() == after_newline) {
            out.resize(before_scope);
        }
        dedent();
        if (out.size() != before) {
            newline();
        }
        close('}');
    }

    void print_block(BlockId bid) {
        const Block& block = ctx.block(bid);
        if (!block.defs.len() && !block.execs.len()) {
            open('{');
            close('}');
            return;
        }
        open('{');
        indent();
        ++block_depth;
        for (const auto didx : block.defs) {
            newline();
            print_def(ctx.def_id(didx), false);
        }
        for (const auto eidx : block.execs) {
            newline();
            print_stmt(ctx.exec_id(eidx));
        }
        --block_depth;
        dedent();
        newline();
        close('}');
    }

    void print_body(ExecId eid) {
        if (ctx.exec(eid).holds<ExecBlock>()) {
            print_block(ctx.exec(eid).as<ExecBlock>().block_id);
            return;
        }
        open('{');
        out += ' ';
        print_stmt(eid);
        out += ' ';
        close('}');
    }

    void print_stmt(ExecId eid) {
        const Exec& exec = ctx.exec(eid);
        print_exec(eid);
        const bool needs_terminator = !exec.holds<ExecBlock>() && !exec.holds<ExecBranch>()
                                      && !exec.holds<ExecLoop>() && !exec.holds<ExecMatch>();
        if (needs_terminator) {
            out += ';';
        }
    }

    void print_operand(ExecId eid) { print_wrapped(eid, false); }

    void print_postfix_operand(ExecId eid) { print_wrapped(eid, true); }

    void print_wrapped(ExecId eid, bool postfix) {
        const Exec& exec = ctx.exec(eid);
        const bool is_prefix = exec.holds<ExecDeref>() || exec.holds<ExecUnary>()
                               || exec.holds<ExecBorrow>() || exec.holds<ExecAddrOf>();
        const bool wrap = exec.holds<ExecBinary>() || exec.holds<ExecAssignment>()
                          || exec.holds<ExecCompoundAssignment>() || exec.holds<ExecCast>()
                          || exec.holds<ExecRange>() || (postfix && is_prefix);
        if (wrap) {
            open('(');
        }
        print_exec(eid);
        if (wrap) {
            close(')');
        }
    }

    void print_exec_list(IdSlice<ExecId> elems, std::string_view sep) {
        for (auto eidx = elems.begin(); eidx != elems.end(); ++eidx) {
            print_exec(ctx.exec_id(eidx));
            if (eidx != elems.last_elem()) {
                out += sep;
            }
        }
    }

    void print_constant(const ExecComptConstant& c) {
        std::string text;
        switch (c.type_builtin()) {
        case builtin_type::str:
            text += '"';
            for (const char ch : std::string_view{ctx.symbol_id_to_cstr(c.as<SymbolId>())}) {
                append_escaped_char(text, ch, '"');
            }
            text += '"';
            break;
        case builtin_type::charr:
            text += '\'';
            append_escaped_char(text, c.as<char>(), '\'');
            text += '\'';
            break;
        default:
            text = c.to_string(ctx);
            break;
        }
        literal(text);
    }

    static void append_escaped_char(std::string& text, char ch, char quote) {
        switch (ch) {
        case '\n':
            text += "\\n";
            return;
        case '\t':
            text += "\\t";
            return;
        case '\r':
            text += "\\r";
            return;
        case '\0':
            text += "\\0";
            return;
        case '\\':
            text += "\\\\";
            return;
        default:
            break;
        }
        if (ch == quote) {
            text += '\\';
        }
        text += ch;
    }

    OptId<ExecId> chained_else_if(ExecId eid) {
        const Exec& exec = ctx.exec(eid);
        if (exec.holds<ExecBranch>()) {
            return eid;
        }
        if (!exec.holds<ExecBlock>()) {
            return {};
        }
        const Block& block = ctx.block(exec.as<ExecBlock>().block_id);
        if (block.defs.len() || block.execs.len() != 1) {
            return {};
        }
        const ExecId inner = ctx.exec_id(block.execs.begin());
        if (ctx.exec(inner).holds<ExecBranch>()) {
            return inner;
        }
        return {};
    }

    static std::string exec_label(ExecId eid) { return "Exec#" + std::to_string(eid.raw()); }

    void print_exec(ExecId eid) {
        const auto vs = Ovld{
            [&](const ExecBlock& t) { print_block(t.block_id); },
            [&](const ExecJump& t) {
                keyword(t.spot == jump_spot::start ? "continue" : "break");
                out += ' ';
                out += exec_label(t.block);
            },
            [&](const ExecLoop& t) {
                out += exec_label(eid);
                out += ": ";
                if (t.update.has_value()) {
                    keyword("for");
                    out += ' ';
                    open('(');
                    out += "; ";
                    if (t.condition.has_value()) {
                        print_exec(t.condition.as_id());
                    }
                    out += "; ";
                    print_exec(t.update.as_id());
                    close(')');
                    out += ' ';
                } else {
                    keyword("while");
                    out += ' ';
                    if (t.condition.has_value()) {
                        print_exec(t.condition.as_id());
                    } else {
                        literal("true");
                    }
                    out += ' ';
                }
                print_body(t.body);
            },
            [&](const ExecBranch& t) {
                keyword("if");
                out += ' ';
                print_exec(t.condition);
                out += ' ';
                print_body(t.then_block);
                if (t.else_block.has_value()) {
                    out += ' ';
                    keyword("else");
                    out += ' ';
                    const ExecId else_eid = t.else_block.as_id();
                    const OptId<ExecId> chained = chained_else_if(else_eid);
                    if (chained.has_value()) {
                        print_exec(chained.as_id());
                    } else {
                        print_body(else_eid);
                    }
                }
            },
            [&](const ExecReturn& t) {
                keyword("return");
                if (t.return_value.has_value()) {
                    out += ' ';
                    print_exec(t.return_value.as_id());
                }
            },
            [&](const ExecYield& t) {
                keyword("yield");
                if (t.yield_value.has_value()) {
                    out += ' ';
                    print_exec(t.yield_value.as_id());
                }
            },
            [&](const ExecUnionInit& t) {
                const Def& union_def = ctx.def(t.union_def_id);
                type_name(ctx.symbol(union_def.name));
                open('{');
                out += '.';
                out += ctx.symbol(
                    ctx.def(union_def.as<DefUnion>().ordered_members.get(t.active_member_idx))
                        .name);
                out += " = ";
                const bool explicit_move = t.move && !ctx.exec(t.member_init).holds<ExecMove>();
                if (explicit_move) {
                    keyword("move");
                    open('(');
                }
                print_exec(t.member_init);
                if (explicit_move) {
                    close(')');
                }
                close('}');
            },
            [&](const ExecVariantInit& t) {
                type_name(ctx.symbol(ctx.def(t.variant_def_id).name));
                out += "..";
                print_exec(t.payload_init);
            },
            [&](const ExecVariantFieldInit& t) {
                type_name(ctx.symbol(ctx.def(t.variant_field_def_id).name));
                open('(');
                print_exec_list(t.member_inits, ", ");
                close(')');
            },
            [&](const ExecStructInit& t) {
                if (t.anonymous) {
                    open('(');
                    for (const auto eidx : t.member_inits) {
                        print_exec(ctx.exec_id(eidx));
                        out += ", ";
                    }
                    close(')');
                    return;
                }
                const auto& members = ctx.def(t.struct_def_id).as<DefStruct>().ordered_members;
                type_name(ctx.symbol(ctx.def(t.struct_def_id).name));
                open('{');
                for (HirSize i = 0; i < t.member_inits.len(); ++i) {
                    if (i) {
                        out += ", ";
                    }
                    out += '.';
                    out += ctx.symbol(ctx.def(members.get(i)).name);
                    out += " = ";
                    print_exec(ctx.exec_id(t.member_inits.get(i)));
                }
                close('}');
            },
            [&](const ExecVariable& t) { out += ctx.symbol(ctx.def(t.def_id).name); },
            [&](const ExecComptConstant& t) { print_constant(t); },
            [&](const ExecListLiteral& t) {
                open('[');
                print_exec_list(t.elems, ", ");
                close(']');
            },
            [&](const ExecAssignment& t) {
                print_exec(t.lhs);
                out += " = ";
                print_exec(t.rhs);
            },
            [&](const ExecMemberAccess& t) {
                print_postfix_operand(t.owner);
                out += '.';
                out += ctx.symbol(ctx.def(t.member).name);
            },
            [&](const ExecBinary& t) {
                print_operand(t.lhs);
                out += ' ';
                out += binary_op_to_cstr(t.op);
                out += ' ';
                print_operand(t.rhs);
            },
            [&](const ExecCast& t) {
                print_operand(t.exec);
                out += ' ';
                keyword("as");
                out += ' ';
                type_name(type_to_string_with_akas(ctx, t.target_tid));
            },
            [&](const ExecSubscript& t) {
                print_postfix_operand(t.base);
                open('[');
                print_exec(t.index);
                close(']');
            },
            [&](const ExecFnCall& t) {
                print_postfix_operand(t.callee);
                open('(');
                print_exec_list(t.args, ", ");
                close(')');
            },
            [&](const ExecBorrow& t) {
                out += '&';
                if (t.mut) {
                    keyword("mut");
                    out += ' ';
                }
                print_operand(t.borrowee);
            },
            [&](const ExecAddrOf& t) {
                out += '^';
                if (t.mut) {
                    keyword("mut");
                    out += ' ';
                }
                print_operand(t.addressed);
            },
            [&](const ExecDeref& t) {
                out += '*';
                print_operand(t.dereferenced);
            },
            [&](const ExecMatch& t) {
                keyword("match");
                out += ' ';
                print_exec(t.scrutinee);
                out += ' ';
                open('{');
                indent();
                for (const auto eidx : t.branches) {
                    newline();
                    print_exec(ctx.exec_id(eidx));
                    out += ',';
                }
                dedent();
                newline();
                close('}');
            },
            [&](const ExecMatchBranch& t) {
                if (t.patterns.len()) {
                    print_exec_list(t.patterns, " | ");
                } else {
                    keyword("else");
                }
                out += " => ";
                print_exec(t.body);
            },
            [&](const ExecTernaryIf& t) {
                open('(');
                print_operand(t.then_value);
                out += ' ';
                keyword("if");
                out += ' ';
                print_operand(t.condition);
                out += ' ';
                keyword("else");
                out += ' ';
                print_operand(t.else_value);
                close(')');
            },
            [&](const ExecFnPtr& t) {
                fn_name(ctx.symbol_id_slice_to_string(ctx.canonical_name(t.func_def_id)));
            },
            [&](const ExecRange& t) {
                print_operand(t.start);
                out += t.inclusive ? "...=" : "...";
                print_operand(t.end);
            },
            [&](const ExecCompoundAssignment& t) {
                if (t.yield_old) {
                    print_postfix_operand(t.lhs);
                    out += t.op == binary_op::plus ? "++" : "--";
                    return;
                }
                print_exec(t.lhs);
                out += ' ';
                out += binary_op_to_cstr(t.op);
                out += "= ";
                print_exec(t.rhs);
            },
            [&](const ExecUnary& t) {
                out += unary_op_to_cstr(t.op);
                print_operand(t.operand);
            },
            [&](const ExecBuiltinAccess& t) {
                print_postfix_operand(t.from);
                switch (t.kind) {
                case builtin_access::str_len:
                case builtin_access::slice_len:
                    out += ".len";
                    break;
                case builtin_access::str_ptr:
                case builtin_access::slice_ptr:
                    out += ".@ptr";
                    break;
                case builtin_access::variant_discriminant:
                    out += ".@discriminant";
                    break;
                case builtin_access::range_start:
                    out += ".start";
                    break;
                case builtin_access::range_end:
                    out += ".end";
                    break;
                }
            },
            [&](const ExecMove& t) {
                keyword("move");
                open('(');
                print_exec(t.exec);
                close(')');
            },
        };
        ctx.exec(eid).visit(vs);
    }
};

} // namespace

static std::string_view preview_visibility(Context& ctx, DefId did) {
    const Def& def = ctx.def(did);
    if (def.pub || def.holds<DefVariantField>()) {
        return "";
    }
    if (!def.parent.has_value() && !def.top_level) {
        return "";
    }
    return "hid ";
}

std::string def_to_pretty_string_preview(Context& ctx, DefId did,
                                         string_preview_mode preview_mode) {
    const auto vs = Ovld{
        [&ctx, did](const DefModule&) -> std::string {
            std::string str;

            str += "mod ";
            str += ctx.symbol(ctx.def(did).name);

            return str;
        },
        [&ctx, did](const DefFunction& d) -> std::string {
            std::string str;

            if (ctx.def(did).compt) {
                str += "compt ";
            }
            str += "fn ";
            str += ctx.symbol(ctx.def(did).name);
            str += d.maybe_generic_args.has_value()
                       ? gen_args_to_str(ctx, d.maybe_generic_args.as_id())
                       : "";
            str += '(';
            for (const auto tidx : d.param_types) {
                str += type_to_string(ctx, ctx.type_id(tidx));
                if (tidx != d.param_types.last_elem()) {
                    str += ", ";
                }
            }
            str += ") ";
            if (d.return_type.has_value()) {
                str += d.discardable ? "~> " : "-> ";
                str += type_to_string(ctx, d.return_type.as_id());
            }
            if (d.body) {
                str += " {}";

            } else {
                str += ';';
            }

            return str;
        },
        [&ctx, did](const DefGenericFunction&) -> std::string {
            return preview_def_to_string(ctx, did);
        },
        [&ctx, did](const DefFunctionPrototype&) -> std::string {
            return preview_def_to_string(ctx, did);
        },
        [&ctx, did](const DefVariable&) -> std::string { return preview_def_to_string(ctx, did); },
        [&ctx, did](const DefStruct& d) -> std::string {
            std::string str;
            str += "struct ";
            str += ctx.symbol(ctx.def(did).name);

            const auto contracts = d.contracts;
            if (contracts.len()) {
                str += " has ";

                for (const auto didx : contracts) {

                    str += ctx.symbol_id_slice_to_string(ctx.canonical_name(ctx.def_id(didx)));

                    if (didx != contracts.last_elem()) {
                        str += " + ";
                    }
                }
            }

            str += " {\n";

            const auto ordered_mems = d.ordered_members;

            for (const auto didx : ordered_mems) {
                str += "    ";
                str += preview_visibility(ctx, ctx.def_id(didx));
                str += preview_def_to_string(ctx, ctx.def_id(didx));
                str += '\n';
            }

            str += '}';

            return str;
        },
        [&ctx, did](const DefGenericStruct&) -> std::string {
            return preview_def_to_string(ctx, did);
        },
        [&ctx, did](const DefVariant&) -> std::string { return preview_def_to_string(ctx, did); },
        [&ctx, did](const DefGenericVariant&) -> std::string {
            return preview_def_to_string(ctx, did);
        },
        [&ctx, did](const DefVariantField&) -> std::string {
            return preview_def_to_string(ctx, did);
        },
        [&ctx, did](const DefGenericVariantField&) -> std::string {
            return preview_def_to_string(ctx, did);
        },
        [&ctx, did](const DefUnion&) -> std::string { return preview_def_to_string(ctx, did); },
        [&ctx, did](const DefGenericContract&) -> std::string {
            return preview_def_to_string(ctx, did);
        },
        [&ctx, did](const DefContract&) -> std::string { return preview_def_to_string(ctx, did); },
        [&ctx, did](const DefDeftype&) -> std::string { return preview_def_to_string(ctx, did); },
        [&ctx, did](const DefScopeWrapper&) -> std::string {
            return preview_def_to_string(ctx, did);
        },
        [&ctx, did](const DefUnevaluated&) -> std::string {
            return preview_def_to_string(ctx, did);
        },
        [&ctx, did](const DefMalformed&) -> std::string { return preview_def_to_string(ctx, did); },
    };

    std::string cannonical_name_comment = "// canonical name: ";
    cannonical_name_comment += ctx.symbol_id_slice_to_string(ctx.canonical_name(did));

    std::string str;
    switch (preview_mode) {
    case string_preview_mode::no_ticks:
        str += cannonical_name_comment;
        str += '\n';
        str += preview_visibility(ctx, did);
        str += ctx.def(did).visit(vs);
        break;
    case string_preview_mode::ticks:
        str += "```\n";
        str += cannonical_name_comment;
        str += '\n';
        str += preview_visibility(ctx, did);
        str += ctx.def(did).visit(vs);
        str += "\n```";
        break;
    case string_preview_mode::ticks_bear:
        str += "```bear\n";
        str += cannonical_name_comment;
        str += '\n';
        str += preview_visibility(ctx, did);
        str += ctx.def(did).visit(vs);
        str += "\n```";
        break;
    }

    return str;
}

std::string def_to_plain_string(Context& ctx, DefId did) {
    HirPrettyPrinter printer{ctx, false};
    printer.print_def(did, true);
    return printer.take();
}

std::string def_to_string(Context& ctx, DefId did) {
    HirPrettyPrinter printer{ctx, true};
    printer.print_def(did, true);
    return printer.take();
}

std::string scope_to_string(Context& ctx, ScopeId scope) {
    HirPrettyPrinter printer{ctx, true};
    printer.print_scope_contents(scope, {}, true);
    return printer.take();
}

} // namespace hir
