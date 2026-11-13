//     /                              /
//    /                              /
//   /_____  _____  _____  _____    /  _____   _  _  _____
//  /     / /____  /____/ /____/   /  /____/  /\  / /  __
// /_____/ /____  /    / /   \    /  /    /  /  \/ /____/
// Copyright (C) 2025-2026 Zachary Mahan
// Licensed under the Apache License 2.0. See LICENSE for details.

#ifndef BEARC_COMPILER_HIR_EXPR_SOLVER_COMMON_HPP
#define BEARC_COMPILER_HIR_EXPR_SOLVER_COMMON_HPP

#include "compiler/ast/expr.h"
#include "compiler/hir/compt_expr_solver.hpp"
#include "compiler/hir/context.hpp"
#include "compiler/hir/deduction.hpp"
#include "compiler/hir/def_visitor.hpp"
#include "compiler/hir/diagnostic.hpp"
#include "compiler/hir/exec.hpp"
#include "compiler/hir/expr_solver.hpp"
#include "compiler/hir/indexing.hpp"
#include "compiler/hir/inline_id_map.hpp"
#include "compiler/hir/scope.hpp"
#include "compiler/hir/type_resolver.hpp"
#include "llvm/ADT/SmallBitVector.h"

namespace hir {

enum class compt_or_runtime : uint8_t {
    compt,
    runtime,
};

template <class Solver>
[[nodiscard]] static inline OptId<ExecId>
solve_expr_helper(Solver& solver, FileId fid, ScopeId scope, OptId<MoveMapId> maybe_move_map,
                  const ast_expr_t* expr, OptId<TypeId> maybe_into_tid = {}) {
    if constexpr (std::same_as<Solver, ComptExprSolver>) {
        return solver.solve_expr(fid, scope, expr, maybe_into_tid);
    } else {
        return solver.solve_expr(fid, LexicalCtx{.scope = scope, .map = maybe_move_map.as_id()},
                                 expr, maybe_into_tid);
    }
}

[[nodiscard]] static inline std::optional<hir::ExecConst>
solve_expr_literal(Context& ctx, FileId fid, const ast_expr_t* expr) {
    assert(expr->type == AST_EXPR_LITERAL);
    const token_t* tkn = expr->expr.literal.tkn;
    std::optional<ExecConst> maybe_value{};
    switch (tkn->type) {
    case TOK_CHAR_LIT:
        maybe_value = ExecConst{tkn->val.character};
        break;
    // try as i32 if possible
    case TOK_INT_LIT: {
        maybe_value = ExecConst{tkn->val.signed_integral};
        auto maybe_signed = maybe_value->try_safe_convert_to(ctx, builtin_type::i32);
        if (maybe_signed.has_value()) {
            maybe_value = maybe_signed;
        }
    } break;
        // try as i32 and then i64 if possible
    case TOK_UINT_LIT: {
        maybe_value = ExecConst{tkn->val.unsigned_integral};
        auto maybe_signed = maybe_value->try_safe_convert_to(ctx, builtin_type::i32);
        if (maybe_signed.has_value()) {
            maybe_value = maybe_signed;
        } else {
            maybe_signed = maybe_value->try_safe_convert_to(ctx, builtin_type::i64);
            if (maybe_signed.has_value()) {
                maybe_value = maybe_signed;
            }
        }
        break;
    }
    case TOK_FLOAT_LIT:
        maybe_value = ExecConst{tkn->val.floating};
        break;
    case TOK_STR_LIT:
        maybe_value = ExecConst{ctx.symbol_id_for_str_lit_tkn(tkn, fid)};
        break;
    case TOK_BOOL_LIT_FALSE:
        maybe_value = ExecConst{false};
        break;
    case TOK_BOOL_LIT_TRUE:
        maybe_value = ExecConst{true};
        break;
    case TOK_NULL_LIT:
        maybe_value = ExecConst{nullptr};
        break;
    default:
        std::unreachable();
        break;
    }

    return maybe_value;
}

[[nodiscard]] inline TypeId fn_ptr_tid_for(Context& context, DefId func_did) {
    if (const auto cached = context.def(func_did).as<DefFunction>().maybe_cached_fn_ptr_tid) {
        return cached.as_id();
    }
    const DefFunction& func_def = context.def(func_did).as<DefFunction>();
    const TypeId fn_ptr_tid = context.emplace_type(
        TypeFnPtr{.param_types = func_def.param_types, .return_type = func_def.return_type},
        Span::generated(), false);
    context.def(func_did).as<DefFunction>().maybe_cached_fn_ptr_tid = fn_ptr_tid;
    return fn_ptr_tid;
}

[[nodiscard]] inline ExecId emplace_fn_ptr_exec(Context& context, DefId func_did, Span span) {
    return context.emplace_compt_exec(
        ExecFnPtr{.func_def_id = func_did, .fn_ptr_tid = fn_ptr_tid_for(context, func_did)}, span);
}

template <compt_or_runtime C>
[[nodiscard]] static inline OptId<ExecId>
handle_any_id_impl(Context& context, DefVisitor& def_visitor, FileId fid, ScopeId scope,
                   token_ptr_slice_t id_slice, OptId<GenericArgIdSliceId> maybe_gen_args) {

    // TODO handle moves

    const auto sid_slice = context.symbol_slice(id_slice);
    const Span expr_span{context, fid, id_slice};
    OptId<DefId> maybe_did{};

    if (maybe_gen_args.empty()) {
        maybe_did = context.look_up_scoped_variable(scope, sid_slice, expr_span);
        // try to look up scoped type if needed so we can find variant fields
        if (maybe_did.empty()) {
            maybe_did = context.look_up_scoped_type(scope, sid_slice, expr_span);
        }
        if (maybe_did.empty()) {

            context.emplace_diagnostic(Span{context, fid, id_slice},
                                       diag_code::use_of_undeclared_identifier, diag_type::error);
        }
    } else {
        const HirSize prior_diag_cnt = context.diagnostic_count();

        Span span = Span::combine(expr_span, context.span_for_gen_args(maybe_gen_args.as_id()));

        maybe_did = context.look_up_scoped_variable_generic(def_visitor, scope, sid_slice, span,
                                                            maybe_gen_args.as_id());

        // try to look up scoped type if needed so we can find variant fields
        if (maybe_did.empty()) {
            maybe_did = context.look_up_scoped_type_generic(def_visitor, scope, sid_slice, span,
                                                            maybe_gen_args.as_id());
        }

        const HirSize post_diag_cnt = context.diagnostic_count();

        // if no other issue and we just legit didn't find, then make sure to report, since
        // that means just straight up didn't find anything
        if (maybe_did.empty() && prior_diag_cnt == post_diag_cnt) {
            context.emplace_diagnostic(span, diag_code::use_of_undeclared_identifier,
                                       diag_type::error);
        }
    }
    if (maybe_did.empty()) {
        return {}; // some issue
    }
    const DefId did = maybe_did.as_id();
    const Def& def = context.def(def_visitor.visit_as_dependent(did));

    if (def.holds<DefVariable>()) {

        if (!def.statik && def.parent
            && context.def(def.parent.as_id()).holds_any_of<DefStruct, DefUnion>()) {
            auto d0 = context.emplace_diagnostic(
                expr_span,
                diag_code::cannot_access_non_static_member_without_accessing_thru_an_instance,
                diag_type::error);
            auto d1 = context.emplace_diagnostic_with_message_value(
                def.span, diag_code::declared_here, diag_type::note,
                DiagnosticSymbolBeforeMessage{.sid = def.name});
            context.link_diagnostic(d0, d1);
            return {};
        }

        if (C == compt_or_runtime::compt) {

            if (!def.compt) {
                auto d0 = context.emplace_diagnostic(
                    expr_span, diag_code::cannot_resolve_at_compt, diag_type::error,
                    DiagnosticSubCode{.sub_code = diag_code::is_not_a_compile_time_constant});
                auto d1 = context.emplace_diagnostic_with_message_value(
                    def.span, diag_code::declared_here_without_compt, diag_type::note,
                    DiagnosticIdentifierBeforeMessage{.sid_slice = sid_slice});
                context.link_diagnostic(d0, d1);
                return std::nullopt;
            }

            if (!def.as<DefVariable>().compt_value.has_value()) {
                return {}; // poisoned
            }
        }

        if (def.as<DefVariable>().compt_value.has_value()) {
            // right thing, we good for anything that's compt/runtime eval + compt
            // (make new exec w/ same val since we need to update the span loc!)
            auto orig_exec = context.exec(def.as<DefVariable>().compt_value.as_id());
            return context.emplace_compt_exec(orig_exec.value, expr_span);
        }
        // handle runtime
        const auto tid = def.as<DefVariable>().type_id;

        const bool mut_ref = context.type(tid).mut;

        const auto variable_exec
            = context.emplace_exec(ExecVariable{.def_id = did, .type_id = tid}, expr_span);
        // try auto-dereference
        if (context.type(tid).holds<TypeRef>()) {
            return context.emplace_exec(ExecDeref{.dereferenced = variable_exec, .mut = mut_ref},
                                        expr_span);
        }
        return variable_exec;
    }
    // we hit a def corresponding to a function, so a compt function pointer is quite
    // helpful here.
    if (def.holds<DefFunction>()) {
        return emplace_fn_ptr_exec(context, did, expr_span);
    }

    if (def.holds<DefVariantField>()) {

        const DefVariantField var_field = def.as<DefVariantField>();

        if (var_field.members.len() != 0) {
            context.emplace_diagnostic_with_message_value(
                expr_span, diag_code::only_message_value_is_meaningful, diag_type::error,
                DiagnosticVariantInitExpectedButGotNumArgs{
                    .variant_field_name = def.name,
                    .expected_sid = context.symbol_id(std::to_string(var_field.members.len())),
                    .got_sid = context.symbol_id(std::to_string(0))});
            return {};
        }
        ExecId payload = context.emplace_compt_exec(
            ExecVariantFieldInit{.member_inits = {}, .variant_field_def_id = did}, expr_span);
        return context.emplace_compt_exec(ExecVariantInit{.payload_init = payload,
                                                          .variant_def_id = def.parent.as_id(),
                                                          .active_member_idx = def.member_idx},
                                          expr_span);
    }
    if constexpr (C == compt_or_runtime::compt) {
        auto d0 = context.emplace_diagnostic(
            expr_span, diag_code::is_not_a_variable, diag_type::error,
            DiagnosticSubCode{.sub_code = diag_code::is_not_a_compile_time_constant});
        auto d1 = context.emplace_diagnostic_with_message_value(
            def.span, diag_code::declared_here, diag_type::note,
            DiagnosticIdentifierBeforeMessage{.sid_slice = sid_slice});
        context.link_diagnostic(d0, d1);
    } else {
        auto d0 = context.emplace_diagnostic_with_message_value(
            expr_span, diag_code::is_not_a_variable, diag_type::error,
            DiagnosticSymbolBeforeMessage{.sid = def.name});
        auto d1 = context.emplace_diagnostic_with_message_value(
            def.span, diag_code::declared_here, diag_type::note,
            DiagnosticIdentifierBeforeMessage{.sid_slice = sid_slice});
        context.link_diagnostic(d0, d1);
    }
    return {};
}

// a non-empty MoveMapId has to be passed in for a RuntimeSolver solver
template <IsExprSolver Solver>
[[nodiscard]] static inline OptId<ExecId>
handle_struct_or_union_init_impl(Solver& solver, FileId fid, ScopeId scope,
                                 OptId<MoveMapId> maybe_move_map, const ast_expr_t* expr,
                                 OptId<TypeId> into_tid) {
    Context& context = solver.get_context();
    DefVisitor& def_visitor = solver.get_def_visitor();
    Span expr_span{context, fid, expr};
    auto id_slice = expr->expr.struct_init.id;

    OptId<DefId> maybe_struct_did{};
    OptId<ExecId> maybe_eid{};

    if (id_slice.len == 0) {
        if (into_tid.empty() || context.type(into_tid.as_id()).holds<TypeVar>()) {
            auto d0 = context.emplace_diagnostic(
                expr_span, diag_code::cannot_infer_type_for_initializer, diag_type::error);
            auto d1 = context.emplace_diagnostic(
                expr_span,
                diag_code::explicitly_specify_the_type_by_providing_its_name_before_the_braces,
                diag_type::help);
            context.link_diagnostic(d0, d1);
            return {};
        }
        maybe_struct_did = context.try_def_for_type(into_tid.as_id());
    } else {
        auto sid_slice = context.symbol_slice(expr->expr.struct_init.id);
        Span id_span{context, fid, id_slice};
        OptId<DefId> maybe_did{};
        if (!expr->expr.struct_init.is_generic) {
            maybe_did = context.look_up_scoped_type(scope, sid_slice, id_span);
        } else {
            const auto maybe_generic_args
                = solver.lower_generic_args(fid, scope, expr->expr.struct_init.generic_args, false);
            if (maybe_generic_args.empty()) {
                return {}; // poisoned
            }
            maybe_did = context.look_up_scoped_type_generic(def_visitor, scope, sid_slice, id_span,
                                                            maybe_generic_args.as_id());
        }

        if (maybe_did.empty()) {
            context.emplace_diagnostic(
                id_span, diag_code::use_of_undeclared_identifier, diag_type::error,
                DiagnosticIdentifierAfterMessage{.sid_slice = sid_slice},
                DiagnosticSubCode{.sub_code = diag_code::not_declared_in_this_scope});
            return std::nullopt;
        }

        const auto did = def_visitor.visit_as_dependent(maybe_did.as_id());

        // try as union
        auto maybe_union_def = context.ensure_union_def(did);
        if (maybe_union_def.has_value()) {
            return solve_union_init(solver, fid, scope, maybe_move_map, maybe_union_def.as_id(),
                                    expr);
        }

        maybe_struct_did = context.ensure_struct_def(did);

        if (maybe_struct_did.empty()) {
            const Def& def = context.def(did);
            // check for raw use of generic struct
            if (def.generic) {
                auto d0 = context.emplace_diagnostic_with_message_value(
                    Span{context, fid, id_slice}, diag_code::raw_use_of_generic_type,
                    diag_type::error, DiagnosticIdentifierBeforeMessage{.sid_slice = sid_slice});
                auto d1 = context.emplace_diagnostic(def.span, diag_code::declared_here_as_generic,
                                                     diag_type::note);
                context.link_diagnostic(d0, d1);
                return {};
            }
            auto d0 = context.emplace_diagnostic_with_message_value(
                Span{context, fid, id_slice}, diag_code::is_not_a_struct, diag_type::error,
                DiagnosticIdentifierBeforeMessage{.sid_slice = sid_slice});
            auto d1
                = context.emplace_diagnostic(def.span, diag_code::declared_here, diag_type::note);
            context.link_diagnostic(d0, d1);
            return {};
        }
    }

    if (maybe_struct_did.empty()
        || !context.def(maybe_struct_did.as_id()).template holds<DefStruct>()) {
        return {};
    }
    DefId struct_did = maybe_struct_did.as_id();
    maybe_eid = solve_struct_init(solver, fid, scope, maybe_move_map, struct_did, expr, into_tid);
    return maybe_eid;
}

template <IsExprSolver Solver>
[[nodiscard]] static inline OptId<ExecId>
solve_union_init(Solver& solver, FileId fid, ScopeId scope, OptId<MoveMapId> maybe_move_map,
                 DefId union_did, const ast_expr_t* expr) {

    static constexpr bool is_compt_eval = std::is_same_v<Solver, ComptExprSolver>;
    Context& context = solver.get_context();

    assert(context.def(union_did).template holds<DefUnion>());

    const auto member_dids = context.def(union_did).template as<DefUnion>().ordered_members;
    auto sid_slice = context.symbol_slice(expr->expr.struct_init.id);
    Span id_span{context, fid, expr->expr.struct_init.id};
    const ast_slice_of_exprs_t init_slice = expr->expr.struct_init.member_inits;

    if (init_slice.len > 1) {
        Span mem_span{context, fid, init_slice.start[0]->first,
                      init_slice.start[init_slice.len - 1]->last};
        auto d0 = context.emplace_diagnostic_with_message_value(
            Span{context, fid, expr}, diag_code::too_many_initializers_for_union, diag_type::error,
            DiagnosticIdentifierAfterMessage{.sid_slice = sid_slice});
        auto d1 = context.emplace_diagnostic(
            mem_span, diag_code::union_initializers_must_only_set_one_field, diag_type::note);
        context.link_diagnostic(d0, d1);
        return {};
    }
    if (init_slice.len < 1) {
        auto d0 = context.emplace_diagnostic_with_message_value(
            Span{context, fid, expr}, diag_code::too_few_inits_for_union, diag_type::error,
            DiagnosticIdentifierAfterMessage{.sid_slice = sid_slice});
        auto d1 = context.emplace_diagnostic(Span{context, fid, expr},
                                             diag_code::union_initializers_must_only_set_one_field,
                                             diag_type::note);
        context.link_diagnostic(d0, d1);
        return {};
    }

    const ast_expr_t* member_init = expr->expr.struct_init.member_inits.start[0];
    if (member_init->type != AST_EXPR_STRUCT_MEMBER_INIT) {
        return {};
    }
    SymbolId member_name = context.symbol_id(member_init->expr.struct_member_init.id);
    OptId<DefId> maybe_match = context.linear_name_match_in_def_slice(member_dids, member_name);
    if (maybe_match.empty()) {
        auto d0 = context.emplace_diagnostic(
            Span{context, fid, member_init->expr.struct_member_init.id},
            diag_code::does_not_name_a_field_of_union, diag_type::error,
            DiagnosticIdentifierAfterMessage{.sid_slice = sid_slice},
            DiagnosticSubCode{.sub_code = diag_code::use_of_undeclared_identifier});
        auto d1 = context.emplace_diagnostic_with_message_value(
            context.def(union_did).span, diag_code::declared_here, diag_type::note,
            DiagnosticSymbolAfterMessage{.sid = context.def(union_did).name});
        context.link_diagnostic(d0, d1);
        return {};
    }
    DefId matched_did = maybe_match.as_id();
    if (!context.def(solver.get_def_visitor().visit_as_transparent(matched_did))
             .template holds<DefVariable>()) {
        return {}; // poisoned
    }
    TypeId needed_tid = context.def(matched_did).template as<DefVariable>().type_id;
    OptId<ExecId> maybe_val = solve_expr_helper(
        solver, fid, scope, maybe_move_map, member_init->expr.struct_member_init.value, needed_tid);
    if (maybe_val.empty()) {
        return {}; // poisoned
    }
    return context.emplace_exec(
        ExecUnionInit{
            .member_init = maybe_val.as_id(),
            .union_def_id = union_did,
            .active_member_idx = context.def(matched_did).member_idx,
        },
        Span{context, fid, expr},
        /*should_be_compt=*/is_compt_eval || context.exec(maybe_val.as_id()).compt);
}

template <IsExprSolver Solver>
[[nodiscard]] OptId<ExecId> solve_struct_init(Solver& solver, FileId fid, ScopeId scope,
                                              OptId<MoveMapId> maybe_move_map, DefId struct_did,
                                              const ast_expr_t* expr, OptId<TypeId> into_tid) {

    static constexpr bool is_compt_eval = std::same_as<Solver, ComptExprSolver>;

    Context& context = solver.get_context();
    const auto member_dids = context.ordered_defs_for(struct_did);
    const ast_slice_of_exprs_t init_slice = expr->expr.struct_init.member_inits;

    enum class relative_arity : uint8_t { too_few, same, too_many };

    relative_arity rel_arity = relative_arity::same;
    if (init_slice.len < member_dids.len()) {
        rel_arity = relative_arity::too_few;
    } else if (init_slice.len > member_dids.len()) {
        rel_arity = relative_arity::too_many;
    }

    DiagLinker dl{context};
    InlineIdMap<DefId, ExecId> mem_defs_to_execs{};
    // by member_idx; members whose value failed to solve (already diagnosed, so don't cascade)
    llvm::SmallBitVector poisoned_members(member_dids.len());
    bool cooked = false;
    bool all_compt = true;
    for (auto i = 0uz; i < init_slice.len; i++) {
        ast_expr_t* member_init_expr = init_slice.start[i];
        if (member_init_expr->type != AST_EXPR_STRUCT_MEMBER_INIT) {
            return {};
        }
        ast_expr_struct_member_init mem_init = member_init_expr->expr.struct_member_init;
        const SymbolId mem_sid = context.symbol_id(mem_init.id);
        Span id_span{context, fid, mem_init.id};
        const OptId<DefId> maybe_mem_did = context.look_up_member_var_guarding_hid(
            context.def(struct_did), mem_sid, id_span, scope, dl);
        if (maybe_mem_did.empty()) {
            continue;
        }
        if (context.def(maybe_mem_did.as_id()).statik) {
            dl.link(context.emplace_diagnostic_with_message_value(
                id_span, diag_code::id_does_not_name_a_member_variable_of, diag_type::error,
                DiagnosticSymbolAfterMessage{.sid = context.def(struct_did).name}));
            dl.link(context.emplace_diagnostic_with_message_value(
                context.def(maybe_mem_did.as_id()).span, diag_code::declared_here, diag_type::note,
                DiagnosticSymbolBeforeMessage{.sid = mem_sid}));
            continue;
        }
        TypeId mem_tid = context.def(maybe_mem_did.as_id()).as<DefVariable>().type_id;
        OptId<ExecId> maybe_eid
            = solve_expr_helper(solver, fid, scope, maybe_move_map, mem_init.value, mem_tid);
        if (!maybe_eid) {
            poisoned_members.set(context.def(maybe_mem_did.as_id()).member_idx);
            continue;
        }
        const auto maybe_existing = mem_defs_to_execs.at(maybe_mem_did.as_id());
        if (maybe_existing) {
            dl.link(context.emplace_diagnostic_with_message_value(
                id_span, diag_code::reinitialized_struct_member, diag_type::error,
                DiagnosticSymbolAfterMessage{.sid = mem_sid}));
            dl.link(context.emplace_diagnostic(context.exec(maybe_existing.as_id()).span,
                                               diag_code::initialized_here, diag_type::note));
            continue;
        }
        all_compt &= context.exec(maybe_eid.as_id()).compt;
        mem_defs_to_execs.insert(maybe_mem_did.as_id(), maybe_eid.as_id());
    }

    llvm::SmallVector<ExecId> member_init_execs{};
    for (const auto didx : member_dids) {
        const auto did = context.def_id(didx);
        if (!context.def(did).template holds<DefVariable>()) {
            return std::nullopt;
        }
        const auto maybe_eid = mem_defs_to_execs.at(did);
        const auto maybe_default_eid = context.def(did).as<DefVariable>().compt_value;
        if (!maybe_eid && !maybe_default_eid
            && !poisoned_members.test(context.def(did).member_idx)) {
            cooked = true;
            dl.link(context.emplace_diagnostic_with_message_value(
                Span{context, fid, expr}, diag_code::struct_initializer_does_not_initialize_field,
                diag_type::error, DiagnosticSymbolAfterMessage{.sid = context.def(did).name}));
            dl.link(context.emplace_diagnostic_with_message_value(
                Span{context, fid, expr},
                diag_code::does_not_have_a_default_value_so_an_initial_value_is_needed,
                diag_type::note, DiagnosticSymbolBeforeMessage{.sid = context.def(did).name}));
            dl.link(context.emplace_diagnostic(context.def(did).span, diag_code::declared_here,
                                               diag_type::note));
            continue;
        }
        if (maybe_eid) {
            member_init_execs.push_back(maybe_eid.as_id());
        } else if (maybe_default_eid) {
            member_init_execs.push_back(maybe_default_eid.as_id());
        }
    }
    if (rel_arity == relative_arity::too_many) {
        cooked = true;
        dl.link(context.emplace_diagnostic(Span{context, fid, init_slice},
                                           diag_code::too_many_initializers_given_for_struct_init,
                                           diag_type::error));
    }
    if (cooked) {
        dl.link(context.emplace_diagnostic(
            context.def(struct_did).span, diag_code::declared_here, diag_type::note,
            DiagnosticSymbolBeforeMessage{context.def(struct_did).name}, DiagnosticNoOtherInfo{}));
        return std::nullopt;
    }
    if (poisoned_members.any()) {
        return {};
    }
    // type check before returning here! but only if the into type has a value
    if (into_tid.has_value() && context.type(into_tid.as_id()).template holds<TypeStruct>()) {
        if (auto into_did = context.type(into_tid.as_id()).template as<TypeStruct>().def_id;
            struct_did != into_did) {
            context.emplace_diagnostic(Span{context, fid, expr},
                                       diag_code::cannot_convert_value_of_type, diag_type::error,
                                       DiagnosticTypeToType{.from = context.emplace_type(
                                                                TypeStruct{
                                                                    .def_id = struct_did,
                                                                    .gen_args_slice = {},
                                                                },
                                                                Span{context, fid, expr}, false),
                                                            .to = into_tid.as_id()},
                                       DiagnosticNoOtherInfo{});
            return {};
        }
    }

    // all good, set the exec
    return context.emplace_exec(
        ExecStructInit{.member_inits = context.freeze_id_vec(member_init_execs),
                       .struct_def_id = struct_did},
        Span{context, fid, expr}, /*should_be_compt=*/is_compt_eval || all_compt);
}

template <IsExprSolver Solver>
[[nodiscard]] OptId<ExecId>
solve_list_literal_impl(Solver& solver, FileId fid, ScopeId scope, OptId<MoveMapId> maybe_move_map,
                        const ast_expr_t* list_expr, OptId<TypeId> maybe_into_type) {
    assert(list_expr->type == AST_EXPR_LIST_LITERAL);

    static constexpr bool is_compt_eval = std::same_as<Solver, ComptExprSolver>;

    Context& context = solver.get_context();

    Span whole_list_span{fid, context.ast(fid).buffer(), list_expr->first, list_expr->last};

    ast_expr_list_literal_t list = list_expr->expr.list_literal;

    ast_slice_of_exprs_t list_slice = list.slice;

    // to properly understand desired type (if there is one)
    OptId<TypeId> maybe_elem_into_type{};

    // if into type is builtin, try to cast
    if (maybe_into_type.has_value()) {
        TypeId into_type = maybe_into_type.as_id();
        const Type& type = context.type(into_type);
        if (type.holds<TypeArr>()) {
            auto inner_tid = type.as<TypeArr>().inner;

            maybe_elem_into_type = inner_tid;
        }
    }

    // guard empty
    if (list_slice.len == 0) {
        return context.emplace_exec(
            ExecListLiteral{.elems = IdSlice<ExecId>{}, .elem_type_id = maybe_elem_into_type},
            whole_list_span, is_compt_eval);
    }

    llvm::SmallVector<ExecId> elem_execs{};

    for (HirSize i = 0; i < list_slice.len; ++i) {
        const ast_expr_t* expr = list_slice.start[i];
        OptId<ExecId> maybe_exec
            = solve_expr_helper(solver, fid, scope, maybe_move_map, expr, maybe_elem_into_type);
        if (maybe_exec.empty()) {
            return std::nullopt; // poisoned
        }
        elem_execs.push_back(maybe_exec.as_id());
    }

    IdSlice<ExecId> elem_slice = context.freeze_id_vec(elem_execs);

    OptId<TypeId> maybe_first_type
        = context.infer_type_from_exec(context.exec_id(elem_slice.begin()));

    if (maybe_first_type.empty()) {
        return {}; // poisoned
    }

    // We will now type check this list literal for type-homogeneousness:

    const Exec& first_exec = context.exec(elem_slice.begin());
    TypeId type_for_list = maybe_first_type.as_id();

    bool homo_type = true;
    bool all_compt = true;

    DiagLinker dl{context};

    for (IdIdx<ExecId> eidx = elem_slice.begin(); eidx != elem_slice.end(); eidx++) {
        ExecId eid = context.exec_id(eidx);
        OptId<TypeId> maybe_curr_type = context.infer_type_from_exec(eid);

        if (maybe_curr_type.empty()) {
            return {}; // poisoned
        }

        all_compt &= context.exec(eid).compt;

        TypeId curr_type = maybe_curr_type.as_id();

        if (!context.equivalent_type(curr_type, type_for_list)) {

            const Exec& curr_exec = context.exec(eid);

            // check if it's own first hetero-rodeo, and, if so, emplace the once diagnostic
            // for the whole list before emplacing the per-exec diagnostics
            if (homo_type) {
                dl.link(context.emplace_diagnostic(whole_list_span,
                                                   diag_code::mismatched_types_in_list_literal,
                                                   diag_type::error));
                // say type of first diagnostic this one time
                dl.link(context.emplace_diagnostic_with_message_value(
                    first_exec.span, diag_code::value_is_of_type, diag_type::note,
                    DiagnosticTypeAfterMessage{.tid = type_for_list}));
            }

            homo_type = false;

            dl.link(context.emplace_diagnostic_with_message_value(
                curr_exec.span, diag_code::value_is_of_type, diag_type::note,
                DiagnosticTypeAfterMessage{.tid = curr_type}));
        }
    }

    // list isn't homogeneous, so it's invalid
    if (!homo_type) {
        return {};
    }

    // fine, homogeneous, so return
    return context.emplace_exec(ExecListLiteral{.elems = elem_slice, .elem_type_id = type_for_list},
                                whole_list_span, /*should_be_compt=*/is_compt_eval || all_compt);
}

template <class Solver>
[[nodiscard]] OptId<ExecId> solve_tuple_init_impl(Solver& solver, FileId fid, ScopeId scope,
                                                  OptId<MoveMapId> maybe_move_map,
                                                  const ast_expr_t* expr) {
    assert(expr->type == AST_EXPR_TUPLE_INIT);

    const auto exprs = expr->expr.tuple.exprs;

    llvm::SmallVector<ExecId> eid_vec{};

    Context& context = solver.get_context();

    bool all_compt = true;

    for (auto i = 0uz; i < exprs.len; ++i) {
        const auto maybe_eid
            = solve_expr_helper(solver, fid, scope, maybe_move_map, exprs.start[i]);
        if (maybe_eid.empty()) {
            return {};
        }

        all_compt &= context.exec(maybe_eid.as_id()).compt;

        eid_vec.push_back(maybe_eid.as_id());
    }

    llvm::SmallVector<TypeId> tid_vec{};

    for (const auto eid : eid_vec) {
        const auto maybe_tid = context.infer_type_from_exec(eid);
        if (maybe_tid.empty()) {
            return {};
        }
        tid_vec.push_back(maybe_tid.as_id());
    }

    const auto maybe_anon_tid
        = TypeResolver{context, solver.get_def_visitor()}.make_anon_struct_type(tid_vec);

    if (maybe_anon_tid.empty()) {
        return {};
    }

    const auto& ty = context.type(maybe_anon_tid.as_id());

    assert(ty.holds<TypeStruct>());

    static constexpr bool should_be_compt = std::same_as<Solver, ComptExprSolver>;

    return context.emplace_exec(ExecStructInit{.member_inits = context.freeze_id_vec(eid_vec),
                                               .struct_def_id = ty.as<TypeStruct>().def_id,
                                               .anonymous = true},
                                Span{context, fid, expr}, should_be_compt || all_compt);
}

struct FuncLookUp {
    OptId<DefId> func_did{};
    OptId<ExecId> maybe_variant_init_res{};
    OptId<ExecId> maybe_indirect_callee{};
    bool needs_generic_deduction{false};
    bool did_variant_initializer{false};
};

[[nodiscard]] inline OptId<DefId> struct_did_for_self_val(Context& context, ExecId self_eid) {
    const Exec& exec = context.exec(self_eid);
    if (exec.holds<ExecStructInit>()) {
        return exec.as<ExecStructInit>().struct_def_id;
    }
    const auto maybe_tid = context.infer_type_from_exec(self_eid);
    if (maybe_tid.has_value() && context.type(maybe_tid.as_id()).holds<TypeStruct>()) {
        return context.type(maybe_tid.as_id()).as<TypeStruct>().def_id;
    }
    return {};
}

template <IsExprSolver Solver>
[[nodiscard]] OptId<ExecId>
solve_variant_init_impl(Solver& solver, FileId fid, ScopeId scope, OptId<MoveMapId> maybe_move_map,
                        const ast_expr_t* fn_call_expr, DefId variant_field_did) {
    assert(fn_call_expr->type == AST_EXPR_FN_CALL);

    static constexpr bool is_compt_eval = std::same_as<Solver, ComptExprSolver>;
    Context& context = solver.get_context();

    const ast_expr_t* called_expr = fn_call_expr->expr.fn_call.left_expr;
    const Def& def = context.def(variant_field_did);
    if (!def.holds<DefVariantField>()) {
        context.emplace_diagnostic(Span{context, fid, called_expr},
                                   diag_code::value_is_not_callable, diag_type::error,
                                   DiagnosticSubCode{.sub_code = diag_code::not_a_function});
        return {};
    }
    const DefVariantField var_field_def = def.as<DefVariantField>();
    const ast_slice_of_exprs_t args = fn_call_expr->expr.fn_call.args;
    if (args.len != var_field_def.members.len()) {
        const auto d0 = context.emplace_diagnostic_with_message_value(
            Span{context, fid, fn_call_expr}, diag_code::only_message_value_is_meaningful,
            diag_type::error,
            DiagnosticVariantInitExpectedButGotNumArgs{
                .variant_field_name = def.name,
                .expected_sid = context.symbol_id(std::to_string(var_field_def.members.len())),
                .got_sid = context.symbol_id(std::to_string(args.len))});
        const auto d1 = context.emplace_diagnostic_with_message_value(
            context.def(variant_field_did).span, diag_code::declared_here, diag_type::note,
            DiagnosticSymbolBeforeMessage{.sid = context.def(variant_field_did).name});
        context.link_diagnostic(d0, d1);
        return {};
    }
    bool cooked = false;
    bool all_compt = true;
    llvm::SmallVector<ExecId> member_init_vec;
    for (size_t i = 0; i < args.len; i++) {
        const ast_expr_t* arg = args.start[i];
        const TypeId tid = context.def(var_field_def.members.get(i)).as<DefVariable>().type_id;
        const OptId<ExecId> maybe_eid
            = solve_expr_helper(solver, fid, scope, maybe_move_map, arg, tid);
        if (maybe_eid.empty()) {
            cooked = true;
            continue; // poisoned
        }
        all_compt &= context.exec(maybe_eid.as_id()).compt;
        member_init_vec.push_back(maybe_eid.as_id());
    }
    if (cooked) {
        const Def& field_def = context.def(variant_field_did);
        context.force_link_diagnostic(context.emplace_diagnostic_with_message_value(
            field_def.span, diag_code::declared_here, diag_type::note,
            DiagnosticSymbolBeforeMessage{.sid = field_def.name}));
        return {};
    }
    const bool should_be_compt = is_compt_eval || all_compt;
    const auto member_inits = context.freeze_id_vec(member_init_vec);
    const Span span{context, fid, fn_call_expr};
    const ExecId field_init
        = context.emplace_exec(ExecVariantFieldInit{.member_inits = member_inits,
                                                    .variant_field_def_id = variant_field_did},
                               span, should_be_compt);

    return context.emplace_exec(
        ExecVariantInit{.payload_init = field_init,
                        .variant_def_id = context.def(variant_field_did).parent.as_id(),
                        .active_member_idx = context.def(variant_field_did).member_idx},
        span, should_be_compt);
}

/// tries to look up a function from an expression, and optionally a "self" value, if it exists
///
/// self values are just the the left side of this: self.do_something()
template <IsExprSolver Solver>
[[nodiscard]] std::optional<FuncLookUp>
try_indirect_fn_look_up(Solver& solver, FileId fid, ScopeId scope, OptId<MoveMapId> maybe_move_map,
                        const ast_expr_t* called, OptId<ExecId> maybe_callee_eid = std::nullopt) {
    Context& context = solver.get_context();
    if (maybe_callee_eid.empty()) {
        maybe_callee_eid = solve_expr_helper(solver, fid, scope, maybe_move_map, called);
        if (maybe_callee_eid.empty()) {
            return {}; // poisoned
        }
    }
    const auto maybe_tid = context.infer_type_from_exec(maybe_callee_eid.as_id());
    if (maybe_tid.empty()) {
        return {}; // poisoned
    }
    if (!context.type(maybe_tid.as_id()).holds<TypeFnPtr>()) {
        context.emplace_diagnostic(Span{context, fid, called}, diag_code::value_is_not_callable,
                                   diag_type::error,
                                   DiagnosticSubCode{.sub_code = diag_code::not_a_function});
        return {};
    }
    return FuncLookUp{.maybe_indirect_callee = maybe_callee_eid};
}

template <IsExprSolver Solver>
[[nodiscard]] std::optional<FuncLookUp>
try_fn_look_up_from_expr_impl(Solver& solver, FileId fid, ScopeId scope,
                              OptId<MoveMapId> maybe_move_map, const ast_expr_t* expr,
                              OptId<ExecId> maybe_self_val) {
    static constexpr bool is_compt_eval = std::same_as<Solver, ComptExprSolver>;
    Context& context = solver.get_context();
    DefVisitor& def_visitor = solver.get_def_visitor();

    OptId<DefId> maybe_func_did{};
    bool needs_generic_deduction{false};
    OptId<ExecId> maybe_res{};

    if (maybe_self_val.has_value()) {
        const ast_expr_t* called = expr->expr.fn_call.left_expr;
        if (called->type == AST_EXPR_ID) {

            token_ptr_slice_t id_slice = called->expr.id.slice;
            if (id_slice.len > 1) {
                context.emplace_diagnostic(Span{context, fid, called},
                                           diag_code::scoped_identifer_not_allowed_here,
                                           diag_type::error);
            }
            const token_t* id_tok = id_slice.start[0];
            const SymbolId func_name = context.symbol_id(id_tok);
            const Span fn_name_span{context, fid, id_tok};

            // structs have special look up rules cuz we need to look into their member
            // functions
            if (const auto maybe_struct_did
                = struct_did_for_self_val(context, maybe_self_val.as_id());
                maybe_struct_did.has_value()) {
                const DefId struct_did = maybe_struct_did.as_id();
                if (expr->expr.fn_call.is_generic) {
                    if (const auto maybe_gen_arg_id = solver.lower_generic_args(
                            fid, scope, expr->expr.fn_call.generic_args, false);
                        maybe_gen_arg_id.has_value()) {
                        maybe_func_did = context.look_up_generic_member_function_guarding_hid(
                            def_visitor, context.def(struct_did), func_name,
                            Span{context, fid, expr->expr.fn_call.left_expr}, scope,
                            maybe_gen_arg_id.as_id());
                    }
                } else {
                    maybe_func_did = context.look_up_member_function_guarding_hid(
                        def_visitor, context.def(struct_did), func_name, fn_name_span, scope);
                }
            } else {
                if (expr->expr.fn_call.is_generic) {
                    if (const auto maybe_gen_arg_id = solver.lower_generic_args(
                            fid, scope, expr->expr.fn_call.generic_args, false);
                        maybe_gen_arg_id.has_value()) {
                        maybe_func_did = context.look_up_scoped_variable_generic(
                            def_visitor, scope,
                            context.freeze_id_vec(llvm::SmallVector<SymbolId, 1>{func_name}),
                            Span{context, fid, expr->expr.fn_call.left_expr},
                            maybe_gen_arg_id.as_id());
                    }
                    if (maybe_func_did.has_value()) {
                        maybe_func_did = context.try_func_did(maybe_func_did.as_id());
                    }
                } else {
                    maybe_func_did = context.look_up_variable(scope, func_name);
                    if (maybe_func_did.has_value()) {
                        maybe_func_did = context.try_func_did(maybe_func_did.as_id());
                    }
                }
            }

            if (maybe_func_did.empty()) {
                context.emplace_diagnostic(
                    Span{context, fid, expr->expr.fn_call.left_expr},
                    diag_code::use_of_undeclared_identifier, diag_type::error,
                    DiagnosticSubCode{.sub_code = diag_code::not_declared_in_this_scope});
                return std::nullopt;
            }
        } else {
            const auto maybe_callable_eid
                = solve_expr_helper(solver, fid, scope, maybe_move_map, called);
            if (maybe_callable_eid.empty()) {
                return {}; // posioned
            }
            const Exec& e = context.exec(maybe_callable_eid.as_id());
            if (!e.holds<ExecFnPtr>()) {
                context.emplace_diagnostic(
                    e.span, diag_code::value_is_not_callable, diag_type::error,
                    DiagnosticSubCode{.sub_code = diag_code::not_a_function});
                return {};
            }
            maybe_func_did = e.as<ExecFnPtr>().func_def_id;
        }

        maybe_func_did
            = context.try_func_did(def_visitor.visit_and_resolve_if_needed(maybe_func_did.as_id()));
        const Def& func_def = context.def(maybe_func_did.as_id());

        if (!func_def.holds<DefFunction>()) {
            if (func_def.holds<DefGenericFunction>()) {
                needs_generic_deduction = true;
            } else {
                context.emplace_diagnostic(
                    Span{context, fid, called}, diag_code::value_is_not_callable, diag_type::error,
                    DiagnosticSubCode{.sub_code = diag_code::not_a_function});
                return {};
            }
        }
        if (func_def.holds<DefFunction>() && func_def.as<DefFunction>().poisoned()) {
            return {};
        }

    } else {
        const ast_expr_t* called = expr->expr.fn_call.left_expr;
        if (called->type == AST_EXPR_ID) {
            token_ptr_slice_t id_slice = called->expr.id.slice;

            const Span called_span{context, fid, id_slice};

            const auto sid_slice = context.symbol_slice(id_slice);

            const auto prior_diag_cnt = context.diagnostic_count();

            if (expr->expr.fn_call.is_generic) {
                if (const auto maybe_gen_arg_id
                    = solver.lower_generic_args(fid, scope, expr->expr.fn_call.generic_args, false);
                    maybe_gen_arg_id.has_value()) {
                    maybe_func_did = context.look_up_scoped_variable_generic(
                        def_visitor, scope, sid_slice, called_span, maybe_gen_arg_id.as_id());
                }
            } else {
                maybe_func_did = context.look_up_scoped_variable(scope, sid_slice, called_span);
            }

            if (maybe_func_did.empty()) {

                // try as variant init, since they take the form of function calls but live in
                // the "type" namespace
                if (expr->expr.fn_call.is_generic) {
                    if (const auto maybe_gen_arg_id = solver.lower_generic_args(
                            fid, scope, expr->expr.fn_call.generic_args, false);
                        maybe_gen_arg_id.has_value()) {
                        maybe_func_did = context.look_up_scoped_type_generic(
                            def_visitor, scope, sid_slice, called_span, maybe_gen_arg_id.as_id());
                    }
                } else {
                    maybe_func_did = context.look_up_scoped_type(scope, sid_slice, called_span);
                }

                if (maybe_func_did.has_value()) {
                    maybe_res = solve_variant_init_impl(solver, fid, scope, maybe_move_map, expr,
                                                        maybe_func_did.as_id());
                    return FuncLookUp{.func_did = maybe_func_did.as_id(),
                                      .maybe_variant_init_res = maybe_res,
                                      .needs_generic_deduction = needs_generic_deduction,
                                      .did_variant_initializer = true};
                }

                // no corresponding variant either, so this is just the
                // use_of_undeclared_identifier
                if (prior_diag_cnt == context.diagnostic_count()) {
                    context.emplace_diagnostic(called_span, diag_code::use_of_undeclared_identifier,
                                               diag_type::error);
                }

                return std::nullopt;
            }
        } else {
            const auto maybe_callable_eid
                = solve_expr_helper(solver, fid, scope, maybe_move_map, called);
            if (maybe_callable_eid.empty()) {
                return {}; // posioned
            }
            const Exec& e = context.exec(maybe_callable_eid.as_id());
            if (!e.holds<ExecFnPtr>()) {
                if constexpr (!is_compt_eval) {
                    return try_indirect_fn_look_up(solver, fid, scope, maybe_move_map, called,
                                                   maybe_callable_eid);
                }
                context.emplace_diagnostic(
                    e.span, diag_code::value_is_not_callable, diag_type::error,
                    DiagnosticSubCode{.sub_code = diag_code::not_a_function});
                return {};
            }
            maybe_func_did = e.as<ExecFnPtr>().func_def_id;
        }

        maybe_func_did
            = context.try_func_did(def_visitor.visit_and_resolve_if_needed(maybe_func_did.as_id()));

        const Def& called_def = context.def(def_visitor.visit_as_dependent(maybe_func_did.as_id()));
        if (!called_def.holds<DefFunction>()) {
            if (called_def.holds<DefGenericFunction>()) {
                needs_generic_deduction = true;
            } else if (!is_compt_eval && called_def.holds<DefVariable>()) {
                return try_indirect_fn_look_up(solver, fid, scope, maybe_move_map, called);
            } else {
                context.emplace_diagnostic(
                    Span{context, fid, called}, diag_code::value_is_not_callable, diag_type::error,
                    DiagnosticSubCode{.sub_code = diag_code::not_a_function});
                return {};
            }
        }
    }
    if (maybe_func_did.has_value()) {
        return FuncLookUp{.func_did = maybe_func_did.as_id(),
                          .maybe_variant_init_res = maybe_res,
                          .needs_generic_deduction = needs_generic_deduction,
                          .did_variant_initializer
                          = false}; // we early return in that case (see above)
    }
    return {};
}

/// mutates out_args and func_did as needed
///
/// - func_did is mutated only if we use the args to deduce generic arguments and thus update
/// the func_did from a raw DefGenericFunction to a concrete, instantiated DefFunction
///
/// returns true on success, else false
template <IsExprSolver Solver>
[[nodiscard]] bool try_fn_args_from_expr_and_preliminary_function_def_id_impl(
    Solver& solver, FileId fid, ScopeId scope, OptId<MoveMapId> maybe_move_map,
    const ast_expr_t* expr, OptId<ExecId> maybe_self_val, DefId& func_did,
    const bool needs_generic_deduction, llvm::SmallVectorImpl<ExecId>& out_args) {
    Context& context = solver.get_context();

    if (maybe_self_val.has_value()) {
        out_args.push_back(maybe_self_val.as_id());
    }

    const auto prior_diag_cnt = context.diagnostic_count();

    HirSize total_arg_cnt = 0;
    bool issue = false;
    const ast_slice_of_exprs_t exprs = expr->expr.fn_call.args;
    IdSlice<DefId> params{};

    const auto check_too_many_args =
        [&context](HirSize arg_cnt, HirSize param_cnt, DefId func_did, OptId<ExecId> maybe_self_val,
                   FileId fid, const ast_expr_t* expr, ast_slice_of_exprs_t exprs) {
            auto adjusted_param_len = param_cnt - (maybe_self_val.has_value() ? 1 : 0);

            if (arg_cnt != adjusted_param_len) {

                if (param_cnt != 0) {
                    auto params_len_sym_id = ExecConst{adjusted_param_len}.to_symbol_id(context);
                    auto args_len_sym_id = ExecConst{arg_cnt}.to_symbol_id(context);

                    Span span_of_interest
                        = ((arg_cnt < adjusted_param_len) || exprs.len == 0)
                              ? Span{context, fid, expr->last}
                              : Span::combine(Span{context, fid, exprs.start[0]->first},
                                              Span{context, fid, exprs.start[exprs.len - 1]->last});

                    context.emplace_diagnostic_with_message_value(
                        span_of_interest, diag_code::expected, diag_type::error,
                        DiagnosticSymButGotSym{.leading = context.def(func_did).name,
                                               .sid1 = params_len_sym_id,
                                               .sid2 = args_len_sym_id});
                } else if (maybe_self_val.has_value()) {
                    context.emplace_diagnostic(
                        Span{context, fid, expr},
                        diag_code::free_function_taking_zero_arguments_called_as_method,
                        diag_type::error);
                } else {
                    context.emplace_diagnostic_with_message_value(
                        Span{context, fid, expr}, diag_code::takes_no_arguments, diag_type::error,
                        DiagnosticSymbolBeforeMessage{.sid = context.def(func_did).name});
                }
                return true;
            }
            return false;
        };

    if (needs_generic_deduction) {
        const auto maybe_deduction_guide = context.deduction_guide_for_def(func_did);
        if (maybe_deduction_guide.empty()) {

            // try to give the helpful possible diagnostics
            if (const auto& gen_fn_def = context.def(func_did);
                gen_fn_def.holds<DefGenericFunction>()
                && check_too_many_args(exprs.len, gen_fn_def.as<DefGenericFunction>().param_cnt,
                                       func_did, maybe_self_val, fid, expr, exprs)) {
                return {};
            }

            const auto d0 = context.emplace_diagnostic_with_message_value(
                Span{context, fid, expr}, diag_code::cannot_deduce_generic_paramters_for_function,
                diag_type::error, DiagnosticSymbolAfterMessage{.sid = context.def(func_did).name});
            const auto d1 = context.emplace_diagnostic_with_message_value(
                context.name_span_for_def(func_did), diag_code::declared_here_as_generic,
                diag_type::note, DiagnosticSymbolBeforeMessage{.sid = context.def(func_did).name});
            context.link_diagnostic(d0, d1);
            return {};
        }
        for (HirSize i = 0; i < exprs.len; i++) {
            const ast_expr_t* arg = exprs.start[i];

            OptId<ExecId> maybe_arg_eid
                = solve_expr_helper(solver, fid, scope, maybe_move_map, arg);
            if (maybe_arg_eid.empty()) {
                issue = true;
            } else {
                out_args.push_back(maybe_arg_eid.as_id());
            }
            total_arg_cnt++;
        }
        const auto maybe_gen_args = try_generic_args_from_deduction_guide(
            solver, out_args, maybe_deduction_guide.as_id());
        if (maybe_gen_args.empty()) {

            // try to give the helpful possible diagnostics
            if (const auto& gen_fn_def = context.def(func_did);
                gen_fn_def.holds<DefGenericFunction>()
                && check_too_many_args(exprs.len, gen_fn_def.as<DefGenericFunction>().param_cnt,
                                       func_did, maybe_self_val, fid, expr, exprs)) {
                return {};
            }

            const auto d0 = context.emplace_diagnostic_with_message_value(
                Span{context, fid, expr}, diag_code::cannot_deduce_generic_paramters_for_function,
                diag_type::error, DiagnosticSymbolAfterMessage{.sid = context.def(func_did).name});
            const auto d1 = context.emplace_diagnostic_with_message_value(
                context.name_span_for_def(func_did), diag_code::declared_here_as_generic,
                diag_type::note, DiagnosticSymbolBeforeMessage{.sid = context.def(func_did).name});
            context.link_diagnostic(d0, d1);
            return {};
        }

        const OptId<DefId> maybe_gen_instant = context.try_generic_instantiation(
            solver.get_def_visitor(), func_did, maybe_gen_args.as_id());
        if (maybe_gen_instant.empty()) {
            return {};
        }
        func_did = maybe_gen_instant.as_id();
        params = context.def(maybe_gen_instant.as_id()).as<DefFunction>().params;
    } else {

        params = context.def(func_did).as<DefFunction>().params;

        for (HirSize i = 0; i < exprs.len; i++) {
            const ast_expr_t* arg = exprs.start[i];

            const auto param_index = i + (maybe_self_val.has_value() ? 1 : 0);

            OptId<TypeId> maybe_into_tid = (param_index < params.len())
                                               ? OptId<TypeId>{context.def(params.get(param_index))
                                                                   .as<DefVariable>()
                                                                   .type_id}
                                               : std::nullopt;

            OptId<ExecId> maybe_arg_eid
                = solve_expr_helper(solver, fid, scope, maybe_move_map, arg, maybe_into_tid);
            if (maybe_arg_eid.empty()) {
                issue = true;
            } else {
                out_args.push_back(maybe_arg_eid.as_id());
            }
            total_arg_cnt++;
        }
    }

    issue |= check_too_many_args(total_arg_cnt, params.len(), func_did, maybe_self_val, fid, expr,
                                 exprs);

    if (!context.def(func_did).as<DefFunction>().poisoned() && issue) {
        if (context.diagnostic_count() > prior_diag_cnt) {
            auto d = context.emplace_diagnostic_with_message_value(
                context.name_span_for_def(func_did), diag_code::declared_here, diag_type::note,
                DiagnosticSymbolBeforeMessage{.sid = context.def(func_did).name});
            context.force_link_diagnostic(d);
        }
    }

    // final guard before forwarding params -> execs
    if (issue || (out_args.size() != params.len())) {
        return {}; // just in case
    }

    if (context.def(func_did).as<DefFunction>().posioned) {
        return {}; // already posioned so don't even try it
    }

    return true;
}

template <IsExprSolver Solver>
[[nodiscard]] OptId<ExecId> solve_indirect_fn_call_impl(Solver& solver, FileId fid, ScopeId scope,
                                                        OptId<MoveMapId> maybe_move_map,
                                                        const ast_expr_t* expr, ExecId callee_eid) {
    Context& context = solver.get_context();
    const TypeId fn_ptr_tid = context.infer_type_from_exec(callee_eid).as_id();
    const IdSlice<TypeId> param_types = context.type(fn_ptr_tid).as<TypeFnPtr>().param_types;
    const ast_slice_of_exprs_t exprs = expr->expr.fn_call.args;

    bool issue = false;
    llvm::SmallVector<ExecId> arg_vec{};
    for (HirSize i = 0; i < exprs.len; i++) {
        const OptId<TypeId> maybe_into_tid
            = (i < param_types.len()) ? OptId<TypeId>{context.type_id(param_types.get(i))}
                                      : std::nullopt;
        const OptId<ExecId> maybe_arg_eid
            = solve_expr_helper(solver, fid, scope, maybe_move_map, exprs.start[i], maybe_into_tid);
        if (maybe_arg_eid.empty()) {
            issue = true;
        } else {
            arg_vec.push_back(maybe_arg_eid.as_id());
        }
    }

    if (exprs.len != param_types.len()) {
        const SymbolId callee_sid = context.symbol_id(context.exec(callee_eid).span);
        if (param_types.len() == 0) {
            context.emplace_diagnostic_with_message_value(
                Span{context, fid, expr}, diag_code::takes_no_arguments, diag_type::error,
                DiagnosticSymbolBeforeMessage{.sid = callee_sid});
        } else {
            const Span span_of_interest
                = (exprs.len < param_types.len())
                      ? Span{context, fid, expr->last}
                      : Span::combine(Span{context, fid, exprs.start[0]->first},
                                      Span{context, fid, exprs.start[exprs.len - 1]->last});
            context.emplace_diagnostic_with_message_value(
                span_of_interest, diag_code::expected, diag_type::error,
                DiagnosticSymButGotSym{.leading = callee_sid,
                                       .sid1 = ExecConst{param_types.len()}.to_symbol_id(context),
                                       .sid2 = ExecConst{exprs.len}.to_symbol_id(context)});
        }
        return {};
    }
    if (issue) {
        return {};
    }

    return context.emplace_exec(
        ExecFnCall{.callee = callee_eid, .args = context.freeze_id_vec(arg_vec)},
        Span{context, fid, expr});
}

template <IsExprSolver Solver>
[[nodiscard]] OptId<ExecId>
solve_fn_call_impl(Solver& solver, FileId fid, ScopeId scope, OptId<MoveMapId> maybe_move_map,
                   const ast_expr_t* expr, OptId<ExecId> maybe_self_val = std::nullopt) {
    assert(expr->type == AST_EXPR_FN_CALL);

    Context& context = solver.get_context();

    // record this now so we know better if we've previously generated diagnostics. this is
    // helpful for try_compt_fn_call(...)'s diagnostic propagation logic
    [[maybe_unused]] const auto prior_diag_cnt = context.diagnostic_count();

    // try to find our function, instantiated generic function, or raw generic function (in this
    // case we must later deduce generic arguments)
    const std::optional<FuncLookUp> look_up_res
        = try_fn_look_up_from_expr_impl(solver, fid, scope, maybe_move_map, expr, maybe_self_val);

    // failure, so propogate up
    if (!look_up_res.has_value()) {
        return {};
    }

    // the look-up internally solved a variant initialzier, so just return the result of that
    if (look_up_res->did_variant_initializer) {
        return look_up_res->maybe_variant_init_res;
    }

    if (look_up_res->maybe_indirect_callee.has_value()) {
        return solve_indirect_fn_call_impl(solver, fid, scope, maybe_move_map, expr,
                                           look_up_res->maybe_indirect_callee.as_id());
    }

    // our running func_did (this will only get updated henceforth if we deduce generic args for
    // a raw generic function)
    DefId func_did = look_up_res->func_did.as_id();

    llvm::SmallVector<ExecId> arg_vec{};
    const Span span{context, fid, expr};

    if constexpr (!std::same_as<Solver, ComptExprSolver>) {
        if (context.def(func_did).compt) {
            ComptExprSolver compt_solver{solver.get_def_visitor()};
            if (!try_fn_args_from_expr_and_preliminary_function_def_id_impl(
                    compt_solver, fid, scope, {}, expr, maybe_self_val, /*&*/ func_did,
                    look_up_res->needs_generic_deduction, /*&*/ arg_vec)) {
                return {};
            }
            return compt_solver.try_compt_fn_call(func_did, arg_vec, prior_diag_cnt, span);
        }
    }

    if (!try_fn_args_from_expr_and_preliminary_function_def_id_impl(
            solver, fid, scope, maybe_move_map, expr, maybe_self_val, /*&*/ func_did,
            look_up_res->needs_generic_deduction, /*&*/ arg_vec)) {
        return {};
    }

    if constexpr (std::same_as<Solver, ComptExprSolver>) {
        return solver.try_compt_fn_call(func_did, arg_vec, prior_diag_cnt, span);
    } else {
        const ExecId callee_eid = emplace_fn_ptr_exec(
            context, func_did, Span{context, fid, expr->expr.fn_call.left_expr});
        return context.emplace_exec(
            ExecFnCall{.callee = callee_eid, .args = context.freeze_id_vec(arg_vec)}, span);
    }
}

/// reads a solved compt subscript index (u64, or u32 on 32-bit targets)
[[nodiscard]] inline usize compt_subscript_idx(Context& context, ExecId idx_eid) {
    const ExecConst& idx_const = context.exec(idx_eid).as<ExecConst>();
    assert(idx_const.holds<u64>() || idx_const.holds<u32>());
    return idx_const.holds<u32>() ? usize{idx_const.as<u32>()} : idx_const.as<usize>();
}

/// issues an out of bounds error for a compt subscript, returns whether idx was out of bounds
[[nodiscard]] inline bool diag_if_compt_subscript_out_of_bounds(Context& context, usize idx,
                                                                size_t len, Span idx_span) {
    if (idx < len) {
        return false;
    }
    context.emplace_diagnostic_with_message_value(
        idx_span, diag_code::only_message_value_is_meaningful, diag_type::error,
        DiagnosticIdxOutOfBounds{.idx_sid = context.symbol_id(std::to_string(idx)),
                                 .length_sid = context.symbol_id(std::to_string(len))});
    return true;
}

/// folds a subscript into a compt list literal
[[nodiscard]] inline OptId<ExecId> fold_compt_list_subscript(Context& context, ExecId list_eid,
                                                             ExecId idx_eid, Span idx_span,
                                                             Span expr_span) {
    const ExecListLiteral list = context.exec(list_eid).as<ExecListLiteral>();
    const usize idx = compt_subscript_idx(context, idx_eid);
    if (diag_if_compt_subscript_out_of_bounds(context, idx, list.len(), idx_span)) {
        return {};
    }
    const Exec& elem_exec = context.exec(list.elems.get(idx));
    return context.emplace_exec(elem_exec.value, expr_span, true);
}

/// folds a subscript into a compt list literal for values of type str
[[nodiscard]] inline OptId<ExecId> fold_compt_str_subscript(Context& context, ExecId str_eid,
                                                            ExecId idx_eid, Span idx_span,
                                                            Span expr_span) {
    const SymbolId sid = context.exec(str_eid).as<ExecConst>().as<SymbolId>();
    const usize idx = compt_subscript_idx(context, idx_eid);
    const std::string_view sv = context.symbol(sid);
    if (diag_if_compt_subscript_out_of_bounds(context, idx, sv.size(), idx_span)) {
        return {};
    }
    return context.emplace_exec(ExecConst{sv.at(idx)}, expr_span, true);
}

/// true when bt is an unsigned integral type (u8, u16, u32, u64)
[[nodiscard]] inline bool is_unsigned_builtin(builtin_type bt) {
    return bt == builtin_type::u8 || bt == builtin_type::u16 || bt == builtin_type::u32
           || bt == builtin_type::u64;
}

/// unary minus on an unsigned type is always illegal (compt or runtime), like rust; issues an
/// error naming the unsigned type + a help for how to get wrapping negation
inline void diag_unsigned_negation(Context& context, Span span, builtin_type bt) {
    auto d0 = context.emplace_diagnostic_with_message_value(
        span, diag_code::cannot_negate_value_of_unsigned_type, diag_type::error,
        DiagnosticSymbolAfterMessage{.sid = context.symbol_id(builtin_type_to_cstr(bt))});
    auto d1 = context.emplace_diagnostic(
        span, diag_code::for_wrapping_negation_subtract_from_zero_instead, diag_type::help);
    context.link_diagnostic(d0, d1);
}

} // namespace hir

#endif
