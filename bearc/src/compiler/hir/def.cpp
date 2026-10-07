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
#include "compiler/hir/indexing.hpp"
#include <string>

namespace hir {

static std::string scope_id_str(ScopeId sid) { return "Scope#" + std::to_string(sid.raw()); }

std::string scope_to_string(Context& ctx, ScopeId scope) {
    std::string str;
    str += scope_id_str(scope);
    str += "{\n";
    ctx.scope(scope).for_each_local_namespace([&ctx, &str](ScopeIdMap::Entry e) {
        str += def_to_string(ctx, e.val());
        str += '\n';
    });
    ctx.scope(scope).for_each_local_type([&ctx, &str](ScopeIdMap::Entry e) {
        str += def_to_string(ctx, e.val());
        str += '\n';
    });
    ctx.scope(scope).for_each_local_variable([&ctx, &str](ScopeIdMap::Entry e) {
        str += def_to_string(ctx, e.val());
        str += '\n';
    });
    str += "\n}";
    str += scope_id_str(scope);
    return str;
}

std::string def_to_string(Context& ctx, DefId did) {
    const auto vs = Ovld{
        [&ctx, did](const DefModule& d) -> std::string {
            std::string str;
            str += "mod ";
            str += ctx.symbol_id_slice_to_string(ctx.canonical_name(did));
            str += ' ';
            str += scope_to_string(ctx, d.scope);
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
                str += "(compt ";
                str += exec_to_string(ctx, d.compt_value.as_id());
                str += ')';
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
                str += def_to_string(ctx, ctx.def_id(didx));
                str += '\n';
            }

            str += scope_to_string(ctx, d.scope);

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
        [&ctx, did](const DefVariant&) -> std::string {
            std::string str;
            str += "variant ";
            str += ctx.symbol(ctx.def(did).name);
            str += "{}";
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
        [&ctx, did](const DefVariantField&) -> std::string {
            std::string str;
            str += ctx.symbol(ctx.def(did).name);
            str += "()";
            return {};
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

std::string def_to_pretty_string_preview(Context& ctx, DefId did,
                                         string_preview_mode preview_mode) {
    const auto vs = Ovld{
        [&ctx, did](const DefModule& d) -> std::string {
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
        [&ctx, did](const DefGenericFunction&) -> std::string { return def_to_string(ctx, did); },
        [&ctx, did](const DefFunctionPrototype&) -> std::string { return def_to_string(ctx, did); },
        [&ctx, did](const DefVariable&) -> std::string { return def_to_string(ctx, did); },
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
                str += def_to_string(ctx, ctx.def_id(didx));
                str += '\n';
            }

            str += "\n}";

            return str;
        },
        [&ctx, did](const DefGenericStruct&) -> std::string { return def_to_string(ctx, did); },
        [&ctx, did](const DefVariant&) -> std::string { return def_to_string(ctx, did); },
        [&ctx, did](const DefGenericVariant&) -> std::string { return def_to_string(ctx, did); },
        [&ctx, did](const DefVariantField&) -> std::string { return def_to_string(ctx, did); },
        [&ctx, did](const DefUnion&) -> std::string { return def_to_string(ctx, did); },
        [&ctx, did](const DefGenericContract&) -> std::string { return def_to_string(ctx, did); },
        [&ctx, did](const DefContract&) -> std::string { return def_to_string(ctx, did); },
        [&ctx, did](const DefDeftype&) -> std::string { return def_to_string(ctx, did); },
        [&ctx, did](const DefScopeWrapper&) -> std::string { return def_to_string(ctx, did); },
        [&ctx, did](const DefUnevaluated&) -> std::string { return def_to_string(ctx, did); },
        [&ctx, did](const DefMalformed&) -> std::string { return def_to_string(ctx, did); },
    };

    std::string cannonical_name_comment = "// canonical name: ";
    cannonical_name_comment += ctx.symbol_id_slice_to_string(ctx.canonical_name(did));

    std::string str;
    switch (preview_mode) {
    case string_preview_mode::no_ticks:
        str += cannonical_name_comment;
        str += '\n';
        str += ctx.def(did).visit(vs);
        break;
    case string_preview_mode::ticks:
        str += "```\n";
        str += cannonical_name_comment;
        str += '\n';
        str += ctx.def(did).visit(vs);
        str += "\n```";
        break;
    case string_preview_mode::ticks_bear:
        str += "```bear\n";
        str += cannonical_name_comment;
        str += '\n';
        str += ctx.def(did).visit(vs);
        str += "\n```";
        break;
    }

    return str;
}

} // namespace hir
