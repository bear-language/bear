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
#include "compiler/hir/exec.hpp"
#include "compiler/hir/expr_solver.hpp"
#include "compiler/hir/indexing.hpp"

namespace hir {

enum class compt_or_runtime : uint8_t {
    compt,
    runtime,
};

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

template <compt_or_runtime C>
[[nodiscard]] static inline OptId<ExecId>
handle_any_id_impl(Context& context, DefVisitor& def_visitor, FileId fid, ScopeId scope,
                   token_ptr_slice_t id_slice, OptId<GenericArgIdSliceId> maybe_gen_args) {
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
        const auto variable_exec
            = context.emplace_exec(ExecVariable{.def_id = did, .type_id = tid}, expr_span);
        // try auto-dereference
        if (context.type(tid).holds<TypeRef>()) {
            return context.emplace_exec(ExecDeref{.dereferenced = variable_exec}, expr_span);
        }
        return variable_exec;
    }
    // we hit a def corresponding to a function, so a compt function pointer is quite
    // helpful here.
    if (def.holds<DefFunction>()) {
        const DefFunction& func_def = def.as<DefFunction>();
        return context.emplace_compt_exec(
            ExecFnPtr{.func_def_id = did,
                      .fn_ptr_tid
                      = context.emplace_type(TypeFnPtr{.param_types = func_def.param_types,
                                                       .return_type = func_def.return_type},
                                             Span::generated(), false)},
            expr_span);
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

template <IsExprSolver Solver>
[[nodiscard]] static inline OptId<ExecId>
solve_struct_or_union_init(Solver& solver, FileId fid, ScopeId scope, const ast_expr_t* expr,
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
                = std::same_as<Solver, ComptExprSolver>
                      ? solver.lower_generic_args(fid, scope, expr->expr.struct_init.generic_args,
                                                  false)
                      : ComptExprSolver{def_visitor}.lower_generic_args(
                            fid, scope, expr->expr.struct_init.generic_args, false);
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
            return solve_union_init(solver, fid, scope, maybe_union_def.as_id(), expr);
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

    if (maybe_struct_did.empty()) {
        return {};
    }
    DefId struct_did = maybe_struct_did.as_id();
    maybe_eid = solve_struct_init(solver, fid, scope, struct_did, expr, into_tid);
    return maybe_eid;
}

template <IsExprSolver Solver>
[[nodiscard]] static inline OptId<ExecId> solve_union_init(Solver& solver, FileId fid,
                                                           ScopeId scope, DefId union_did,
                                                           const ast_expr_t* expr) {

    static constexpr bool is_compt_eval = std::same_as<Solver, ComptExprSolver>;
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
    OptId<ExecId> maybe_val
        = solver.solve_expr(fid, scope, member_init->expr.struct_member_init.value, needed_tid);
    if (maybe_val.empty()) {
        return {}; // poisoned
    }
    return context.emplace_exec(
        ExecUnionInit{
            .member_init = maybe_val.as_id(),
            .union_def_id = union_did,
            .active_member_idx = context.def(matched_did).member_idx,
        },
        Span{context, fid, expr}, /*should_be_compt=*/is_compt_eval);
}

template <IsExprSolver Solver>
[[nodiscard]] OptId<ExecId> solve_struct_init(Solver& solver, FileId fid, ScopeId scope,
                                              DefId struct_did, const ast_expr_t* expr,
                                              OptId<TypeId> into_tid) {

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

    llvm::SmallVector<ExecId> member_init_execs;
    bool cooked = false;
    for (auto i = 0uz; i < member_dids.len(); i++) {
        auto didx = member_dids.get(i);
        const Def& member = context.def(didx);

        if (member.holds<DefUnevaluated>()) {
            continue; // must be poisoned
        }

        assert(member.holds<DefVariable>());
        const auto& member_as_var = member.as<DefVariable>();

        const TypeId member_type = member_as_var.type_id;
        const OptId<ExecId> default_val = member_as_var.compt_value;

        // handle too few
        if (i >= init_slice.len) {
            // get default value for the member field
            if (default_val.has_value()) {
                member_init_execs.emplace_back(default_val.as_id());
            } else {
                cooked = true;
                context.emplace_diagnostic(
                    Span(fid, context.ast(fid).buffer(), expr->last),
                    diag_code::struct_field_not_initialized, diag_type::error,
                    DiagnosticSymbolAfterMessage{.sid = context.def(member_dids.get(i)).name},
                    DiagnosticNoOtherInfo{});
            }
            continue; // guard overflow
        }
        assert(i < init_slice.len);
        const ast_expr_t* member_init_expr = init_slice.start[i];

        if (member_init_expr->type != AST_EXPR_STRUCT_MEMBER_INIT) {
            return std::nullopt; // malformed, so was already reported by parser
        }
        const token_t* proposed_member_name_tkn = member_init_expr->expr.struct_member_init.id;
        const ast_expr_t* proposed_val = member_init_expr->expr.struct_member_init.value;
        const Span proposed_member_span
            = Span(fid, context.ast(fid).buffer(), member_init_expr->first, member_init_expr->last);

        const SymbolId true_name = member.name;
        if (context.symbol_id(proposed_member_name_tkn) != true_name) {
            cooked = true;
            context.emplace_diagnostic(proposed_member_span,
                                       diag_code::field_initializer_does_not_match_field,
                                       diag_type::error, DiagnosticSymbolAfterMessage{member.name},
                                       DiagnosticNoOtherInfo{});
            continue;
        }
        OptId<ExecId> hopefully_exec = solver.solve_expr(fid, scope, proposed_val, member_type);
        if (!hopefully_exec.has_value()) {
            cooked = true;
            // just continue, caused by other so must have already been reported
            continue;
        }
        // emplace the init execs
        member_init_execs.emplace_back(hopefully_exec.as_id());
    }
    if (rel_arity == relative_arity::too_many) {
        cooked = true;
        const token_t* first = init_slice.start[member_dids.len()]->first;
        const token_t* last = init_slice.start[init_slice.len - 1]->last;
        context.emplace_diagnostic(Span(fid, context.ast(fid).buffer(), first, last),
                                   diag_code::too_many_initializers_given_for_struct_init,
                                   diag_type::error);
    }
    if (cooked) {
        context.emplace_diagnostic(
            context.def(struct_did).span, diag_code::declared_here, diag_type::note,
            DiagnosticSymbolBeforeMessage{context.def(struct_did).name}, DiagnosticNoOtherInfo{});
        return std::nullopt;
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
        Span{context, fid, expr}, /*should_be_compt=*/is_compt_eval);
}

} // namespace hir

#endif
