//     /                              /
//    /                              /
//   /_____  _____  _____  _____    /  _____   _  _  _____
//  /     / /____  /____/ /____/   /  /____/  /\  / /  __
// /_____/ /____  /    / /   \    /  /    /  /  \/ /____/
// Copyright (C) 2025-2026 Zachary Mahan
// Licensed under the GNU GPL v3. See LICENSE for details.

#include "compiler/hir/def_visitor.hpp"
#include "compiler/ast/expr.h"
#include "compiler/ast/stmt.h"
#include "compiler/hir/compt_expr_solver.hpp"
#include "compiler/hir/context.hpp"
#include "compiler/hir/def.hpp"
#include "compiler/hir/diagnostic.hpp"
#include "compiler/hir/indexing.hpp"
#include "compiler/hir/type.hpp"
#include "compiler/hir/type_resolver.hpp"
#include "utils/data_arena.hpp"
#include "llvm/ADT/SmallVector.h"
#include <cassert>
#include <iostream>
#include <optional>
#include <stddef.h>

namespace hir {

void TopLevelDefVisitor::resolve_top_level_definitions() {
    assert(!this->began_resolution && "already began resolving top level declarations");
    this->began_resolution = true;
    auto last_top_level = context.end_def_id();
    for (auto d = context.begin_def_id(); d != last_top_level; d++) {
        visit_as_independent(d);
    }
}
DefId TopLevelDefVisitor::visit_and_resolve_if_needed(DefId def) {
    const auto resol_st = context.resol_state_of(def);
    if (resol_st == Def::resol_state::resolved || resol_st == Def::resol_state::in_progress) {
        return def;
    }
    def_stack.push_back(def);
    context.set_resol_state_of(def, Def::resol_state::in_progress);
    auto d = resolve_def(def);
    context.set_resol_state_of(def, Def::resol_state::resolved);
    def_stack.pop_back();
    return d;
}

DefId TopLevelDefVisitor::visit_as_transparent(DefId def) noexcept {
    context.promote_mention_state_of(def, Def::mention_state::mentioned);
    return def;
}

DefId TopLevelDefVisitor::visit_as_dependent(DefId def) {
    if (context.resol_state_of(def) == Def::resol_state::in_progress) {
        // reports the double diagnostic revealing the origin of the circular def
        report_cycle(def);
        // return as to prevent infinite recursion
        return def;
    }
    context.promote_mention_state_of(def, Def::mention_state::mentioned);
    return visit_and_resolve_if_needed(def);
}

DefId TopLevelDefVisitor::visit_as_independent(DefId def) {
    // no need to check for cycles here since this is an independent def
    return visit_and_resolve_if_needed(def);
}

DefId TopLevelDefVisitor::visit_as_mutator(DefId def) {
    if (context.resol_state_of(def) == Def::resol_state::in_progress) {
        report_cycle(def);
        return def;
    }
    // mutated
    context.promote_mention_state_of(def, Def::mention_state::mutated);
    return visit_and_resolve_if_needed(def);
}

DefId TopLevelDefVisitor::resolve_def(DefId did) {

    auto check_to_err_when_compt_is_mut = [&](TypeId tid, const Def& def) {
        if (contains_mut(context, tid) && def.compt) {
            context.emplace_diagnostic(context.type(tid).span,
                                       diag_code::compt_variable_should_be_immutable,
                                       diag_type::error);
        }
    };

    const ast_stmt* stmt = context.def_ast_node(did);
    ScopeId scope = context.containing_scope(did);
    Span span = context.def(did).span;

    auto parent_is_struct = [this](const Def& def) {
        return (def.parent.has_value()) ? context.is_struct_def(def.parent.as_id()) : false;
    };

    switch (stmt->type) {
    case AST_STMT_VAR_DECL: {
        Def& def = context.def(did);
        OptId<TypeId> maybe_tid = TypeResolver<TopLevelDefVisitor>{context, *this}.resolve_type(
            span.file_id, scope, stmt->stmt.var_decl.type, parent_is_struct(def));
        if (!maybe_tid.has_value()) {
            goto cleanup;
        }
        // compt =/= mut guard
        check_to_err_when_compt_is_mut(maybe_tid.as_id(), def);
        if (def.compt) {
            context.emplace_diagnostic_with_message_value(
                def.span, diag_code::a_compt_variable_should_be_explicitly_initialized,
                diag_type::error, DiagnosticSymbolBeforeMessage{.sid = def.name});
        }
        // error when struct member does not have an explicit type

        const bool type_contains_var = TypeTransformer<TypeContainsVar>{context}(maybe_tid.as_id());
        if (!def.statik && parent_is_struct(def) && type_contains_var) {
            const Type& type = context.type(maybe_tid.as_id());
            context.emplace_diagnostic_with_message_value(
                type.span, diag_code::should_have_explicit_type, diag_type::error,
                DiagnosticStructMemberSymBeforeMsg{.mem_sid = def.name});
        };
        def.set_value(DefVariable{.type_id = maybe_tid.as_id(), .compt_value = std::nullopt});
        // TODO handle invalid non-initialized statements
        // search for default value/default method
        break;
    }
    case AST_STMT_VAR_INIT_DECL: {
        const auto var_init_decl = stmt->stmt.var_init_decl;
        OptId<TypeId> maybe_tid = TypeResolver<TopLevelDefVisitor>{context, *this}.resolve_type(
            span.file_id, scope, var_init_decl.type,
            parent_is_struct(context.def(did))); // needs layout info if parent is struct
        if (!maybe_tid.has_value()) {
            goto cleanup; // maybe set a special value to indicate error differently
        }
        const bool type_contains_var = TypeTransformer<TypeContainsVar>{context}(maybe_tid.as_id());
        if (context.def(did).compt && type_contains_var) {
            context.emplace_diagnostic(context.type(maybe_tid.as_id()).span,
                                       diag_code::compt_variable_should_have_an_explicit_type,
                                       diag_type::error);
        }
        // compt =/= mut guard
        check_to_err_when_compt_is_mut(maybe_tid.as_id(), context.def(did));

        const auto maybe_compt_eid
            = ComptExprSolver(context, *this)
                  .solve_expr(span.file_id, scope, stmt->stmt.var_init_decl.rhs, maybe_tid.as_id());

        // error when struct member does not have an explicit type
        if (!context.def(did).statik && parent_is_struct(context.def(did)) && type_contains_var) {
            const Type& type = context.type(maybe_tid.as_id());
            auto d0 = context.emplace_diagnostic_with_message_value(
                type.span, diag_code::should_have_explicit_type, diag_type::error,
                DiagnosticStructMemberSymBeforeMsg{.mem_sid = context.def(did).name});

            // if type is inferable from its compt default value, suggest changing type to said type
            if (maybe_compt_eid.has_value()) {
                OptId<TypeId> maybe_tid
                    = ComptExprSolver{context, *this}.infer_type_from_exec(maybe_compt_eid.as_id());
                if (maybe_tid.has_value()) {
                    auto d1 = context.emplace_diagnostic_with_message_value(
                        type.span, diag_code::replace_with, diag_type::help,
                        DiagnosticTypeAfterMessage{.tid = maybe_tid.as_id()});
                    context.link_diagnostic(d0, d1);
                }
            }
        };

        context.def(did).set_value(
            DefVariable{.type_id = maybe_tid.as_id(), .compt_value = maybe_compt_eid});
        // check poison /not init
        if (context.def(did).compt && var_init_decl.assign_op->type == TOK_ASSIGN_MOVE) {
            auto d0 = context.emplace_diagnostic(
                Span{context, context.def(did).span.file_id, var_init_decl.assign_op},
                diag_code::compt_vars_should_not_be_move_initialized, diag_type::error);
            if (maybe_compt_eid.has_value()) {
                auto d1 = context.emplace_diagnostic(
                    context.exec(maybe_compt_eid.as_id()).span,
                    diag_code::compile_time_constant_cannot_be_moved, diag_type::note);
                context.link_diagnostic(d0, d1);
            }
        }
        // TODO only issue this if expr is a valid run-time statement, but not compt statement, this
        // will require trying to do a run-time expr lowering on the init expression, which isn't
        // impl'd yet
        if (!maybe_compt_eid.has_value()) {
            if (!context.def(did).compt && var_init_decl.rhs->type != AST_EXPR_STATIC_ASSERT) {
                context.emplace_diagnostic(
                    Span{context, context.def(did).span.file_id, stmt->stmt.var_init_decl.rhs},
                    diag_code::all_runtime_glob_and_mem_vars_need_compt_init, diag_type::note,
                    DiagnosticInfoDontDisplayFile{});
            }
            goto cleanup;
        }
        break;
    }
    case AST_STMT_STRUCT_DEF: {
        auto strct = stmt->stmt.struct_decl;
        HirSize prior_diag_count = context.diagnostic_count();
        // delay resolution/instantiation until specialization
        if (context.def(did).generic) {
            const auto maybe_generic_params = resolve_generic_params(
                context.def(did).span.file_id, context.containing_scope(did), strct.generic_params);
            if (!maybe_generic_params.has_value()) {
                goto cleanup;
            }
            context.def(did).set_value(DefGenericStruct{
                .generics_args_to_concrete_defs_map = context.make_generic_args_map_and_get_id(),
                .generic_params = maybe_generic_params.value()});
            goto cleanup;
        }

        ScopeId structs_scope = context.scope_for_top_level_def(did);
        context.register_generated_deftype(
            structs_scope, context.symbol_id<"Self">(),
            // TODO properly handle generic args here
            context.emplace_type(TypeStruct{.def_id = did, .gen_args_slice = {}, .generic = false},
                                 Span::generated(), false),
            did, Span{context, context.def(did).span.file_id, strct.name});
        // ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

        IdSlice<DefId> ordered_defs = context.ordered_defs_for(did);
        // visit each orderer member (variable), since the struct is actually dependent on those
        // defs
        for (auto didx = ordered_defs.begin(); didx != ordered_defs.end(); didx++) {
            visit_as_dependent(context.def_id(didx));
        }

        llvm::SmallVector<DefId> contract_dids{};
        DiagLinker dlinker{context};
        for (HirSize i = 0; i < strct.contracts.len; i++) {
            const ast_expr_t* contract = strct.contracts.start[i];
            if (contract->type != AST_EXPR_ID) {
                continue;
            }
            Span ctr_span{context, context.def(did).span.file_id, contract->expr.id.slice};
            auto maybe_contract_did = context.look_up_scoped_type(
                scope, context.symbol_slice(contract->expr.id.slice), ctr_span);
            if (maybe_contract_did.empty()) {
                auto d = context.emplace_diagnostic(
                    ctr_span, diag_code::use_of_undeclared_identifier, diag_type::error,
                    DiagnosticSubCode{.sub_code = diag_code::not_a_contract});
                dlinker.link(d);
                continue;
            }
            auto contract_did = visit_and_resolve_if_needed(maybe_contract_did.as_id());
            const Def& contract_def = context.def(contract_did);

            if (!contract_def.holds<DefContract>()) {
                auto d = context.emplace_diagnostic(
                    ctr_span, diag_code::invalid_contract, diag_type::error,
                    DiagnosticSubCode{.sub_code = diag_code::not_a_contract});
                dlinker.link(d);
                auto d1 = context.emplace_diagnostic(context.def(did).span,
                                                     diag_code::declared_here, diag_type::note);
                dlinker.link(d1);
                continue;
            }
            // try to insert contract into struct's scope
            auto already_defined
                = Scope::look_up_local_type(context, structs_scope, contract_def.name);
            if (already_defined.has_value()) {
                // do diagnostics for the redefinition
                if (context.def(already_defined.as_id()).holds<DefContract>()) {
                    dlinker.link(context.emplace_diagnostic_with_message_value(
                        ctr_span, diag_code::struct_already_has_contract, diag_type::error,
                        DiagnosticSymbolAfterMessage{contract_def.name}));
                } else {
                    dlinker.link(context.emplace_diagnostic(ctr_span, diag_code::redefined_symbol,
                                                            diag_type::error));
                    dlinker.link(context.emplace_diagnostic(
                        context.name_span_for_def(already_defined.as_id()),
                        diag_code::previous_def_here, diag_type::note));
                }
                continue;
            }
            context.insert_type(structs_scope, contract_def.name, contract_did);
            contract_dids.push_back(contract_did);
        }

        auto contracts = context.freeze_id_vec(contract_dids);

        context.def(did).set_value(DefStruct{
            .scope = structs_scope,
            .ordered_members = ordered_defs,
            /* .contracts = */
            .contracts = contracts,
            .orginal = {},
            .maybe_generic_args = {}, // not generic
        });

        try_satisfy_contracts(did, contracts);

        const auto maybe_generic_args = context.search_for_gen_args_for_def(did);
        HirSize posterior_diag_count = context.diagnostic_count();
        if (posterior_diag_count > prior_diag_count && maybe_generic_args.has_value()) {
            context.emplace_diagnostic(context.span_for_gen_args(maybe_generic_args.as_id()),
                                       diag_code::in_generic_instantiated_here, diag_type::note);
        }

        break;
    }
    case AST_STMT_USE: {
        auto use = stmt->stmt.use;
        auto sid_slice = context.symbol_slice(use.id);
        // to be used as the name
        const token_t* last_symbol = use.id.start[use.id.len - 1];
        Span id_span{span.file_id, context.ast(span.file_id).buffer(), use.id.start[0],
                     last_symbol};

        // by default, look up a mod (a namespace). If `use mod` was NOT explicitly specified, then
        // look for a type only if a mod was NOT found. This statys in line with the "favor modules"
        // philosophy
        const bool only_look_for_mod = use.mod;
        bool used_mod = true;
        OptId<DefId> used_did = context.look_up_scoped_namespace(scope, sid_slice, id_span);
        if (!only_look_for_mod && used_did.empty()) {
            used_did = context.look_up_scoped_type(scope, sid_slice, id_span);
            used_mod = false;
        }

        if (used_did.empty()) {
            auto code = only_look_for_mod ? diag_code::use_of_undeclared_mod
                                          : diag_code::use_of_undeclared_identifier;

            context.emplace_diagnostic_with_message_value(
                id_span, code, diag_type::error,
                DiagnosticIdentifierAfterMessage{.sid_slice = sid_slice});

            break; // don't insert!
        }
        ScopeId scope_into_which_to_insert = context.containing_scope(did);
        // insert base name into containing scope
        if (used_mod) {
            context.scope(scope_into_which_to_insert)
                .insert_namespace(context.symbol_id(last_symbol), used_did.as_id());
        } else {
            context.scope(scope_into_which_to_insert)
                .insert_type(context.symbol_id(last_symbol), used_did.as_id());
        }
        break;
    }
    case AST_STMT_DEFTYPE: {
        if (stmt->stmt.deftype.aliased_type_expr->type != AST_EXPR_TYPE) {
            goto cleanup; // already malformed during parsing
        }
        OptId<TypeId> maybe_type = TypeResolver<TopLevelDefVisitor>{context, *this}.resolve_type(
            span.file_id, scope, stmt->stmt.deftype.aliased_type_expr->expr.type_expr.type);
        if (!maybe_type.has_value()) {
            goto cleanup;
        }

        context.def(did).set_value(DefDeftype{.type = maybe_type.as_id()});

        break;
    }
    case AST_STMT_FN_PROTOTYPE: {
        const ast_stmt_fn_decl fn_decl = stmt->stmt.fn_prototype;
        const auto fid = context.def(did).span.file_id;

        OptId<TypeId> maybe_self_type = context.self_type_for_fn(scope, &fn_decl, context.def(did));
        DefFunction::ParamResolResult params_res
            = resolve_params(fid, scope, did, fn_decl.params, maybe_self_type);

        auto params = params_res.params;

        llvm::SmallVector<TypeId> type_vec{};

        const bool func_is_runtime = !context.def(did).compt;

        auto parent_is_contract = [this](const Def& def) -> bool {
            if (def.parent.empty()) {
                return false;
            }
            if (context.def(visit_and_resolve_if_needed(def.parent.as_id())).holds<DefContract>()) {
                return true;
            }
            return false;
        };

        for (auto didx = params.begin(); didx != params.end(); didx++) {
            const Def& param_def = context.def(didx);
            assert(param_def.holds<DefVariable>());

            // guard run-time func with `var` typed params
            if (func_is_runtime
                && !(parent_is_contract(context.def(did)) && (didx == params.begin()))
                && TypeTransformer<TypeContainsVar>{context}(param_def.as<DefVariable>().type_id)) {
                const Span ty_span = context.type(param_def.as<DefVariable>().type_id).span;
                auto d0 = context.emplace_diagnostic(
                    ty_span, diag_code::type_deduction_not_legal_here, diag_type::error);
                auto d1 = context.emplace_diagnostic(
                    param_def.span, diag_code::non_compt_function_params_must_have_explicit_types,
                    diag_type::note);
                context.link_diagnostic(d0, d1);
            }
            type_vec.push_back(param_def.as<DefVariable>().type_id);
        }

        auto param_types = context.freeze_id_vec(type_vec);

        auto return_tid
            = (fn_decl.return_type)
                  ? TypeResolver{context, *this}.resolve_type(fid, scope, fn_decl.return_type)
                  : std::nullopt;

        if (return_tid.has_value() && !parent_is_contract(context.def(did))) {
            context.report_invalid_return_type(return_tid.as_id());
        }

        context.def(did).set_value(DefFunctionPrototype{.params = params,
                                                        .param_types = param_types,
                                                        .return_type = return_tid,
                                                        .takes_self = maybe_self_type.has_value()});

        break;
    }
    case AST_STMT_FN_DECL: {
        const ast_stmt_fn_decl fn_decl = stmt->stmt.fn_decl;
        Def& def = context.def(did);
        const auto fid = def.span.file_id;
        const HirSize prior_diag_count = context.diagnostic_count();

        if (def.compt && fn_decl.is_mut) {
            Span span = Span::find_between_tokens(context, fid, fn_decl.kw, fn_decl.name.start[0]);
            auto d0 = context.emplace_diagnostic(
                span, diag_code::compt_mut_methods_are_not_permitted, diag_type::error);
            auto d1 = context.emplace_diagnostic(
                span, diag_code::compt_expression_functions_can_only_act_on_immut_vals,
                diag_type::note, DiagnosticInfoNoPreview{});
            auto d2 = context.emplace_diagnostic_with_message_value(
                span, diag_code::remove, diag_type::help,
                DiagnosticSymbolAfterMessage{context.symbol_id(span)});
            context.link_diagnostic(d0, d1);
            context.link_diagnostic(d1, d2);
        }
        if (def.compt && !fn_decl.only_expr) {
            Span span{context, fid, fn_decl.block->first};
            auto d0 = context.emplace_diagnostic(
                span, diag_code::compt_function_does_not_yield_a_pure_expr, diag_type::error);
            auto d1 = context.emplace_diagnostic_with_message_value(
                span, diag_code::declare_using_pure_expression_syntax_replacing_body_with,
                diag_type::help,
                DiagnosticSymbolAfterMessage{.sid = context.symbol_id<"=> (Expression)">()});
            context.link_diagnostic(d0, d1);
        }

        OptId<TypeId> maybe_self_type = context.self_type_for_fn(scope, &fn_decl, def);

        bool takes_self = maybe_self_type.has_value();

        DefFunction::ParamResolResult params_res
            = resolve_params(fid, scope, did, fn_decl.params, maybe_self_type);

        auto params = params_res.params;

        llvm::SmallVector<TypeId> type_vec{};

        const bool func_is_runtime = !def.compt;

        for (auto didx = params.begin(); didx != params.end(); didx++) {
            const Def& param_def = context.def(didx);
            assert(param_def.holds<DefVariable>());

            if (didx == params.begin() && !takes_self && parent_is_struct(context.def(did))
                && context.type_matches_struct_def(param_def.as<DefVariable>().type_id,
                                                   def.parent.as_id())) {
                takes_self = true;
            }

            // guard run-time func with `var` typed params
            if (func_is_runtime
                && TypeTransformer<TypeContainsVar>{context}(param_def.as<DefVariable>().type_id)) {
                const Span ty_span = context.type(param_def.as<DefVariable>().type_id).span;
                auto d0 = context.emplace_diagnostic(
                    ty_span, diag_code::type_deduction_not_legal_here, diag_type::error);
                auto d1 = context.emplace_diagnostic(
                    param_def.span, diag_code::non_compt_function_params_must_have_explicit_types,
                    diag_type::note);
                auto d2 = context.emplace_diagnostic(
                    ty_span, diag_code::use_a_generic_function_parameter_and_specify_it_as_a_type,
                    diag_type::help);
                context.link_diagnostic(d0, d1);
                context.link_diagnostic(d1, d2);
            }
            type_vec.push_back(param_def.as<DefVariable>().type_id);
        }

        const IdSlice<TypeId> param_types = context.freeze_id_vec(type_vec);

        const OptId<TypeId> return_tid
            = (fn_decl.return_type)
                  ? TypeResolver{context, *this}.resolve_type(fid, scope, fn_decl.return_type)
                  : std::nullopt;
        if (return_tid.has_value()) {
            context.report_invalid_return_type(return_tid.as_id());
        }

        if (context.def(did).generic) {
            const auto maybe_generic_params
                = resolve_generic_params(context.def(did).span.file_id,
                                         context.containing_scope(did), fn_decl.generic_params);
            if (!maybe_generic_params.has_value()) {
                goto cleanup;
            }
            context.def(did).set_value(DefGenericFunction{
                .generics_args_to_concrete_defs_map = context.make_generic_args_map_and_get_id(),
                .generic_params = maybe_generic_params.value()});
            goto cleanup;
        }

        const HirSize posterior_diag_count = context.diagnostic_count();

        const auto maybe_generic_args = context.search_for_gen_args_for_def(did);

        if (posterior_diag_count > prior_diag_count && maybe_generic_args.has_value()) {
            context.emplace_diagnostic(context.span_for_gen_args(maybe_generic_args.as_id()),
                                       diag_code::in_generic_instantiated_here, diag_type::note);
        }

        // handle methods explicitly
        def.set_value(DefFunction{.params = params,
                                  .param_types = param_types,
                                  .return_type = return_tid,
                                  .body = std::nullopt,
                                  .original = std::nullopt, // not generic
                                  .maybe_generic_args = {}, // not generic
                                  .discardable = fn_decl.discardable,
                                  .takes_self = takes_self,
                                  .posioned = params_res.poisoned});
        break;
    }
    case AST_STMT_CONTRACT_DEF: {
        ScopeId contract_scope = context.scope_for_top_level_def(did);
        context.register_generated_deftype(
            contract_scope, context.symbol_id<"Self">(),
            context.emplace_type(TypeVar{}, Span::generated(), false), did,
            Span{context, context.def(did).span.file_id, stmt->stmt.contract_decl.name});
        IdSlice<DefId> ordered_fns = context.ordered_defs_for(did);
        for (auto didx = ordered_fns.begin(); didx != ordered_fns.end(); ++didx) {
            visit_as_transparent(context.def_id(didx));
        }
        context.def(did).set_value(DefContract{.funcs = ordered_fns, .scope = contract_scope});
        break;
    }
    case AST_STMT_UNION_DEF: {
        // visit children
        auto members = context.ordered_defs_for(did);
        for (auto didx = members.begin(); didx != members.end(); didx++) {
            visit_as_dependent(context.def_id(didx));
        }
        context.def(did).set_value(
            DefUnion{.scope = context.scope_for_top_level_def(did), .ordered_members = members});

        break;
    }
    case AST_STMT_VARIANT_DEF: {

        HirSize prior_diag_count = context.diagnostic_count();

        if (context.def(did).generic) {
            const auto maybe_generic_params = resolve_generic_params(
                context.def(did).span.file_id, context.containing_scope(did),
                stmt->stmt.variant_decl.generic_params);
            if (!maybe_generic_params.has_value()) {
                goto cleanup;
            }
            context.def(did).set_value(DefGenericVariant{
                .generics_args_to_concrete_defs_map = context.make_generic_args_map_and_get_id(),
                .generic_params = maybe_generic_params.value()});
            goto cleanup;
        }
        auto members = context.ordered_defs_for(did);
        for (auto didx = members.begin(); didx != members.end(); didx++) {
            visit_as_dependent(context.def_id(didx));
        }

        const HirSize posterior_diag_count = context.diagnostic_count();

        const auto maybe_generic_args = context.search_for_gen_args_for_def(did);

        if (posterior_diag_count > prior_diag_count && maybe_generic_args.has_value()) {
            context.emplace_diagnostic(context.span_for_gen_args(maybe_generic_args.as_id()),
                                       diag_code::in_generic_instantiated_here, diag_type::note);
        }

        context.def(did).set_value(DefVariant{
            .scope = context.scope_for_top_level_def(did),
            .ordered_members = members,
            .orginal = {},
            .maybe_generic_args = {} // not generic
        });

        break;
    }
    case AST_STMT_VARIANT_FIELD_DECL: {
        const ast_stmt_variant_field_decl_t vf = stmt->stmt.variant_field_decl;
        const ast_slice_of_params_t params = vf.params;

        llvm::SmallVector<DefId> param_vec;

        for (size_t i = 0; i != params.len; i++) {
            const ast_param_t* param = params.start[i];
            if (!param->valid) {
                continue;
            }
            const auto fid = context.def(did).span.file_id;
            const auto maybe_tid
                = TypeResolver{context, *this}.resolve_type(fid, scope, param->type);
            if (maybe_tid.empty()) {
                continue;
            }
            const auto tid = maybe_tid.as_id();
            param_vec.push_back(context.register_def(
                context.symbol_id(param->name), Span{context, fid, param->first, param->last},
                context.def(did).parent.as_id(), DefVariable{.type_id = tid}));
        }

        IdSlice<DefId> members = context.freeze_id_vec(param_vec);

        context.def(did).set_value(
            DefVariantField{.scope = context.scope_for_top_level_def(did), .members = members});
        break;
    }

        // the rest are not possible (already handled)/shouldn't be resolved at top level
    case AST_STMT_FILE:
    case AST_STMT_EXTERN_BLOCK:
    case AST_STMT_MODULE:
    case AST_STMT_VISIBILITY_MODIFIER:
    case AST_STMT_COMPT_MODIFIER:
    case AST_STMT_STATIC_MODIFIER:
    case AST_STMT_IMPORT:
    case AST_STMT_BLOCK:
    case AST_STMT_EXPR:
    case AST_STMT_EMPTY:
    case AST_STMT_BREAK:
    case AST_STMT_IF:
    case AST_STMT_ELSE:
    case AST_STMT_WHILE:
    case AST_STMT_FOR:
    case AST_STMT_FOR_IN:
    case AST_STMT_RETURN:
    case AST_STMT_CONTINUE:
    case AST_STMT_YIELD:
    case AST_STMT_ALIGNAS_MODIFIER:
    case AST_STMT_INVALID:
        break;
    }
cleanup:
    context.relinquish_temp_scopes();
    return did;
}

void TopLevelDefVisitor::report_cycle(DefId culprit) {
    // culprit is the origin, but accomplice has just referred to the culprit
    assert(!def_stack.empty());
    DefId accomplice = def_stack[def_stack.size() - 1];

    DiagnosticId accomplice_diag
        = context.emplace_diagnostic(context.make_top_level_def_name_span(accomplice),
                                     diag_code::circular_definition, diag_type::error);

    DiagnosticId prev_diag = accomplice_diag;

    for (auto it = def_stack.rbegin(); it != def_stack.rend(); it++) {
        DefId d = *it;
        if (d != culprit && it != def_stack.rbegin()) {
            DiagnosticId curr_diag = context.emplace_diagnostic(
                context.make_top_level_def_name_span(d), diag_code::circular_definition_passes_thru,
                diag_type::note);
            // set correlation inside of context
            context.link_diagnostic(prev_diag, curr_diag);
            // cycle
            prev_diag = curr_diag;

        } else /* is culprit */ {
            DiagnosticId culprit_diag = context.emplace_diagnostic(
                context.make_top_level_def_name_span(culprit),
                diag_code::circular_definition_origin, diag_type::note);
            // set correlation inside of context
            context.link_diagnostic(prev_diag, culprit_diag);
            // found, so return out of here
            return;
        }
    }
    // unreachable since we *should* find the culprit in the def_stack
    assert(false && "failed to find culprit defintion when reporting a circular defintion");
}

OptId<DefId> TopLevelDefVisitor::resolve_param(FileId fid, ScopeId scope, DefId func_def,
                                               const ast_param_t* param) {

    if (!param->valid) {
        return std::nullopt;
    }

    Span span{context, fid, param->first, param->last};

    auto maybe_tid = TypeResolver{context, *this}.resolve_type(fid, scope, param->type);

    if (maybe_tid.empty()) {
        return std::nullopt;
    }

    auto tid = maybe_tid.as_id();

    SymbolId name = context.symbol_id(param->name);

    return resolve_param(fid, scope, func_def, tid, name, span);
}

OptId<DefId> TopLevelDefVisitor::resolve_param(FileId fid, ScopeId scope, DefId func_def,
                                               TypeId tid, SymbolId name, Span span) {
    auto param_did = context.register_compt_def(name, span, func_def);

    context.def(param_did).set_value(DefVariable{.type_id = tid, .compt_value = std::nullopt});

    return param_did;
}

[[nodiscard]] DefFunction::ParamResolResult
TopLevelDefVisitor::resolve_params(FileId fid, ScopeId scope, DefId func_def,
                                   const ast_slice_of_params_t params, OptId<TypeId> self_type) {

    llvm::SmallVector<DefId> vec;

    auto freeze_params = [this, &vec](bool poisoned) {
        return DefFunction::ParamResolResult{.params = context.freeze_id_vec(vec),
                                             .poisoned = poisoned};
    };

    if (self_type.has_value()) {
        const auto* fn_node = context.def_ast_node(func_def);
        assert(fn_node->type == AST_STMT_FN_DECL || fn_node->type == AST_STMT_FN_PROTOTYPE);
        auto hopefully_self_param
            = resolve_param(fid, scope, func_def, self_type.as_id(), context.symbol_id<"self">(),
                            Span{context, fid, fn_node->stmt.fn_decl.kw});
        if (hopefully_self_param.empty()) {
            return freeze_params(true); // poisoned
        }
        vec.push_back(hopefully_self_param.as_id());
    }

    for (HirSize i = 0; i < params.len; i++) {
        const ast_param_t* param = params.start[i];
        OptId<DefId> maybe_param = resolve_param(fid, scope, func_def, param);
        if (maybe_param.empty()) {
            return freeze_params(true); // poisoned
        }
        vec.push_back(maybe_param.as_id());
    }

    return freeze_params(false); // not poisoned
}

bool TopLevelDefVisitor::try_satisfy_contract(DefId struct_did, DefId contract_did) {

    const Def& struct_def = context.def(struct_did);
    assert(struct_def.holds<DefStruct>());
    DefStruct strukt = struct_def.as<DefStruct>();
    const Def& contract_def = context.def(contract_did);
    assert(contract_def.holds<DefContract>());
    DefContract contract = contract_def.as<DefContract>();

    ScopeId struct_scope = strukt.scope;

    DiagLinker dlinker{context};

    bool cooked = false;

    for (auto didx = contract.funcs.begin(); didx != contract.funcs.end(); ++didx) {

        DefId ct_func_did = visit_and_resolve_if_needed(context.def_id(didx));
        const Def& ct_func_def = context.def(ct_func_did);

        OptId<DefId> maybe_matching_named_func_in_struct
            = context.look_up_variable(struct_scope, ct_func_def.name);

        if (maybe_matching_named_func_in_struct.empty()) {
            auto d = context.emplace_diagnostic_with_message_value(
                context.name_span_for_def(struct_did), diag_code::only_message_value_is_meaningful,
                diag_type::error,
                DiagnosticStructDoesNotDefineBlankForContract{.struct_name = struct_def.name,
                                                              .func_name = ct_func_def.name,
                                                              .contract_name = contract_def.name});
            dlinker.link(d);
            dlinker.link(context.emplace_diagnostic_with_message_value(
                ct_func_def.span, diag_code::declared_in_contract_here, diag_type::note,
                DiagnosticSymbolBeforeMessage{.sid = ct_func_def.name}));
            cooked = true;
            continue;
        }

        // make sure fn is resolved
        auto matched_fn_did
            = visit_and_resolve_if_needed(maybe_matching_named_func_in_struct.as_id());

        const Def& matching_def = context.def(matched_fn_did);

        if (!matching_def.holds<DefFunction>()) {
            auto d = context.emplace_diagnostic(
                matching_def.span, diag_code::only_message_value_is_meaningful, diag_type::error,
                DiagnosticStructDoesNotDefineBlankForContract{.struct_name = struct_def.name,
                                                              .func_name = ct_func_def.name,
                                                              .contract_name = contract_def.name},
                DiagnosticSubCode{.sub_code = diag_code::not_a_function});
            dlinker.link(d);
            cooked = true;
            continue;
        }

        if (contract_def.compt && !matching_def.compt) {
            dlinker.link(context.emplace_diagnostic_with_message_value(
                matching_def.span, diag_code::should_be_declared_compt, diag_type::error,
                DiagnosticSymbolBeforeMessage{.sid = matching_def.name}));
            dlinker.link(context.emplace_diagnostic_with_message_value(
                ct_func_def.span, diag_code::declared_in_contract_here, diag_type::note,
                DiagnosticSymbolBeforeMessage{.sid = ct_func_def.name}));
        }

        if (!context.func_sigs_match_for_contract(ct_func_did, matched_fn_did)) {
            dlinker.link(context.emplace_diagnostic(
                context.name_span_for_def(matched_fn_did),
                diag_code::only_message_value_is_meaningful, diag_type::error,
                DiagnosticStructDoesNotDefineBlankForContract{.struct_name = struct_def.name,
                                                              .func_name = ct_func_def.name,
                                                              .contract_name = contract_def.name},
                DiagnosticSubCode{.sub_code
                                  = diag_code::function_signature_does_not_match_contract}));

            dlinker.link(
                context.report_function_disagreement_with_contract(ct_func_did, matched_fn_did));

            dlinker.link(context.emplace_diagnostic_with_message_value(
                ct_func_def.span, diag_code::declared_in_contract_here, diag_type::note,
                DiagnosticSymbolBeforeMessage{.sid = ct_func_def.name}));

            cooked = true;
        }
    }
    if (cooked) {
        dlinker.link(context.emplace_diagnostic_with_message_value(
            contract_def.span, diag_code::declared_here, diag_type::note,
            DiagnosticSymbolBeforeMessage{.sid = contract_def.name}));
    }
    return cooked;
}

bool TopLevelDefVisitor::try_satisfy_contracts(DefId struct_did, IdSlice<DefId> contract_dids) {
    bool cooked = false;
    for (auto didx = contract_dids.begin(); didx != contract_dids.end(); ++didx) {
        cooked |= try_satisfy_contract(struct_did, context.def_id(didx));
    }
    return cooked;
}

[[nodiscard]] std::optional<IdSlice<GenericParamId>>
TopLevelDefVisitor::resolve_generic_params(FileId fid, ScopeId scope,
                                           ast_slice_of_generic_params_t gen_params) {
    llvm::SmallVector<GenericParamId> param_vec;
    DataArena arena{0x400}; // TOOD, get from context probably
    IdHashMap<SymbolId, GenericParamId> param_map{arena, 1000};
    bool cooked = false;
    for (size_t i = 0; i < gen_params.len; ++i) {
        const OptId<GenericParamId> maybe_param_id
            = resolve_generic_param(fid, scope, gen_params.start[i]);
        if (maybe_param_id.empty()) {
            cooked = true;
            continue;
        }
        const Id<GenericParam> param_id = maybe_param_id.as_id();
        const SymbolId name = context.gen_param(param_id).name;
        const OptId<GenericParamId> maybe_defined_already = param_map.at(name);
        if (param_map.contains(name)) {
            const auto d0 = context.emplace_diagnostic(
                context.gen_param(param_id).span, diag_code::redefined_symbol, diag_type::error);
            const auto d1
                = context.emplace_diagnostic(context.gen_param(maybe_defined_already.as_id()).span,
                                             diag_code::previous_def_here, diag_type::note);
            context.link_diagnostic(d0, d1);
            cooked = true;
            continue;
        }

        param_map.insert(name, param_id); // register name -> param_id mapping to check redefinition
        param_vec.push_back(
            param_id); // add param_id to list of all params to be made into a param slice
    }

    if (cooked) {
        return {};
    }
    return context.freeze_id_vec(param_vec);
}
[[nodiscard]] OptId<GenericParamId>
TopLevelDefVisitor::resolve_generic_param(FileId fid, ScopeId scope,
                                          const ast_generic_parameter_t* gen_param) {

    switch (gen_param->tag) {
    case AST_GENERIC_PARAM_TYPE: {
        if (!gen_param->param.generic_type->valid) {
            return {};
        }
        const auto* id = gen_param->param.generic_type->id;
        const SymbolId name = context.symbol_id(id);
        IdSlice<DefId> contracts = resolve_contracts_for_generic_param(
            fid, scope, gen_param->param.generic_type->contract_ids);
        return context.emplace_generic_param(Span{context, fid, gen_param->first, gen_param->last},
                                             GenericParamType{contracts}, name);
    }
    case AST_GENERIC_PARAM_VAR: {
        const auto gen_var = *gen_param->param.generic_var;

        if (!gen_var.valid) {
            return {};
        }

        const auto maybe_tid = TypeResolver{context, *this}.resolve_type(fid, scope, gen_var.type);
        if (maybe_tid.empty()) {
            return {};
        }
        SymbolId name = context.symbol_id(gen_var.name);
        return context.emplace_generic_param(Span{context, fid, gen_param->first, gen_param->last},
                                             GenericParamVariable{.type = maybe_tid.as_id()}, name);
    }
    case AST_GENERIC_PARAM_INVALID:
        return {};
    }

    return {};
}

IdSlice<DefId>
TopLevelDefVisitor::resolve_contracts_for_generic_param(FileId fid, ScopeId scope,
                                                        ast_slice_of_exprs_t contracts) {

    llvm::SmallVector<DefId> contract_vec{};

    auto valid = +[](const ast_expr_t* expr) -> bool { return expr->type == AST_EXPR_ID; };

    for (size_t i = 0; i < contracts.len; ++i) {
        const ast_expr_t* contract = contracts.start[i];
        if (!valid(contract)) {
            return {};
        }
        const auto id_slice = contract->expr.id.slice;
        Span span{context, fid, id_slice};
        const auto sid_slice = context.symbol_slice(id_slice);
        const auto maybe_did = context.look_up_scoped_type(scope, sid_slice, span);
        if (maybe_did.empty()) {
            context.emplace_diagnostic(span, diag_code::use_of_undeclared_identifier,
                                       diag_type::error,
                                       DiagnosticSubCode{.sub_code = diag_code::not_a_contract});
            return {};
        }
        if (!context.is_contract(visit_as_transparent(maybe_did.as_id()))) {
            const auto d0
                = context.emplace_diagnostic(span, diag_code::invalid_contract, diag_type::error,
                                             DiagnosticSubCode{diag_code::not_a_contract});
            const auto d1 = context.emplace_diagnostic_with_message_value(
                context.def(maybe_did.as_id()).span, diag_code::declared_here, diag_type::note,
                DiagnosticIdentifierBeforeMessage{.sid_slice = sid_slice});
            context.link_diagnostic(d0, d1);
            return {};
        }
        contract_vec.push_back(maybe_did.as_id());
    }
    return context.freeze_id_vec(contract_vec);
}

DefId InsideBodyDefVisitor::visit_as_dependent(DefId def) {
    context.promote_mention_state_of(def, Def::mention_state::mentioned);
    return def;
}

DefId InsideBodyDefVisitor::visit_as_transparent(DefId def) {
    context.promote_mention_state_of(def, Def::mention_state::mentioned);
    return def;
}

DefId InsideBodyDefVisitor::visit_as_independent(DefId def) { return def; }

DefId InsideBodyDefVisitor::visit_as_mutator(DefId def) {
    context.promote_mention_state_of(def, Def::mention_state::mutated);
    return def;
}

} // namespace hir
