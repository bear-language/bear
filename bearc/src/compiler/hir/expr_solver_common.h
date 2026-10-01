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
#include "compiler/hir/context.hpp"
#include "compiler/hir/exec.hpp"
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

} // namespace hir

#endif
