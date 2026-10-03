//     /                              /
//    /                              /
//   /_____  _____  _____  _____    /  _____   _  _  _____
//  /     / /____  /____/ /____/   /  /____/  /\  / /  __
// /_____/ /____  /    / /   \    /  /    /  /  \/ /____/
// Copyright (C) 2025-2026 Zachary Mahan
// Licensed under the Apache License 2.0. See LICENSE for details.

#include "compiler/hir/def.hpp"
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
    // TODO finish
    const auto vs = Ovld{
        [&ctx, did](const DefModule& d) -> std::string {
            std::string str;
            str += "mod ";
            str += ctx.symbold_id_slice_to_string(ctx.canonical_name(did));
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
            str += ctx.symbold_id_slice_to_string(ctx.canonical_name(did));
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
            str += ctx.symbold_id_slice_to_string(ctx.canonical_name(did));
            str += gen_params_to_string(ctx, d.generic_params);
            str += '(';
            for (auto i{0uz}; i < d.param_cnt; ++i) {
                str += "...";
                if (i != d.param_cnt - 1) {
                    str += ", ";
                }
            }
            str += ") -> [return_type]? {...}";
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

                    str += ctx.symbold_id_slice_to_string(ctx.canonical_name(ctx.def_id(didx)));

                    if (didx != contracts.last_elem()) {
                        str += " + ";
                    }
                }

                str += " {\n";

                const auto ordered_mems = d.ordered_members;

                str += "--- ordered members ---\n";
                for (const auto didx : ordered_mems) {
                    str += def_to_string(ctx, ctx.def_id(didx));
                    str += '\n';
                }
                str += "^^^ ordered members ^^^\n";

                str += scope_to_string(ctx, d.scope);

                str += "\n}\n";
            }

            return str;
        },
        [&ctx, did](const DefGenericStruct& d) -> std::string {
            std::string str;
            str += "struct ";
            str += ctx.symbol(ctx.def(did).name);
            str += gen_params_to_string(ctx, d.generic_params);
            str += ';';
            return str;
        },
        [&ctx, did](const DefVariant&) -> std::string {
            std::string str;
            str += "variant ";
            str += ctx.symbol(ctx.def(did).name);
            str += ';';
            return str;
        },
        [&ctx, did](const DefGenericVariant& d) -> std::string {
            std::string str;
            str += "variant ";
            str += ctx.symbol(ctx.def(did).name);
            str += gen_params_to_string(ctx, d.generic_params);
            str += ';';
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
            str += ';';
            return str;
        },
        [&ctx, did](const DefGenericContract& d) -> std::string {
            std::string str;
            str += "contract ";
            str += ctx.symbol(ctx.def(did).name);
            str += gen_params_to_string(ctx, d.generic_params);
            str += ';';
            return str;
        },
        [&ctx, did](const DefContract&) -> std::string {
            std::string str;
            str += "contract ";
            str += ctx.symbol(ctx.def(did).name);
            str += ';';
            return str;
        },
        [&ctx, did](const DefDeftype& d) -> std::string {
            std::string str{"deftype "};
            str += ctx.symbol(ctx.def(did).name);
            str += ' ';
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
} // namespace hir
