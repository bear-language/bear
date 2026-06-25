//     /                              /
//    /                              /
//   /_____  _____  _____  _____    /  _____   _  _  _____
//  /     / /____  /____/ /____/   /  /____/  /\  / /  __
// /_____/ /____  /    / /   \    /  /    /  /  \/ /____/
// Copyright (C) 2025-2026 Zachary Mahan
// Licensed under the GNU GPL v3. See LICENSE for details.

#ifndef COMPILER_HIR_COMPT_EXPR_SOLVER_HPP
#define COMPILER_HIR_COMPT_EXPR_SOLVER_HPP

#include "compiler/ast/expr.h"
#include "compiler/ast/params.h"
#include "compiler/ast/stmt.h"
#include "compiler/ast/type.h"
#include "compiler/hir/context.hpp"
#include "compiler/hir/def.hpp"
#include "compiler/hir/diagnostic.hpp"
#include "compiler/hir/exec.hpp"
#include "compiler/hir/exec_ops.hpp"
#include "compiler/hir/exec_proving.hpp"
#include "compiler/hir/expr_solver.hpp"
#include "compiler/hir/indexing.hpp"
#include "compiler/hir/matching.hpp"
#include "compiler/hir/span.hpp"
#include "compiler/hir/type.hpp"
#include "compiler/parser/token_eaters.h"
#include "compiler/token.h"
#include "def_visitor.hpp"
#include <cassert>
#include <optional>
#include <utility>
namespace hir {

template <IsDefVisitor V> class ComptExprSolver {
    Context& context;
    V& def_visitor;
    HirSize call_depth = 0;

  public:
    static constexpr HirSize MAX_COMPT_CALL_FRAMES = 100; // TODO increase once memoization is added

    ComptExprSolver(Context& ctx, V& def_visitor) : context{ctx}, def_visitor{def_visitor} {}

    [[nodiscard]] Context& get_context() { return this->context; }

    [[nodiscard]] OptId<ExecId> solve_expr(FileId fid, ScopeId scope, const ast_expr_t* expr) {
        return solve_expr(fid, scope, expr, std::nullopt);
    }

    [[nodiscard]] OptId<TypeId> infer_type_from_compt_expr(FileId fid, ScopeId scope,
                                                           const ast_expr_t* expr) {

        if (expr->type == AST_EXPR_ID) {
            auto sid = context.symbol_slice(expr->expr.id.slice);
            Span span{context, fid, expr->first, expr->last};
            auto maybe_did = context.look_up_scoped_variable(scope, sid, span);
            if (maybe_did.empty()) {
                context.emplace_diagnostic(span, diag_code::use_of_undeclared_identifier,
                                           diag_type::error);
                return std::nullopt;
            }
            const Def& def = context.def(def_visitor.visit_as_dependent(maybe_did.as_id()));
            if (!def.holds<DefVariable>()) {
                return std::nullopt;
            }
            return def.as<DefVariable>().type_id;
        }

        OptId<ExecId> maybe_eid = solve_expr(fid, scope, expr);

        if (maybe_eid.empty()) {
            return std::nullopt; // already an issue/poisoned
        }

        auto eid = maybe_eid.as_id();

        return infer_type_from_exec(eid);
    }

    [[nodiscard]] OptId<TypeId> infer_type_from_exec(ExecId eid) {
        const Exec& exec = context.exec(eid);
        if (exec.holds<ExecExprComptConstant>()) {
            auto bin_type = exec.as<ExecExprComptConstant>().type_builtin();
            return context.emplace_type(TypeBuiltin{.type = bin_type}, Span::generated(), false);
        }
        if (exec.holds<ExecExprStructInit>()) {
            auto struct_did = exec.as<ExecExprStructInit>().struct_def_id;

            const auto struct_def = context.def(struct_did);

            if (!struct_def.template holds<DefStruct>()) {
                return {};
            }

            const auto maybe_gen_args = struct_def.template as<DefStruct>().maybe_generic_args;

            return context.emplace_type(TypeStruct{.def_id = struct_did,
                                                   .gen_args_slice = maybe_gen_args,
                                                   .generic = maybe_gen_args.has_value()},
                                        Span::generated(), false);
        }
        if (exec.holds<ExecExprListLiteral>()) {
            auto list_exec = exec.as<ExecExprListLiteral>();
            auto maybe_contained_type = list_exec.elem_type_id;
            if (maybe_contained_type.empty()) {
                return std::nullopt;
            }
            auto contained_type = maybe_contained_type.as_id();
            auto len = list_exec.len();
            return context.emplace_type(
                TypeArr{
                    .inner = contained_type,
                    .compt_size_expr = std::nullopt,
                    .canonical_size = len,
                },
                Span::generated(), false);
        }
        if (exec.holds<ExecFnPtr>()) {
            return exec.as<ExecFnPtr>().fn_ptr_tid;
        }
        if (exec.holds<ExecExprUnionInit>()) {
            return context.emplace_type(
                TypeUnion{.def_id = exec.as<ExecExprUnionInit>().union_def_id}, Span::generated(),
                false);
        }
        if (exec.holds<ExecExprVariantInit>()) {
            const auto did = exec.as<ExecExprVariantInit>().variant_def_id;
            const auto variant_def = context.def(did);

            if (!variant_def.template holds<DefVariant>()) {
                return {};
            }

            const auto maybe_gen_args = variant_def.template as<DefVariant>().maybe_generic_args;

            return context.emplace_type(TypeVariant{.def_id = did,
                                                    .gen_args_slice = maybe_gen_args,
                                                    .generic = maybe_gen_args.has_value()},

                                        Span::generated(), false);
        }

        // report issue
        context.emplace_diagnostic(exec.span, diag_code::cannot_infer_type_at_compt,
                                   diag_type::error);

        return std::nullopt;
    }

    // solves a top level compt expr (this is primarily for array sizing & builtin types for top
    // level generic instantiation with compt parameterizations)
    [[nodiscard]] OptId<ExecId> solve_expr(FileId fid, ScopeId scope, const ast_expr_t* expr,
                                           OptId<TypeId> maybe_into_tid) {

        if (expr->type == AST_EXPR_INVALID) {
            return {};
        }

        while (expr->type == AST_EXPR_GROUPING) {
            expr = expr->expr.grouping.expr;
        }

        auto expr_is_mem_access = +[](const ast_expr_t* expr) {
            return expr->type == AST_EXPR_BINARY && expr->expr.binary.op->type == TOK_DOT;
        };

        // no type provided, so try to infer
        if (!maybe_into_tid.has_value()) {
            if (expr->type == AST_EXPR_ID) {
                return handle_any_id(fid, scope, expr->expr.id.slice);
            }
            if (expr->type == AST_EXPR_GENERIC_ID) {
                return handle_any_generic_id(fid, scope, expr->expr.generic_id.slice,
                                             expr->expr.generic_id.args);
            }
            if (expr->type == AST_EXPR_STRUCT_INIT) {
                return solve_struct_or_union(fid, scope, expr, {});
            }
            if (expr->type == AST_EXPR_LIST_LITERAL) {
                return solve_list(fid, scope, expr, maybe_into_tid);
            }
            if (expr->type == AST_EXPR_MATCH) {
                return handle_match(fid, scope, expr);
            }
            if (expr_is_mem_access(expr)) {
                return solve_expr_binary(fid, scope, expr);
            }
            return solve_builtin_compt_expr(fid, scope, expr, std::nullopt, maybe_into_tid);
        }

        const Type& orig_can_be_ref = context.type(maybe_into_tid.as_id());
        const auto into_tid = context.try_decay(maybe_into_tid.as_id());
        const Type& into_type = context.type(into_tid);

        // `var` provided as type, so try to infer
        if (into_type.holds<TypeVar>()) {
            if (expr->type == AST_EXPR_ID) {
                return handle_any_id(fid, scope, expr->expr.id.slice);
            }
            if (expr->type == AST_EXPR_GENERIC_ID) {
                return handle_any_generic_id(fid, scope, expr->expr.generic_id.slice,
                                             expr->expr.generic_id.args);
            }
            if (expr->type == AST_EXPR_STRUCT_INIT) {
                return solve_struct_or_union(fid, scope, expr, into_tid);
            }
            if (expr->type == AST_EXPR_LIST_LITERAL) {
                return solve_list(fid, scope, expr, into_tid);
            }
            if (expr->type == AST_EXPR_MATCH) {
                return handle_match(fid, scope, expr);
            }
            if (expr_is_mem_access(expr)) {
                return solve_expr_binary(fid, scope, expr);
            }
            return solve_builtin_compt_expr(fid, scope, expr, std::nullopt, into_tid);
        }

        if (orig_can_be_ref.holds<TypeRef>() && expr->type == AST_EXPR_BORROW) {
            return solve_expr_borrow(fid, scope, expr, into_tid);
        }

        if (into_type.holds<TypePtr>()) {
            OptId<ExecId> maybe_eid = solve_builtin_compt_expr(fid, scope, expr, {}, {});
            if (maybe_eid.has_value()) {
                OptId<TypeId> maybe_tid = infer_type_from_exec(maybe_eid.as_id());
                if (maybe_tid.has_value()) {
                    const Type& ty = context.type(maybe_tid.as_id());
                    if (ty.holds<TypeBuiltin>()
                        and ty.as<TypeBuiltin>().type == builtin_type::nullpointer) {
                        return maybe_eid.as_id();
                    }
                }
            }
            auto inner_tid = into_type.as<TypePtr>().inner;
            auto inner = context.type(inner_tid);
            auto d0 = context.emplace_diagnostic(
                into_type.span, diag_code::pointers_are_not_assignable_at_compt, diag_type::error);
            if (inner.template holds_any_of<TypeStruct, TypeBuiltin>()) {
                auto d1 = context.emplace_diagnostic_with_message_value(
                    into_type.span, diag_code::replace_with, diag_type::help,
                    DiagnosticTypeAfterMessage{.tid = inner_tid});
                context.link_diagnostic(d0, d1);
            }
            return std::nullopt;
        }

        if (expr_is_mem_access(expr)) {
            auto maybe_eid = solve_expr_binary(fid, scope, expr);
            if (maybe_eid.empty()) {
                return std::nullopt; // poison
            }
            auto maybe_tid = infer_type_from_exec(maybe_eid.as_id());
            if (maybe_tid.empty()) {
                return std::nullopt; // poison
            }

            // guard diff type
            if (!context.equivalent_type(into_tid, maybe_tid.as_id())) {
                context.emplace_diagnostic_with_message_value(
                    Span(fid, context.ast(fid).buffer(), expr->first, expr->last),
                    diag_code::cannot_convert_value_of_type, diag_type::error,
                    DiagnosticTypeToType{.from = maybe_tid.as_id(), .to = into_tid});
                return std::nullopt;
            }
        }

        // try TypeArr at compt
        if (into_type.holds<TypeArr>()) {
            auto maybe_list = solve_list(fid, scope, expr, into_tid);
            if (maybe_list.empty()) {
                return std::nullopt; // poisoned
            }
            auto list_eid = maybe_list.as_id();

            auto maybe_list_type = infer_type_from_exec(list_eid);

            if (maybe_list_type.empty()) {
                return std::nullopt; // poisoned
            }

            TypeId list_type = maybe_list_type.as_id();

            // guard diff type
            if (!context.equivalent_type(into_tid, list_type)) {
                context.emplace_diagnostic_with_message_value(
                    Span(fid, context.ast(fid).buffer(), expr->first, expr->last),
                    diag_code::cannot_convert_value_of_type, diag_type::error,
                    DiagnosticTypeToType{.from = list_type, .to = into_tid});
                return std::nullopt;
            }

            return list_eid;
        }

        if (into_type.holds<TypeFnPtr>()
            && (expr->type == AST_EXPR_ID || expr->type == AST_EXPR_GENERIC_ID)) {
            OptId<ExecId> maybe_fnp
                = (expr->type == AST_EXPR_ID)
                      ? handle_any_id(fid, scope, expr->expr.id.slice)
                      : handle_any_generic_id(fid, scope, expr->expr.generic_id.slice,
                                              expr->expr.generic_id.args);
            if (maybe_fnp.empty()) {
                return std::nullopt; // poisoned
            }

            auto fnp_eid = maybe_fnp.as_id();

            auto maybe_tnp_tid = infer_type_from_exec(fnp_eid);

            if (maybe_tnp_tid.empty()) {
                return std::nullopt; // poisoned
            }

            TypeId fnp_tid = maybe_tnp_tid.as_id();

            // guard diff type
            if (!context.equivalent_type(into_tid, fnp_tid)) {
                context.emplace_diagnostic_with_message_value(
                    Span(fid, context.ast(fid).buffer(), expr->first, expr->last),
                    diag_code::cannot_convert_value_of_type, diag_type::error,
                    DiagnosticTypeToType{.from = fnp_tid, .to = into_tid});
                return std::nullopt;
            }

            return fnp_eid;
        }

        if (into_type.holds<TypeStruct>() || into_type.holds<TypeUnion>()) {
            return solve_struct_or_union(fid, scope, expr, into_tid);
        }

        // handle MyVariant..Thingy(var a)
        if (into_type.holds<TypeVariant>() && expr->type == AST_EXPR_FN_CALL) {
            const auto maybe_eid = solve_fn_call(fid, scope, expr);
            if (maybe_eid.empty()) {
                return std::nullopt; // poison
            }
            auto maybe_tid = infer_type_from_exec(maybe_eid.as_id());
            if (maybe_tid.empty()) {
                return std::nullopt; // poison
            }

            // guard diff type
            if (!context.equivalent_type(into_tid, maybe_tid.as_id())) {
                context.emplace_diagnostic_with_message_value(
                    Span{context, fid, expr}, diag_code::cannot_convert_value_of_type,
                    diag_type::error,
                    DiagnosticTypeToType{.from = maybe_tid.as_id(), .to = into_tid});
                return std::nullopt;
            }
            return maybe_eid.as_id();
        }

        // handle MyThingy..ThingyField
        if (into_type.holds<TypeVariant>()) {
            OptId<ExecId> maybe_eid{};

            if (expr->type == AST_EXPR_ID) {
                maybe_eid = handle_any_id(fid, scope, expr->expr.id.slice);
            } else if (expr->type == AST_EXPR_MATCH) {
                maybe_eid = handle_match(fid, scope, expr);
            } else {
                maybe_eid = solve_expr(fid, scope, expr); // check type down at the guard
            }

            if (maybe_eid.empty()) {
                return std::nullopt; // poison
            }
            auto maybe_tid = infer_type_from_exec(maybe_eid.as_id());
            if (maybe_tid.empty()) {
                return std::nullopt; // poison
            }

            // guard diff type
            if (!context.equivalent_type(into_tid, maybe_tid.as_id())) {
                context.emplace_diagnostic_with_message_value(
                    Span{context, fid, expr}, diag_code::cannot_convert_value_of_type,
                    diag_type::error,
                    DiagnosticTypeToType{.from = maybe_tid.as_id(), .to = into_tid});
                return std::nullopt;
            }
            return maybe_eid.as_id();
        }

        // guard against non-builtins
        if (!into_type.holds<TypeBuiltin>()) {
            context.emplace_diagnostic(Span{context, fid, expr}, diag_code::cannot_resolve_at_compt,
                                       diag_type::error);
            return OptId<ExecId>{};
        }
        builtin_type into_builtin_type = into_type.as<TypeBuiltin>().type;
        return solve_builtin_compt_expr(fid, scope, expr, into_builtin_type, into_tid);
    }

    void enter_compt_fn() { ++call_depth; }

    void exit_compt_fn() { --call_depth; }

    [[nodiscard]] OptId<ExecId> solve_builtin_compt_expr(FileId fid, ScopeId scope,
                                                         const ast_expr_t* expr,
                                                         std::optional<builtin_type> into_builtin,
                                                         OptId<TypeId> into_tid = {}) {
        auto emplace_e = [this, fid, expr](ExecValue val) {
            return context.register_exec(
                context, val, Span(fid, context.ast(fid).buffer(), expr->first, expr->last), true);
        };

        if (into_tid.has_value() && !into_builtin.has_value()
            && context.type(into_tid.as_id()).template holds<TypeBuiltin>()) {
            into_builtin = context.type(into_tid.as_id()).template as<TypeBuiltin>().type;
        }

        auto cannot_conv = [this, fid, expr, into_builtin]() {
            context.emplace_diagnostic_with_message_value(
                Span{context, fid, expr}, diag_code::cannot_convert_expression_to_type,
                diag_type::error,
                DiagnosticTypeAfterMessage{
                    .tid = context.emplace_type(TypeBuiltin{.type = into_builtin.value()},
                                                Span::generated(), false)});
            return std::nullopt;
        };

        auto cannot_conv_from = [this, fid, expr, into_builtin](TypeId tid) {
            context.emplace_diagnostic_with_message_value(
                Span{context, fid, expr}, diag_code::cannot_convert_value_of_type, diag_type::error,
                DiagnosticTypeToType{
                    .from = tid,
                    .to = context.emplace_type(TypeBuiltin{.type = into_builtin.value()},
                                               Span::generated(), false)});
            return std::nullopt;
        };

        auto guard_type_mismatch = [this, into_builtin, &cannot_conv,
                                    &cannot_conv_from](OptId<ExecId> maybe_eid) -> OptId<ExecId> {
            if (!into_builtin.has_value()) {
                return maybe_eid;
            }
            if (maybe_eid.empty()) {
                return std::nullopt; // poisoned
            }

            const auto eid = maybe_eid.as_id();

            const OptId<TypeId> maybe_inferred = infer_type_from_exec(eid);
            if (maybe_inferred.empty()) {
                cannot_conv();
                return std::nullopt;
            }

            const TypeId inffered_tid = maybe_inferred.as_id();
            const Type& inffered_type = context.type(inffered_tid);
            if (!inffered_type.template holds<TypeBuiltin>()) {
                cannot_conv_from(inffered_tid);
                return std::nullopt;
            }
            if (inffered_type.template as<TypeBuiltin>().type != into_builtin.value()) {
                cannot_conv_from(inffered_tid);
                return std::nullopt;
            }
            return eid;
        };

        std::optional<ExecConst> maybe_value;
        switch (expr->type) {
        case AST_EXPR_ID: {
            Span id_span{fid, context.ast(fid).buffer(), expr->expr.id.slice.start[0],
                         expr->expr.id.slice.start[expr->expr.id.slice.len - 1]};
            OptId<ExecId> maybe_eid = handle_any_id(fid, scope, expr->expr.id.slice);
            if (maybe_eid.empty()) {
                return {};
            }
            // happy path, canonicalize compt value
            const Exec& exec = context.exec(maybe_eid.as_id());

            maybe_value = exec.template try_as<ExecConst>();

            break;
        }
        case AST_EXPR_LITERAL: {
            const token_t* tkn = expr->expr.literal.tkn;
            switch (tkn->type) {
            case TOK_CHAR_LIT:
                maybe_value = ExecConst{tkn->val.character};
                break;
            // try as i32 if possible
            case TOK_INT_LIT: {
                maybe_value = ExecConst{tkn->val.signed_integral};
                auto maybe_signed = maybe_value->try_safe_convert_to(builtin_type::i32);
                if (maybe_signed.has_value()) {
                    maybe_value = maybe_signed;
                }
            } break;
                // try as i32 and then i64 if possible
            case TOK_UINT_LIT: {
                maybe_value = ExecConst{tkn->val.unsigned_integral};
                auto maybe_signed = maybe_value->try_safe_convert_to(builtin_type::i32);
                if (maybe_signed.has_value()) {
                    maybe_value = maybe_signed;
                } else {
                    maybe_signed = maybe_value->try_safe_convert_to(builtin_type::i64);
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
                maybe_value = ExecConst{context.symbol_id_for_str_lit_tkn(tkn)};
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
            break;
        }
        case AST_EXPR_BINARY: {
            auto maybe_eid = solve_expr_binary(fid, scope, expr);
            if (maybe_eid.has_value()) {
                const Exec& exec = context.exec(maybe_eid.as_id());
                if (exec.holds<ExecConst>()) {
                    maybe_value = exec.as<ExecConst>();
                } else {
                    maybe_value = std::nullopt;
                }
            } else {
                return std::nullopt; // poisoned
            }
            break;
        }
        case AST_EXPR_GROUPING: {
            // get inner
            return solve_builtin_compt_expr(fid, scope, expr->expr.grouping.expr, into_builtin,
                                            into_tid);
            break;
        }
        case AST_EXPR_PRE_UNARY: {
            // eventually allow sizeof, allignof
            token_type_e t = expr->expr.unary.op->type;
            OptId<ExecId> maybe_inner{};
            if (t == TOK_BOOL_NOT || t == TOK_PLUS || t == TOK_MINUS || t == TOK_BIT_NOT) {
                maybe_inner = solve_builtin_compt_expr(fid, scope, expr->expr.unary.expr,
                                                       std::nullopt, std::nullopt);
            } else {
                Span span{fid, context.ast(fid).buffer(), expr->expr.unary.op};
                auto d0 = context.emplace_diagnostic(span, diag_code::operator_not_viable_at_compt,
                                                     diag_type::error);
                // be more helpful for ++ and -- at compt
                if ((t == TOK_INC || t == TOK_DEC) && maybe_inner.has_value()) {
                    auto d1 = context.emplace_diagnostic(
                        Span{fid, context.ast(fid).buffer(), expr->expr.unary.expr->first,
                             expr->expr.unary.expr->last},
                        diag_code::immutable_value_is_not_assignable, diag_type::note,
                        DiagnosticNoOtherInfo{});
                    context.link_diagnostic(d0, d1);
                }
                return std::nullopt;
            }

            auto maybe_op = token_to_unary_op(expr->expr.unary.op);

            // guard malformed ops
            if (!maybe_op.has_value()) {
                context.emplace_diagnostic(
                    Span{fid, context.ast(fid).buffer(), expr->expr.unary.op},
                    diag_code::operator_not_viable_at_compt, diag_type::error);
                return std::nullopt;
            }

            // passed guard, so get value
            auto op = maybe_op.value();

            // already cooked (and error should have been reported)
            if (maybe_inner.empty()) {
                return std::nullopt;
            }

            Span op_span = Span{fid, context.ast(fid).buffer(), expr->expr.unary.op};

            OptId<ExecId> maybe_eid = solve_preunary_exec(op, op_span, maybe_inner.as_id());

            bool cooked = false;

            if (maybe_eid.has_value()) {
                auto exec = context.exec(maybe_eid.as_id());
                if (!exec.template holds<ExecConst>()) {
                    cooked = true;
                } else {
                    maybe_value = exec.template as<ExecConst>();
                }
            } else {
                cooked = true;
            }
            if (cooked) {
                return std::nullopt; // return w/ no diag since we're poisoned
            }
            break;
        }
        case AST_EXPR_POST_UNARY: {
            // not supported (-- or ++ require mutable lvalues)
            token_type_e t = expr->expr.unary.op->type;
            OptId<ExecId> maybe_inner = solve_builtin_compt_expr(fid, scope, expr->expr.unary.expr,
                                                                 into_builtin, into_tid);
            Span op_span{fid, context.ast(fid).buffer(), expr->expr.unary.op};
            auto d0 = context.emplace_diagnostic(op_span, diag_code::operator_not_viable_at_compt,
                                                 diag_type::error);
            // be more helpful for ++ and -- at compt
            if ((t == TOK_INC || t == TOK_DEC) && maybe_inner.has_value()) {
                auto d1 = context.emplace_diagnostic(
                    Span{fid, context.ast(fid).buffer(), expr->expr.unary.expr->first,
                         expr->expr.unary.expr->last},
                    diag_code::immutable_value_is_not_assignable, diag_type::note);
                context.link_diagnostic(d0, d1);
                return std::nullopt;
            }
            // inner is already cooked and error has been reported
            if (maybe_inner.empty()) {
                return std::nullopt;
            }
            return std::nullopt;
        }
        case AST_EXPR_TERNARY_IF: {
            return solve_ternary_if(fid, scope, expr, into_tid);
        }
        case AST_EXPR_COMPT:
            return solve_expr(fid, scope, expr->expr.compt_expr.inner, into_tid);
        case AST_EXPR_SAME_TYPE:
            return handle_same_type(fid, scope, expr);
        case AST_EXPR_INFERABLE_AS:
            return handle_inferable_as(fid, scope, expr);
        case AST_EXPR_TYPE_TO_STR:
            return handle_type_to_str(fid, scope, expr);
        case AST_EXPR_STATIC_ASSERT:
            return handle_static_assert(fid, scope, expr);
        case AST_EXPR_DEFINED:
            return handle_defined(fid, scope, expr);
        case AST_EXPR_FN_CALL: {
            OptId<ExecId> maybe_eid = solve_fn_call(fid, scope, expr);
            return guard_type_mismatch(maybe_eid);
        }
        case AST_EXPR_SUBSCRIPT: {
            OptId<ExecId> maybe_eid = solve_expr_subscript(fid, scope, expr);
            return guard_type_mismatch(maybe_eid);
        }
        case AST_EXPR_HAS_CONTRACT:
            return handle_has_contract(fid, scope, expr);
        case AST_EXPR_LIST_LITERAL:
        case AST_EXPR_STRUCT_INIT:
            if (into_builtin.has_value()) {
                context.emplace_diagnostic_with_message_value(
                    Span{context, fid, expr}, diag_code::cannot_convert_expression_to_type,
                    diag_type::error,
                    DiagnosticTypeAfterMessage{
                        .tid = context.emplace_type(TypeBuiltin{.type = into_builtin.value()},
                                                    Span::generated(), false)});
                return std::nullopt;
            }
            break;
        case AST_EXPR_MATCH: {
            const auto maybe_eid = handle_match(fid, scope, expr);
            if (maybe_eid.empty()) {
                return {}; // poisoned
            }
            if (context.exec(maybe_eid.as_id()).template holds<ExecConst>()) {
                maybe_value = context.exec(maybe_eid.as_id()).template as<ExecConst>();
            } else if (into_tid.has_value()) {
                context.emplace_diagnostic_with_message_value(
                    Span{context, fid, expr}, diag_code::cannot_convert_expression_to_type,
                    diag_type::error, DiagnosticTypeAfterMessage{.tid = into_tid.as_id()});
                return {};
            }
            break;
        }
        case AST_EXPR_GENERIC_ID: {
            const auto gen_id = expr->expr.generic_id;
            OptId<ExecId> maybe_eid = handle_any_generic_id(fid, scope, gen_id.slice, gen_id.args);
            if (maybe_eid.empty()) {
                return {};
            }
            // happy path, canonicalize compt value
            const Exec& exec = context.exec(maybe_eid.as_id());

            maybe_value = exec.template try_as<ExecConst>();
            break;
        }
        case AST_EXPR_DIAGNOSTIC: {
            const auto maybe_eid = handle_diag_expr(fid, scope, expr);
            if (maybe_eid.empty()) {
                return {};
            }
            maybe_value = context.exec(maybe_eid.as_id()).template as<ExecConst>();
            break;
        }
        case AST_EXPR_TYPE:
        case AST_EXPR_BORROW:
        case AST_EXPR_STRUCT_MEMBER_INIT:
        case AST_EXPR_CLOSURE:
        case AST_EXPR_VARIANT_DECOMP:
        case AST_EXPR_BLOCK:
        case AST_EXPR_MATCH_BRANCH:
        case AST_EXPR_ELSE_MATCH_PATTERN:
        case AST_EXPR_INVALID:
            // not a valid compile-time expr
            context.emplace_diagnostic(
                Span(fid, context.ast(fid).buffer(), expr->first, expr->last),
                diag_code::cannot_resolve_at_compt, diag_type::error);
            return std::nullopt;
            break;
        }

        if (maybe_value.has_value()) {
            if (!into_builtin.has_value()) {
                return emplace_e(maybe_value.value());
            }

            auto maybe_converted = maybe_value.value().try_safe_convert_to(into_builtin.value());

            if (!maybe_converted.has_value()) {
                auto from_builtin = maybe_value.value().type_builtin();
                TypeId from = context.emplace_type(TypeBuiltin{.type = from_builtin},
                                                   Span::generated(), false);
                TypeId to = into_tid.has_value()
                                ? into_tid.as_id()
                                : context.emplace_type(TypeBuiltin{.type = into_builtin.value()},
                                                       Span::generated(), false);
                context.emplace_diagnostic(
                    Span(fid, context.ast(fid).buffer(), expr->first, expr->last),
                    diag_code::cannot_convert_value_of_type, diag_type::error,
                    DiagnosticTypeToType{.from = from, .to = to}, DiagnosticNoOtherInfo{});

                return OptId<ExecId>{};
            }

            assert(maybe_converted.value().matches_type(into_builtin.value()));
            return emplace_e(maybe_converted.value());
        }
        context.emplace_diagnostic(Span(fid, context.ast(fid).buffer(), expr->first, expr->last),
                                   diag_code::cannot_resolve_at_compt, diag_type::error);
        // as to not duplicate compt errors from bubbling up
        return std::nullopt;
    }

    /**
     * solve a struct's value at compile-time, this essentially attempts a canonicalization down
     * to a struct-init eexpression where each field is evaluatable at compile-time
     */
    [[nodiscard]] OptId<ExecId> solve_struct_or_union(FileId fid, ScopeId scope,
                                                      const ast_expr_t* expr,
                                                      OptId<TypeId> into_tid) {

        auto visit_def
            = [this](DefId did) { return context.def(def_visitor.visit_as_dependent(did)); };

        Span expr_span{context, fid, expr};

        OptId<ExecId> maybe_eid{};

        auto validate_lookup
            = [this, into_tid, expr_span, visit_def](OptId<DefId> maybe_did,
                                                     token_ptr_slice_t id_slice) -> OptId<ExecId> {
            if (!maybe_did.has_value()) {
                auto sid_slice = context.symbol_slice(id_slice);
                context.emplace_diagnostic(
                    expr_span, diag_code::use_of_undeclared_identifier, diag_type::error,
                    DiagnosticIdentifierAfterMessage{.sid_slice = sid_slice},
                    DiagnosticSubCode{.sub_code = diag_code::not_declared_in_this_scope});
                return std::nullopt;
            }
            // happy path, canonicalize compt value
            DefId did = maybe_did.as_id();
            const Def& def = visit_def(did);
            if (!def.holds<DefVariable>()) {
                context.emplace_diagnostic(expr_span, diag_code::not_a_compile_time_constant,
                                           diag_type::error);
                return std::nullopt;
            }
            const auto& def_variable = def.as<DefVariable>();
            if (!def.compt) {
                auto d0 = context.emplace_diagnostic(
                    expr_span, diag_code::cannot_init_with_non_compt_value, diag_type::error,
                    DiagnosticSubCode{.sub_code = diag_code::not_a_compile_time_constant});
                auto d1 = context.emplace_diagnostic(
                    def.span, diag_code::declared_here_without_compt, diag_type::note);
                context.link_diagnostic(d0, d1);
                return std::nullopt;
            }
            if (!def_variable.compt_value.has_value()) {
                // this means that this definition must have had an issue, and it was thus
                // already reported, so just return a nullopt
                return std::nullopt;
            }
            if (into_tid.has_value()
                && context.equivalent_type(context.try_decay(def_variable.type_id),
                                           into_tid.as_id())) {
                // since it's compt (immutable, it's perfectly fine to just share the
                // original exec value here)
                return def_variable.compt_value;
            }
            if (into_tid.has_value()) {
                context.emplace_diagnostic(
                    expr_span, diag_code::cannot_convert_value_of_type, diag_type::error,
                    DiagnosticTypeToType{.from = def_variable.type_id, .to = into_tid.as_id()},
                    DiagnosticNoOtherInfo{});
            }
            return std::nullopt;
        };

        switch (expr->type) {
        case AST_EXPR_ID: {
            const token_ptr_slice_t id_slice = expr->expr.id.slice;
            Span id_span{context, fid, id_slice};
            const auto maybe_did
                = context.look_up_scoped_variable(scope, context.symbol_slice(id_slice), id_span);
            return validate_lookup(maybe_did, id_slice);
        }
        case AST_EXPR_STRUCT_INIT: {

            auto id_slice = expr->expr.struct_init.id;
            OptId<DefId> maybe_struct_did{};
            if (id_slice.len == 0) {
                if (into_tid.empty()
                    || TypeTransformer<TypeContainsVar>{context}(into_tid.as_id())) {
                    auto d0 = context.emplace_diagnostic(
                        expr_span, diag_code::cannot_infer_type_for_initializer, diag_type::error);
                    auto d1 = context.emplace_diagnostic(
                        expr_span,
                        diag_code::
                            explicitly_specify_the_type_by_providing_its_name_before_the_braces,
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
                    const auto maybe_generic_args = lower_generic_args(
                        fid, scope, expr->expr.struct_init.generic_args, false);
                    if (maybe_generic_args.empty()) {
                        return {}; // poisoned
                    }
                    maybe_did = context.look_up_scoped_type_generic(
                        def_visitor, scope, sid_slice, id_span, maybe_generic_args.as_id());
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
                auto maybe_union_def = context.try_union_def(did);
                if (maybe_union_def.has_value()) {
                    return handle_union_init(fid, scope, maybe_union_def.as_id(), expr);
                }

                maybe_struct_did = context.try_struct_def(did);

                if (maybe_struct_did.empty()) {
                    const Def& def = context.def(did);
                    // check for raw use of generic struct
                    if (def.generic) {
                        auto d0 = context.emplace_diagnostic_with_message_value(
                            Span{context, fid, id_slice}, diag_code::raw_use_of_generic_type,
                            diag_type::error,
                            DiagnosticIdentifierBeforeMessage{.sid_slice = sid_slice});
                        auto d1 = context.emplace_diagnostic(
                            def.span, diag_code::declared_here_as_generic, diag_type::note);
                        context.link_diagnostic(d0, d1);
                        return {};
                    }
                    auto d0 = context.emplace_diagnostic_with_message_value(
                        Span{context, fid, id_slice}, diag_code::is_not_a_struct, diag_type::error,
                        DiagnosticIdentifierBeforeMessage{.sid_slice = sid_slice});
                    auto d1 = context.emplace_diagnostic(def.span, diag_code::declared_here,
                                                         diag_type::note);
                    context.link_diagnostic(d0, d1);
                    return {};
                }
            }

            if (maybe_struct_did.empty()) {
                return {};
            }
            DefId struct_did = maybe_struct_did.as_id();
            maybe_eid = handle_struct_init(fid, scope, struct_did, expr, into_tid);
            if (maybe_eid.empty()) {
                return {}; // poisoned
            }
            break;
        }

        case AST_EXPR_TERNARY_IF: {
            return solve_ternary_if(fid, scope, expr, into_tid);
        }
        case AST_EXPR_COMPT:
            return solve_expr(fid, scope, expr->expr.compt_expr.inner, into_tid);
            // these are all invalid
        case AST_EXPR_FN_CALL: {
            maybe_eid = solve_fn_call(fid, scope, expr);
            break;
        }
        case AST_EXPR_SUBSCRIPT:
            maybe_eid = solve_expr_subscript(fid, scope, expr);
            break;
        case AST_EXPR_BINARY:
            maybe_eid = solve_expr_binary(fid, scope, expr);
            break;
        case AST_EXPR_GROUPING:
            maybe_eid = solve_expr(fid, scope, expr);
            break;
        case AST_EXPR_MATCH:
            maybe_eid = handle_match(fid, scope, expr);
            break;
        case AST_EXPR_GENERIC_ID: {
            const token_ptr_slice_t id_slice = expr->expr.generic_id.slice;
            Span id_span{context, fid, id_slice};
            const OptId<GenericArgIdSliceId> maybe_args = lower_generic_args(
                fid, scope, expr->expr.generic_id.args, false); // don't need layour info
            if (maybe_args.empty()) {
                return {};
            }
            const auto maybe_did = context.look_up_scoped_variable_generic(
                def_visitor, scope, context.symbol_slice(id_slice), id_span, maybe_args.as_id());
            return validate_lookup(maybe_did, id_slice);
        }
        case AST_EXPR_SAME_TYPE:
        case AST_EXPR_HAS_CONTRACT:
        case AST_EXPR_DEFINED:
        case AST_EXPR_TYPE_TO_STR:
        case AST_EXPR_STATIC_ASSERT:
        case AST_EXPR_LITERAL:
        case AST_EXPR_LIST_LITERAL:
        case AST_EXPR_PRE_UNARY:
        case AST_EXPR_POST_UNARY:
        case AST_EXPR_TYPE:
        case AST_EXPR_BORROW:
        case AST_EXPR_STRUCT_MEMBER_INIT:
        case AST_EXPR_CLOSURE:
        case AST_EXPR_VARIANT_DECOMP:
        case AST_EXPR_BLOCK:
        case AST_EXPR_MATCH_BRANCH:
        case AST_EXPR_ELSE_MATCH_PATTERN:
        case AST_EXPR_INFERABLE_AS:
        case AST_EXPR_DIAGNOSTIC:
        case AST_EXPR_INVALID:
            break;
        }
        // try type checking
        if (maybe_eid.has_value() && into_tid.has_value()) {
            ExecId eid = maybe_eid.as_id();
            const Type& into_type = context.type(into_tid.as_id());
            if (into_type.holds<TypeVar>()) {
                return eid;
            }
            if (const Exec& exec = context.exec(eid);
                exec.holds<ExecExprStructInit>() && into_type.holds<TypeStruct>()
                && (exec.as<ExecExprStructInit>().struct_def_id
                    == into_type.as<TypeStruct>().def_id)) {
                return eid;
            }
            OptId<TypeId> maybe_inferred_etid = infer_type_from_exec(eid);
            if (maybe_inferred_etid.has_value()) {
                auto inferred_tid = maybe_inferred_etid.as_id();
                context.emplace_diagnostic_with_message_value(
                    expr_span, diag_code::cannot_convert_value_of_type, diag_type::error,
                    DiagnosticTypeToType{.from = inferred_tid, .to = into_tid.as_id()});
                return {};
            }
        }
        return maybe_eid;
    }

  private:
    [[nodiscard]] OptId<ExecId> handle_union_init(FileId fid, ScopeId scope, DefId union_did,
                                                  const ast_expr_t* expr) {
        assert(context.def(union_did).template holds<DefUnion>());
        const auto member_dids = context.def(union_did).template as<DefUnion>().ordered_members;
        auto sid_slice = context.symbol_slice(expr->expr.struct_init.id);
        Span id_span{fid, context.ast(fid).buffer(), expr->expr.struct_init.id.start[0],
                     expr->expr.struct_init.id.start[expr->expr.id.slice.len - 1]};
        const ast_slice_of_exprs_t init_slice = expr->expr.struct_init.member_inits;
        if (init_slice.len > 1) {
            Span mem_span{context, fid, init_slice.start[0]->first,
                          init_slice.start[init_slice.len - 1]->last};
            auto d0 = context.emplace_diagnostic_with_message_value(
                Span{context, fid, expr}, diag_code::too_many_initializers_for_union,
                diag_type::error, DiagnosticIdentifierAfterMessage{.sid_slice = sid_slice});
            auto d1 = context.emplace_diagnostic(
                mem_span, diag_code::union_initializers_must_only_set_one_field, diag_type::note);
            context.link_diagnostic(d0, d1);
            return {};
        }
        if (init_slice.len < 1) {
            auto d0 = context.emplace_diagnostic_with_message_value(
                Span{context, fid, expr}, diag_code::too_few_inits_for_union, diag_type::error,
                DiagnosticIdentifierAfterMessage{.sid_slice = sid_slice});
            auto d1 = context.emplace_diagnostic(
                Span{context, fid, expr}, diag_code::union_initializers_must_only_set_one_field,
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
        if (!context.def(def_visitor.visit_as_transparent(matched_did))
                 .template holds<DefVariable>()) {
            return {}; // poisoned
        }
        if (member_init->expr.struct_member_init.assign_op->type == TOK_ASSIGN_MOVE) {
            context.emplace_diagnostic(
                Span{context, fid, member_init->expr.struct_member_init.assign_op},
                diag_code::compt_values_cannot_be_moved, diag_type::error);
        }
        TypeId needed_tid = context.def(matched_did).template as<DefVariable>().type_id;
        OptId<ExecId> maybe_val
            = solve_expr(fid, scope, member_init->expr.struct_member_init.value, needed_tid);
        if (maybe_val.empty()) {
            return {}; // poisoned
        }
        return context.emplace_compt_exec(
            ExecExprUnionInit{.member_init = maybe_val.as_id(),
                              .union_def_id = union_did,
                              .active_member_idx = context.def(matched_did).member_idx,
                              .move = false},
            Span{context, fid, expr});
    }
    [[nodiscard]] OptId<ExecId> handle_struct_init(FileId fid, ScopeId scope, DefId struct_did,
                                                   const ast_expr_t* expr, OptId<TypeId> into_tid) {
        const auto member_dids = context.ordered_defs_for(struct_did);
        auto sid_slice = context.symbol_slice(expr->expr.struct_init.id);
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
        for (auto i = 0u; i < member_dids.len(); i++) {
            auto didx = member_dids.get(i);
            const auto member_did = context.def_id(didx);
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
                    ExecExprStructMemberInit init{
                        .field_def = member_did, .value = default_val.as_id(), .move = false};
                    member_init_execs.emplace_back(
                        context.register_exec(context, init, Span::generated(), true));
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
            const token_t* assign_op = member_init_expr->expr.struct_member_init.id;

            if (assign_op->type == TOK_ASSIGN_MOVE) {
                context.emplace_diagnostic(Span(fid, context.ast(fid).buffer(), assign_op),
                                           diag_code::compt_values_cannot_be_moved,
                                           diag_type::error);
            }
            const ast_expr_t* proposed_val = member_init_expr->expr.struct_member_init.value;
            const Span proposed_member_span = Span(fid, context.ast(fid).buffer(),
                                                   member_init_expr->first, member_init_expr->last);

            const SymbolId true_name = member.name;
            if (context.symbol_id(proposed_member_name_tkn) != true_name) {
                cooked = true;
                context.emplace_diagnostic(
                    proposed_member_span, diag_code::field_initializer_does_not_match_field,
                    diag_type::error, DiagnosticSymbolAfterMessage{member.name},
                    DiagnosticNoOtherInfo{});
                continue;
            }
            OptId<ExecId> hopefully_exec = solve_expr(fid, scope, proposed_val, member_type);
            if (!hopefully_exec.has_value()) {
                cooked = true;
                // just continue, caused by other so must have already been reported
                continue;
            }
            // emplace the init execs
            member_init_execs.emplace_back(context.emplace_exec(
                ExecExprStructMemberInit{
                    .field_def = member_did, .value = hopefully_exec.as_id(), .move = false},
                proposed_member_span, true));
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
                DiagnosticIdentifierBeforeMessage{.sid_slice = sid_slice}, DiagnosticNoOtherInfo{});
            return std::nullopt;
        }
        // type check before returning here! but only if the into type isn't var!
        if (into_tid.has_value() && context.type(into_tid.as_id()).template holds<TypeStruct>()) {
            if (auto into_did = context.type(into_tid.as_id()).template as<TypeStruct>().def_id;
                struct_did != into_did) {
                context.emplace_diagnostic(
                    Span{context, fid, expr}, diag_code::cannot_convert_value_of_type,
                    diag_type::error,
                    DiagnosticTypeToType{.from
                                         = context.emplace_type(TypeStruct{.def_id = struct_did,
                                                                           .gen_args_slice = {},
                                                                           .generic = false},
                                                                Span{context, fid, expr}, false),
                                         .to = into_tid.as_id()},
                    DiagnosticNoOtherInfo{});
                return {};
            }
        }

        // all good, set the exec
        return context.emplace_exec(
            ExecExprStructInit{.member_inits = context.freeze_id_vec(member_init_execs),
                               .struct_def_id = struct_did},
            Span{context, fid, expr}, true);
    }
    [[nodiscard]] OptId<ExecId> solve_compt_cast(FileId fid, ScopeId scope, ExecId eid,
                                                 const ast_expr_t* into_expr) {
        if (into_expr->type != AST_EXPR_TYPE) {
            auto span = Span{fid, context.ast(fid).buffer(), into_expr->first, into_expr->last};
            auto d0 = context.emplace_diagnostic(span, diag_code::invalid_cast, diag_type::error);
            auto d1 = context.emplace_diagnostic(
                span, diag_code::parentheses_should_be_used_for_chained_casts, diag_type::note,
                DiagnosticNoOtherInfo{});
            context.link_diagnostic(d0, d1);
            return std::nullopt;
        }
        const ast_type_t* ast_type = into_expr->expr.type_expr.type;
        auto maybe_tid = resolve_type(fid, scope, ast_type);
        if (!maybe_tid.has_value()) {
            // error already report in type resolution, so just return none
            return std::nullopt;
        }
        TypeId tid = maybe_tid.as_id();
        return handle_cast(eid, tid);
    }

    [[nodiscard]] OptId<ExecId> handle_cast(ExecId eid, TypeId into_tid) {
        const Exec& exec = context.exec(eid);
        const Type& type = context.type(into_tid);
        if (!exec.holds<ExecConst>() || !type.holds<TypeBuiltin>()) {
            context.emplace_diagnostic_with_message_value(
                exec.span, diag_code::cannot_convert_expression_to_type, diag_type::error,
                DiagnosticTypeAfterMessage{.tid = into_tid});
            return std::nullopt;
        }
        auto into_builtin = type.as<TypeBuiltin>().type;
        auto from_constant = exec.as<ExecConst>();

        // allow string casts by converting to symbol id
        std::optional<ExecConst> maybe_converted
            = (into_builtin == builtin_type::str) ? ExecConst{from_constant.to_symbol_id(context)}
                                                  : from_constant.try_safe_convert_to(into_builtin);
        if (!maybe_converted.has_value()) {
            context.emplace_diagnostic_with_message_value(
                exec.span, diag_code::cannot_convert_expression_to_type, diag_type::error,
                DiagnosticTypeAfterMessage{.tid = into_tid});
            ExecConst eecc = exec.as<ExecConst>();
            context.emplace_diagnostic_with_message_value(
                exec.span, diag_code::guaranteed_narrowing_of_compt_value, diag_type::note,
                DiagnosticSymbolAfterMessage(eecc.to_symbol_id(context)));
            return std::nullopt;
        }
        return context.emplace_exec(maybe_converted.value(), exec.span, /*compt*/ true);
    }

    [[nodiscard]] OptId<ExecId> solve_binary_compt_exec(ExecId lhs_eid, binary_op op,
                                                        ExecId rhs_eid) {
        const Exec& lhs_exec = context.exec(lhs_eid);
        const Exec& rhs_exec = context.exec(rhs_eid);

        DiagLinker dl{context};
        auto handle_invalid_operand = [this, &dl](ExecId eid) {
            dl.link(context.emplace_diagnostic(context.exec(eid).span,
                                               diag_code::invalid_operand_for_binary_expression,
                                               diag_type::error));
        };

        if (bin_op_is_eq_neq(op) && lhs_exec.holds_same<ExecExprListLiteral>(rhs_exec)) {
            return solve_list_eq(lhs_exec, rhs_exec, op);
        }

        if (bin_op_is_eq_neq(op) && lhs_exec.holds_same<ExecExprStructInit>(rhs_exec)) {
            return solve_struct_eq(lhs_exec, rhs_exec, op);
        }

        if (bin_op_is_eq_neq(op) && lhs_exec.holds_same<ExecExprVariantInit>(rhs_exec)) {
            return solve_variant_eq(lhs_eid, rhs_eid, op);
        }

        bool cooked = false;
        if (!lhs_exec.holds<ExecConst>()) {
            handle_invalid_operand(lhs_eid);
            cooked = true;
        }
        if (!rhs_exec.holds<ExecConst>()) {
            handle_invalid_operand(rhs_eid);
            cooked = true;
        }
        if (cooked) {
            OptId<TypeId> maybe_lhs_tid = infer_type_from_exec(lhs_eid);
            OptId<TypeId> maybe_rhs_tid = infer_type_from_exec(rhs_eid);
            if (maybe_lhs_tid.has_value() && maybe_rhs_tid.has_value()) {
                const auto lhs_tid = maybe_lhs_tid.as_id();
                const auto rhs_tid = maybe_rhs_tid.as_id();
                dl.link(context.emplace_diagnostic_with_message_value(
                    lhs_exec.span, diag_code::value_is_of_type, diag_type::note,
                    DiagnosticTypeAfterMessage{.tid = lhs_tid}));
                dl.link(context.emplace_diagnostic_with_message_value(
                    rhs_exec.span, diag_code::value_is_of_type, diag_type::note,
                    DiagnosticTypeAfterMessage{.tid = rhs_tid}));
            }
            return std::nullopt;
        }
        switch (op) {
        case binary_op::plus:
        case binary_op::minus:
        case binary_op::multiply:
        case binary_op::divide:
        case binary_op::modulo:
        case binary_op::greater_than:
        case binary_op::less_than:
        case binary_op::greater_than_or_equal:
        case binary_op::less_than_or_equal:
        case binary_op::bool_equal:
        case binary_op::bool_not_equal:
            return handle_binary_scalar(lhs_exec, op, rhs_exec);
        case binary_op::bit_or:
        case binary_op::bit_and:
        case binary_op::bit_xor:
        case binary_op::left_bitshift:
        case binary_op::right_shift_logical:
        case binary_op::right_shift_arithmetic:
            return handle_binary_bitwise(lhs_exec, op, rhs_exec);
        case binary_op::bool_or:
        case binary_op::bool_and:
            return handle_binary_bool_conj_disj(lhs_exec, op, rhs_exec);
        }
        return std::nullopt;
    }

    [[nodiscard]] OptId<TypeId> resolve_type(FileId fid, ScopeId scope, const ast_type_t* type);

    [[nodiscard]] OptId<TypeId> resolve_type(FileId fid, ScopeId scope, const ast_type_t* type,
                                             bool need_layout_info);

    [[nodiscard]] bool guard_incompatible_types(const Exec& lhs, const Exec& rhs, ExecConst lhs_val,
                                                ExecConst rhs_val) {

        if (!lhs_val.holds_same_variant_type(rhs_val)) {
            auto lhs_tid = context.emplace_type(TypeBuiltin{.type = lhs_val.type_builtin()},
                                                lhs.span, false);
            auto rhs_tid = context.emplace_type(TypeBuiltin{.type = rhs_val.type_builtin()},
                                                rhs.span, false);
            context.emplace_diagnostic_with_message_value(
                Span::combine(lhs.span, rhs.span),
                diag_code::incompatible_types_for_binary_expression, diag_type::error,
                DiagnosticTypeAndType{.lhs_tid = lhs_tid, .rhs_tid = rhs_tid});
            return true;
        }
        return false;
    }

    [[nodiscard]] bool guard_op_not_viable_for_types(const Exec& lhs, const Exec& rhs,
                                                     ExecConst lhs_val, binary_op op,
                                                     ExecConst rhs_val) {
        if (!lhs_val.has_binary_op(op) || !rhs_val.has_binary_op(op)) {
            auto lhs_tid = context.emplace_type(TypeBuiltin{.type = lhs_val.type_builtin()},
                                                lhs.span, false);
            auto rhs_tid = context.emplace_type(TypeBuiltin{.type = rhs_val.type_builtin()},
                                                rhs.span, false);
            context.emplace_diagnostic_with_message_value(
                Span::combine(lhs.span, rhs.span),
                diag_code::incompatible_types_for_binary_operator, diag_type::error,
                DiagnosticTypeAndTypeForBinaryOp{.lhs_tid = lhs_tid, .rhs_tid = rhs_tid, .op = op});
            return true;
        }
        return false;
    }

    void guard_try_converge_types(ExecConst& lhs_val, binary_op op, ExecConst& rhs_val) {
        // try to safely convert (more ergonomic for literals and guranteed safe conversions)
        if (!lhs_val.holds_same_variant_type(rhs_val)) {
            const bool use_lhs_type = lhs_val.has_binary_op(op);
            const bool use_rhs_type = rhs_val.has_binary_op(op);
            // prefer lhs
            if (use_lhs_type) {
                auto maybe_converted_rhs = rhs_val.try_safe_convert_to(lhs_val.type_builtin());
                rhs_val = (maybe_converted_rhs.has_value()) ? maybe_converted_rhs.value() : rhs_val;
            } else if (use_rhs_type) {
                auto maybe_converted_lhs = lhs_val.try_safe_convert_to(rhs_val.type_builtin());
                lhs_val = (maybe_converted_lhs.has_value()) ? maybe_converted_lhs.value() : lhs_val;
            }
        }
    }

    [[nodiscard]] OptId<ExecId> handle_binary_scalar(const Exec& lhs, binary_op op,
                                                     const Exec& rhs) {

        auto lhs_val = lhs.as<ExecConst>();
        auto rhs_val = rhs.as<ExecConst>();

        // converge types, if possible
        guard_try_converge_types(/* & */ lhs_val, op, /* & */ rhs_val);

        if (guard_incompatible_types(lhs, rhs, lhs_val, rhs_val)) {
            return std::nullopt;
        }

        if (guard_op_not_viable_for_types(lhs, rhs, lhs_val, op, rhs_val)) {
            return std::nullopt;
        }

        std::optional<ExecConst> maybe_value{};

        auto guard_div_by_zero = [&]() -> bool {
            if (rhs_val.equals_zero()) {
                context.emplace_diagnostic(
                    rhs.span, diag_code::dividing_by_zero_at_compt_is_illegal, diag_type::error);
                return true;
            }
            return false;
        };

        switch (op) {
        case binary_op::plus:
            maybe_value = ExecConst::plus(context, lhs_val, rhs_val);
            break;
        case binary_op::minus:
            maybe_value = ExecConst::minus(lhs_val, rhs_val);
            break;
        case binary_op::multiply:
            maybe_value = ExecConst::multiply(lhs_val, rhs_val);
            break;

            // for div and mod guard div by zero so we don't crash the interpreter
        case binary_op::divide:
            maybe_value
                = (guard_div_by_zero()) ? std::nullopt : ExecConst::divide(lhs_val, rhs_val);
            break;
        case binary_op::modulo:
            maybe_value = guard_div_by_zero() ? std::nullopt : ExecConst::mod(lhs_val, rhs_val);
            break;
        case binary_op::greater_than:
            maybe_value = ExecConst::greater_than(lhs_val, rhs_val);
            break;
        case binary_op::less_than:
            maybe_value = ExecConst::less_than(lhs_val, rhs_val);
            break;
        case binary_op::greater_than_or_equal:
            maybe_value = ExecConst::greater_than_or_equal(lhs_val, rhs_val);
            break;
        case binary_op::less_than_or_equal:
            maybe_value = ExecConst::less_than_or_equal(lhs_val, rhs_val);
            break;
        case binary_op::bool_equal:
            maybe_value = ExecConst::equal(lhs_val, rhs_val);
            break;
        case binary_op::bool_not_equal:
            maybe_value = ExecConst::not_equal(lhs_val, rhs_val);
            break;
        default:
            std::unreachable();
            break;
        }
        if (!maybe_value.has_value()) {
            return std::nullopt;
        }
        // all good so exec up
        return context.emplace_exec(maybe_value.value(), Span::combine(lhs.span, rhs.span),
                                    /*compt*/ true);
    }

    [[nodiscard]] OptId<ExecId> handle_binary_bitwise(const Exec& lhs, binary_op op,
                                                      const Exec& rhs) {
        auto lhs_val = lhs.as<ExecConst>();
        auto rhs_val = rhs.as<ExecConst>();

        // converge types, if possible
        guard_try_converge_types(lhs_val, op, rhs_val);

        if (guard_incompatible_types(lhs, rhs, lhs_val, rhs_val)) {
            return std::nullopt;
        }

        // try to do these when possible
        if (op == binary_op::right_shift_logical) {
            auto maybe_converted = lhs_val.try_down_convert_to(builtin_type::u64);
            lhs_val = maybe_converted.has_value() ? maybe_converted.value() : lhs_val;
            guard_try_converge_types(lhs_val, op, rhs_val);
        } else if (op == binary_op::right_shift_arithmetic) {
            auto maybe_converted = lhs_val.try_down_convert_to(builtin_type::i64);
            lhs_val = maybe_converted.has_value() ? maybe_converted.value() : lhs_val;
            guard_try_converge_types(lhs_val, op, rhs_val);
        }

        if (guard_op_not_viable_for_types(lhs, rhs, lhs_val, op, rhs_val)) {
            return std::nullopt;
        }

        std::optional<ExecConst> maybe_value{};

        switch (op) {
        case binary_op::bit_or:
            maybe_value = ExecConst::bit_or(lhs_val, rhs_val);
            break;
        case binary_op::bit_and:
            maybe_value = ExecConst::bit_and(lhs_val, rhs_val);
            break;
        case binary_op::bit_xor:
            maybe_value = ExecConst::bit_xor(lhs_val, rhs_val);
            break;
        case binary_op::left_bitshift:
            maybe_value = ExecConst::bit_lsh(lhs_val, rhs_val);
            break;
        case binary_op::right_shift_logical: {
            maybe_value = ExecConst::bit_rshl(lhs_val, rhs_val);
            break;
        }
        case binary_op::right_shift_arithmetic: {
            maybe_value = ExecConst::bit_rsha(lhs_val, rhs_val);
            break;
        }
        default:
            std::unreachable();
            break;
        }

        if (maybe_value.has_value()) {

            return context.emplace_exec(maybe_value.value(), Span::combine(lhs.span, rhs.span),
                                        /*compt*/ true);
        }

        return std::nullopt;
    }

    [[nodiscard]] OptId<ExecId> handle_binary_bool_conj_disj(const Exec& lhs, binary_op op,
                                                             const Exec& rhs) {
        auto lhs_val = lhs.as<ExecConst>();
        auto rhs_val = rhs.as<ExecConst>();

        // converge types to BOOL, if possible

        auto maybe_converted_lhs = lhs_val.try_safe_convert_to(builtin_type::boolean);
        auto maybe_converted_rhs = rhs_val.try_safe_convert_to(builtin_type::boolean);

        auto do_no_bool = [this](const Exec& exec) {
            context.emplace_diagnostic_with_message_value(
                exec.span, diag_code::value_is_not_contextually_convertible_to, diag_type::error,
                DiagnosticTypeAfterMessage{
                    .tid = context.emplace_type(TypeBuiltin{.type = builtin_type::boolean},
                                                Span::generated(), false)});
        };

        // LHS cannot be bool
        if (!maybe_converted_lhs.has_value()) {
            do_no_bool(lhs);
        }

        // RHS cannot be bool
        if (!maybe_converted_rhs.has_value()) {
            do_no_bool(rhs);
        }

        // if either cannot be bool, return none (no need to report further)
        if (!maybe_converted_lhs.has_value() || !maybe_converted_rhs.has_value()) {
            return std::nullopt;
        }

        // update converted to booleys
        lhs_val = maybe_converted_lhs.value();
        rhs_val = maybe_converted_rhs.value();

        if (guard_incompatible_types(lhs, rhs, lhs_val, rhs_val)) {
            return std::nullopt;
        }

        if (guard_op_not_viable_for_types(lhs, rhs, lhs_val, op, rhs_val)) {
            return std::nullopt;
        }

        std::optional<ExecConst> maybe_value{};

        switch (op) {
        case hir::binary_op::bool_and:
            maybe_value = ExecConst::bool_and(lhs_val, rhs_val);
            break;
        case hir::binary_op::bool_or:
            maybe_value = ExecConst::bool_or(lhs_val, rhs_val);
            break;
        default:
            std::unreachable();
            break;
        }
        if (!maybe_value.has_value()) {
            return std::nullopt;
        }
        // all good so exec up
        return context.emplace_exec(maybe_value.value(), Span::combine(lhs.span, rhs.span),
                                    /*compt*/ true);
    }
    [[nodiscard]] OptId<ExecId> solve_preunary_exec(unary_op op, Span op_span, ExecId eid) {
        const Exec& inner_exec = context.exec(eid);

        if (!inner_exec.template holds<ExecConst>()) {
            return std::nullopt;
        }

        ExecConst inner_val = inner_exec.as<ExecConst>();

        // try make inner for !<expr> into a bool
        if (op == unary_op::bool_not) {
            auto maybe_converted = inner_val.try_down_convert_to(builtin_type::boolean);
            if (!maybe_converted.has_value()) {
                context.emplace_diagnostic_with_message_value(
                    inner_exec.span, diag_code::value_is_not_contextually_convertible_to,
                    diag_type::error,
                    DiagnosticTypeAfterMessage{
                        .tid = context.emplace_type(TypeBuiltin{.type = builtin_type::boolean},
                                                    Span::generated(), false)});
                return std::nullopt;
            }
            inner_val = maybe_converted.value();
        }

        std::optional<ExecConst> maybe_value{};

        switch (op) {
        case unary_op::plus:
            maybe_value = ExecConst::preunary_plus(inner_val);
            break;
        case unary_op::minus:
            maybe_value = ExecConst::preunary_minus(inner_val);
            break;
        case unary_op::bool_not:
            maybe_value = ExecConst::preunary_bool_not(inner_val);
            break;
        case unary_op::bit_not:
            maybe_value = ExecConst::preunary_bit_not(inner_val);
            break;

        // these aren't halal at compt
        case unary_op::inc:
        case unary_op::dec:
            break;
        }
        if (!maybe_value.has_value()) {
            return std::nullopt;
        }
        // all good so exec up
        return context.emplace_exec(maybe_value.value(), Span::combine(op_span, inner_exec.span),
                                    /*compt*/ true);
    }

    [[nodiscard]] OptId<ExecId> solve_ternary_if(FileId fid, ScopeId scope,
                                                 const ast_expr_t* tern_expr,
                                                 OptId<TypeId> maybe_into_tid) {
        const auto* happy_expr = tern_expr->expr.ternary_if.happy_expr;
        const auto* cond_expr = tern_expr->expr.ternary_if.condition;
        const auto* else_expr = tern_expr->expr.ternary_if.else_expr;
        auto maybe_cond_exec
            = solve_expr(fid, scope, cond_expr,
                         context.emplace_type(TypeBuiltin{.type = builtin_type::boolean},
                                              Span::generated(), false));

        if (maybe_cond_exec.empty()) {
            return std::nullopt;
        }

        auto cond_exec = maybe_cond_exec.as_id();

        auto maybe_cond_const = context.exec(cond_exec).template try_as<ExecConst>();

        if (!maybe_cond_const.has_value()) {
            return std::nullopt;
        }

        ExecConst cond_const = maybe_cond_const.value();

        auto maybe_cond_bool_val = cond_const.try_as<bool>();

        if (!maybe_cond_bool_val.has_value()) {
            return std::nullopt;
        }

        const bool cond_val = maybe_cond_bool_val.value();

        // happy
        if (cond_val) {
            return solve_expr(fid, scope, happy_expr, maybe_into_tid);
        }
        // else
        return solve_expr(fid, scope, else_expr, maybe_into_tid);
    }
    [[nodiscard]] OptId<ExecId> solve_list(FileId fid, ScopeId scope, const ast_expr_t* expr,
                                           OptId<TypeId> maybe_into_tid) {

        Span expr_span{fid, context.ast(fid).buffer(), expr->first, expr->last};

        auto guard_exec_type = [this, fid, expr, expr_span,
                                maybe_into_tid](OptId<ExecId> maybe_eid) -> OptId<ExecId> {
            if (maybe_eid.empty()) {
                return std::nullopt; // poisoned
            }
            const OptId<TypeId> maybe_tid = infer_type_from_exec(maybe_eid.as_id());
            if (maybe_tid.empty()) {
                return std::nullopt; // poisoned
            }
            const auto tid = maybe_tid.as_id();
            if (maybe_into_tid.has_value()) {
                if (context.equivalent_type(tid, maybe_into_tid.as_id())) {
                    return maybe_eid;
                }
                context.emplace_diagnostic_with_message_value(
                    Span{context, fid, expr}, diag_code::cannot_convert_value_of_type,
                    diag_type::error,
                    DiagnosticTypeToType{.from = tid, .to = maybe_into_tid.as_id()});
            } else {
                context.emplace_diagnostic(expr_span, diag_code::cannot_resolve_at_compt,
                                           diag_type::error);
            }
            return std::nullopt;
        };

        switch (expr->type) {
        case AST_EXPR_ID: {
            OptId<ExecId> maybe_eid = handle_any_id(fid, scope, expr->expr.id.slice);
            if (maybe_eid.empty()) {
                break;
            }
            auto eid = maybe_eid.as_id();
            auto exec = context.exec(eid);
            if (exec.template holds<ExecExprListLiteral>()) {
                return context.emplace_exec(exec.value, expr_span, true);
            }
            break;
        }
        case AST_EXPR_GENERIC_ID: {
            const auto gen_id = expr->expr.generic_id;
            OptId<ExecId> maybe_eid = handle_any_generic_id(fid, scope, gen_id.slice, gen_id.args);
            if (maybe_eid.empty()) {
                break;
            }
            auto eid = maybe_eid.as_id();
            auto exec = context.exec(eid);
            if (exec.template holds<ExecExprListLiteral>()) {
                return context.emplace_exec(exec.value, expr_span, true);
            }
            break;
        }
        case AST_EXPR_LIST_LITERAL:
            return handle_list_literal(fid, scope, expr, maybe_into_tid);
        case AST_EXPR_COMPT:
            return solve_expr(fid, scope, expr->expr.compt_expr.inner, maybe_into_tid);
        case AST_EXPR_SUBSCRIPT:
            return guard_exec_type(solve_expr_subscript(fid, scope, expr));
        case AST_EXPR_MATCH: {
            return guard_exec_type(handle_match(fid, scope, expr));
        }
        case AST_EXPR_BORROW:
        case AST_EXPR_LITERAL:
        case AST_EXPR_BINARY:
        case AST_EXPR_GROUPING:
        case AST_EXPR_PRE_UNARY:
        case AST_EXPR_POST_UNARY:
        case AST_EXPR_FN_CALL:
        case AST_EXPR_DEFINED:
        case AST_EXPR_TYPE:
        case AST_EXPR_STRUCT_INIT:
        case AST_EXPR_STRUCT_MEMBER_INIT:
        case AST_EXPR_CLOSURE:
        case AST_EXPR_TERNARY_IF:
        case AST_EXPR_VARIANT_DECOMP:
        case AST_EXPR_BLOCK:
        case AST_EXPR_MATCH_BRANCH:
        case AST_EXPR_ELSE_MATCH_PATTERN:
        case AST_EXPR_INVALID:
        case AST_EXPR_SAME_TYPE:
        case AST_EXPR_TYPE_TO_STR:
        case AST_EXPR_HAS_CONTRACT:
        case AST_EXPR_INFERABLE_AS:
        case AST_EXPR_DIAGNOSTIC:
        case AST_EXPR_STATIC_ASSERT:
            break;
        }
        context.emplace_diagnostic(expr_span, diag_code::cannot_resolve_at_compt, diag_type::error);
        return std::nullopt;
    }
    [[nodiscard]] OptId<ExecId> handle_list_literal(FileId fid, ScopeId scope,
                                                    const ast_expr_t* list_expr,
                                                    OptId<TypeId> maybe_into_type) {
        assert(list_expr->type == AST_EXPR_LIST_LITERAL);

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
            return context.emplace_exec(ExecExprListLiteral{.elems = IdSlice<ExecId>{},
                                                            .elem_type_id = maybe_elem_into_type,
                                                            .compt = true},
                                        whole_list_span, true);
        }

        llvm::SmallVector<ExecId> elem_execs{};

        for (HirSize i = 0; i < list_slice.len; ++i) {
            const ast_expr_t* expr = list_slice.start[i];
            OptId<ExecId> maybe_exec
                = solve_expr(fid, scope, expr,
                             maybe_elem_into_type); // this inner part runs at compt and thus
                                                    // will recursive check that sub exprs are
                                                    // compt and will error out when appropriate
            if (maybe_exec.empty()) {
                return std::nullopt; // poisoned
            }
            elem_execs.push_back(maybe_exec.as_id());
        }

        IdSlice<ExecId> elem_slice = context.freeze_id_vec(elem_execs);

        OptId<TypeId> maybe_first_type = infer_type_from_exec(context.exec_id(elem_slice.begin()));

        if (maybe_first_type.empty()) {
            return std::nullopt; // poisoned
        }

        // We will now type check this list literal for type-homogeneousness:

        const Exec& first_exec = context.exec(elem_slice.begin());
        TypeId type_for_list = maybe_first_type.as_id();

        bool homo_type = true;
        OptId<DiagnosticId> prev_diag{};

        for (IdIdx<ExecId> eidx = elem_slice.begin(); eidx != elem_slice.end(); eidx++) {
            ExecId eid = context.exec_id(eidx);
            OptId<TypeId> maybe_curr_type = infer_type_from_exec(eid);

            if (maybe_curr_type.empty()) {
                return std::nullopt; // poisoned
            }

            TypeId curr_type = maybe_curr_type.as_id();

            if (!context.equivalent_type(curr_type, type_for_list)) {

                const Exec& curr_exec = context.exec(eid);

                // check if it's own first hetero-rodeo, and, if so, emplace the once diagnostic
                // for the whole list before emplacing the per-exec diagnostics
                if (homo_type) {
                    prev_diag = context.emplace_diagnostic(
                        whole_list_span, diag_code::mismatched_types_in_list_literal,
                        diag_type::error);
                    // say type of first diagnostic this one time
                    auto curr_diag = context.emplace_diagnostic_with_message_value(
                        first_exec.span, diag_code::value_is_of_type, diag_type::note,
                        DiagnosticTypeAfterMessage{.tid = type_for_list});
                    context.link_diagnostic(prev_diag.as_id(), curr_diag);
                    prev_diag = curr_diag;
                }

                homo_type = false;

                // prev_diag will always be set by now
                auto curr_diag = prev_diag.as_id();

                auto next_diag = context.emplace_diagnostic_with_message_value(
                    curr_exec.span, diag_code::value_is_of_type, diag_type::note,
                    DiagnosticTypeAfterMessage{.tid = curr_type});
                context.link_diagnostic(curr_diag, next_diag);
                prev_diag = next_diag;
            }
        }

        // list isn't homogeneous, so it's invalid
        if (!homo_type) {
            return std::nullopt;
        }

        // fine, homogeneous, so return
        return context.emplace_exec(
            ExecExprListLiteral{.elems = elem_slice, .elem_type_id = type_for_list, .compt = true},
            whole_list_span, true);
    }

    [[nodiscard]] OptId<ExecId> handle_any_id(FileId fid, ScopeId scope,
                                              token_ptr_slice_t id_slice) {
        return handle_any_id(fid, scope, id_slice, OptId<GenericArgIdSliceId>{});
    }

    [[nodiscard]] OptId<ExecId> handle_any_generic_id(FileId fid, ScopeId scope,
                                                      token_ptr_slice_t id_slice,
                                                      ast_slice_of_generic_args_t gen_args) {
        const auto prior_diag_cnt = context.diagnostic_count();
        const OptId<GenericArgIdSliceId> maybe_gen_args
            = lower_generic_args(fid, scope, gen_args, false); // don't need layout info
        if (maybe_gen_args.empty()) {
            return {}; // issue with gen args so we're posioned
        }
        const auto res = handle_any_id(fid, scope, id_slice, maybe_gen_args.as_id());

        if (prior_diag_cnt < context.diagnostic_count()) {
            context.force_link_diagnostic(context.emplace_diagnostic(
                context.span_for_gen_args(maybe_gen_args.as_id()),
                diag_code::in_generic_instantiated_here, diag_type::note));
        }

        return res;
    }

    // tries to get the const value corresponding to some variable's name, if it exists
    [[nodiscard]] OptId<ExecId> handle_any_id(FileId fid, ScopeId scope, token_ptr_slice_t id_slice,
                                              OptId<GenericArgIdSliceId> maybe_gen_args) {
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
                                           diag_code::use_of_undeclared_identifier,
                                           diag_type::error);
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
        if (def.holds<DefVariable>() && def.as<DefVariable>().compt_value.has_value()
            && def.compt) {

            if (!def.compt) {
                auto d0 = context.emplace_diagnostic(
                    expr_span, diag_code::cannot_resolve_at_compt, diag_type::error,
                    DiagnosticSubCode{.sub_code = diag_code::not_a_compile_time_constant});
                auto d1 = context.emplace_diagnostic_with_message_value(
                    def.span, diag_code::declared_here_without_compt, diag_type::note,
                    DiagnosticIdentifierBeforeMessage{.sid_slice = sid_slice});
                context.link_diagnostic(d0, d1);
                return std::nullopt;
            }

            // right thing, we good
            // (make new exec w/ same val since we need to update the span loc!)
            auto orig_exec = context.exec(def.as<DefVariable>().compt_value.as_id());
            return context.emplace_exec(orig_exec.value, expr_span, true);
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
            return context.emplace_compt_exec(
                ExecExprVariantInit{.payload_init = payload,
                                    .variant_def_id = def.parent.as_id(),
                                    .active_member_idx = def.member_idx},
                expr_span);
        }
        auto d0 = context.emplace_diagnostic(
            expr_span, diag_code::cannot_resolve_at_compt, diag_type::error,
            DiagnosticSubCode{.sub_code = diag_code::not_a_compile_time_constant});
        auto d1 = context.emplace_diagnostic_with_message_value(
            def.span, diag_code::declared_here, diag_type::note,
            DiagnosticIdentifierBeforeMessage{.sid_slice = sid_slice});
        context.link_diagnostic(d0, d1);
        return std::nullopt;
    }
    [[nodiscard]] OptId<ExecId> handle_same_type(FileId fid, ScopeId scope,
                                                 const ast_expr_t* same_type_expr) {
        assert(same_type_expr->type == AST_EXPR_SAME_TYPE);
        auto maybe_lhs_tid = resolve_type(fid, scope, same_type_expr->expr.same_type.lhs_type);

        if (maybe_lhs_tid.empty()) {
            return std::nullopt;
        }

        auto lhs_tid = maybe_lhs_tid.as_id();

        auto maybe_rhs_tid = resolve_type(fid, scope, same_type_expr->expr.same_type.rhs_type);

        if (maybe_rhs_tid.empty()) {
            return std::nullopt;
        }

        auto rhs_tid = maybe_rhs_tid.as_id();

        const bool same_type_bool_val = context.equivalent_type(lhs_tid, rhs_tid);

        return context.emplace_exec(ExecExprComptConstant{same_type_bool_val},
                                    Span{context, fid, same_type_expr->first, same_type_expr->last},
                                    true);
    }
    [[nodiscard]] OptId<ExecId> handle_inferable_as(FileId fid, ScopeId scope,
                                                    const ast_expr_t* infas_expr) {
        assert(infas_expr->type == AST_EXPR_INFERABLE_AS);
        auto maybe_lhs_tid = resolve_type(fid, scope, infas_expr->expr.same_type.lhs_type);

        if (maybe_lhs_tid.empty()) {
            return std::nullopt;
        }

        auto lhs_tid = maybe_lhs_tid.as_id();

        auto maybe_rhs_tid = resolve_type(fid, scope, infas_expr->expr.same_type.rhs_type);

        if (maybe_rhs_tid.empty()) {
            return std::nullopt;
        }

        auto rhs_tid = maybe_rhs_tid.as_id();

        const bool infas_type_bool_val = context.type_inferable_as(lhs_tid, rhs_tid);

        return context.emplace_exec(ExecExprComptConstant{infas_type_bool_val},
                                    Span{context, fid, infas_expr->first, infas_expr->last}, true);
    }

    [[nodiscard]] OptId<ExecId> handle_type_to_str(FileId fid, ScopeId scope,
                                                   const ast_expr_t* tts_expr) {

        assert(tts_expr->type == AST_EXPR_TYPE_TO_STR);

        auto maybe_tid = resolve_type(fid, scope, tts_expr->expr.type_to_str.type);

        if (maybe_tid.empty()) {
            return std::nullopt;
        }

        auto tid = maybe_tid.as_id();

        SymbolId sid = context.symbol_id(type_to_string_as_mentioned(context, tid));

        return context.emplace_exec(ExecExprComptConstant{sid},
                                    Span{context, fid, tts_expr->first, tts_expr->last}, true);
    }
    [[nodiscard]] OptId<ExecId> handle_static_assert(FileId fid, ScopeId scope,
                                                     const ast_expr_t* sass_expr) {
        assert(sass_expr->type == AST_EXPR_STATIC_ASSERT);

        auto maybe_eid = solve_expr(fid, scope, sass_expr->expr.static_assert_expr.inner,
                                    context.emplace_type(TypeBuiltin{.type = builtin_type::boolean},
                                                         Span::generated(), false));

        if (maybe_eid.empty()) {
            return std::nullopt;
        }

        const Exec& inner_exec = context.exec(maybe_eid.as_id());

        if (!inner_exec.holds<ExecConst>() || !inner_exec.as<ExecConst>().holds<bool>()) {
            return std::nullopt; // poisoned
        }

        const auto bool_val = inner_exec.as<ExecConst>().as<bool>();

        if (!bool_val) {

            OptId<SymbolId> maybe_sid{};
            if (sass_expr->expr.static_assert_expr.maybe_string) {
                OptId<ExecId> maybe_string_eid = solve_builtin_compt_expr(
                    fid, scope, sass_expr->expr.static_assert_expr.maybe_string, builtin_type::str);
                if (maybe_string_eid.has_value()) {
                    maybe_sid = context.exec(maybe_string_eid.as_id())
                                    .template as<ExecConst>()
                                    .template as<SymbolId>();
                }
            }
            if (maybe_sid.has_value()) {
                context.emplace_diagnostic_with_message_value(
                    inner_exec.span, diag_code::static_assertion_failed_colon, diag_type::error,
                    DiagnosticSymbolAfterMessage{.sid = maybe_sid.as_id()});
            } else {
                context.emplace_diagnostic(
                    inner_exec.span, diag_code::static_assertion_failed, diag_type::error,
                    DiagnosticSubCode{.sub_code = diag_code::condition_is_false});
            }
        }

        return context.emplace_exec(ExecConst{bool_val}, Span{context, fid, sass_expr}, true);
    }
    [[nodiscard]] OptId<ExecId> handle_diag_expr(FileId fid, ScopeId scope,
                                                 const ast_expr_t* expr) {
        assert(expr->type == AST_EXPR_DIAGNOSTIC);

        OptId<ExecId> maybe_string_eid{};
        if (expr->expr.diagnostic.maybe_second_expr) {
            maybe_string_eid = solve_builtin_compt_expr(
                fid, scope, expr->expr.diagnostic.maybe_second_expr, builtin_type::str);
        } else {
            maybe_string_eid = solve_builtin_compt_expr(
                fid, scope, expr->expr.diagnostic.first_expr, builtin_type::str);
        }

        if (maybe_string_eid.empty()) {
            return {};
        }

        const auto str_exec = context.exec(maybe_string_eid.as_id());

        if (!str_exec.template holds<ExecConst>()
            || !str_exec.template as<ExecConst>().template holds<SymbolId>()) {
            return {}; // poisoned
        }

        SymbolId sid = str_exec.template as<ExecConst>().template as<SymbolId>();

        auto bool_val = false;

        OptId<ExecId> maybe_bool_eid{};
        if (expr->expr.diagnostic.maybe_second_expr) {
            maybe_bool_eid = solve_builtin_compt_expr(fid, scope, expr->expr.diagnostic.first_expr,
                                                      builtin_type::boolean);
            if (maybe_bool_eid.empty()) {
                return std::nullopt;
            }

            const Exec& inner_exec = context.exec(maybe_bool_eid.as_id());

            if (!inner_exec.holds<ExecConst>()
                || !inner_exec.template as<ExecConst>().template holds<bool>()) {
                return {}; // poisoned
            }

            bool_val = inner_exec.as<ExecConst>().as<bool>();
        } else {
            bool_val = true;
        }

        diag_type t = diag_type::error;
        switch (expr->expr.diagnostic.diag_type) {
        case DIAG_TYPE_ERROR:
            t = diag_type::error;
            break;
        case DIAG_TYPE_NOTE:
            t = diag_type::note;
            break;
        case DIAG_TYPE_WARNING:
            t = diag_type::warning;
            break;
        case DIAG_TYPE_HELP:
            t = diag_type::help;
            break;
        }

        if (bool_val) {
            context.try_link_custom_diagnostic(context.emplace_diagnostic(
                Span{context, fid, expr}, diag_code::only_message_value_is_meaningful, t,
                DiagnosticCustomComptDiag{.sid = sid}, DiagnosticInfoNoPreview{}));
        }

        return context.emplace_compt_exec(ExecConst{bool_val}, Span{context, fid, expr});
    }

    [[nodiscard]] OptId<ExecId> solve_expr_binary(FileId fid, ScopeId scope,
                                                  const ast_expr_t* expr) {
        assert(expr->type == AST_EXPR_BINARY);
        bool cooked = false;
        ComptBinaryOp maybe_bin_op{expr->expr.binary.op};
        if (maybe_bin_op.holds<InvalidOp>()) {
            cooked = true;
        } else if (maybe_bin_op.holds<assign_op>()) {
            auto d0 = context.emplace_diagnostic(Span(context, fid, expr->expr.binary.op),
                                                 diag_code::cannot_mutate_compt_const,
                                                 diag_type::error);
            OptId<ExecId> maybe_eid = solve_expr(fid, scope, expr->expr.binary.lhs);
            if (maybe_eid.has_value()) {
                auto d1 = context.emplace_diagnostic(Span{context, fid, expr->expr.binary.lhs},
                                                     diag_code::value_is_a_compile_time_constant,
                                                     diag_type::note);
                context.link_diagnostic(d0, d1);
            }
            cooked = true;
        } else if (maybe_bin_op.holds<is_as_op>()) {
            OptId<ExecId> lhs = solve_expr(fid, scope, expr->expr.binary.lhs,
                                           std::nullopt); // since we don't care about the type yet
            if (!lhs.has_value()) {
                cooked = true;
            }
            if (lhs.has_value()) {
                if (maybe_bin_op.as<is_as_op>() == is_as_op::is) {
                    return solve_is(fid, scope, lhs.as_id(), expr->expr.binary.rhs);
                }
                assert(maybe_bin_op.as<is_as_op>() == is_as_op::as);
                return solve_compt_cast(fid, scope, lhs.as_id(), expr->expr.binary.rhs);
            }
        } else if (maybe_bin_op.holds<access_op>()) {
            OptId<ExecId> maybe_lhs = solve_expr(fid, scope, expr->expr.binary.lhs, std::nullopt);
            if (!maybe_lhs.has_value()) {
                return std::nullopt; // poisoned
            }
            auto lhs_eid = maybe_lhs.as_id();
            const Exec& lhs_exec = context.exec(lhs_eid);

            if (maybe_bin_op.as<access_op>() == access_op::rarrow) {
                Span op_span{context, fid, expr->expr.binary.op};
                auto d0 = context.emplace_diagnostic(op_span, diag_code::invalid_binary_operator,
                                                     diag_type::error);
                auto d1 = context.emplace_diagnostic_with_message_value(
                    op_span, diag_code::replace_with, diag_type::help,
                    DiagnosticSymbolAfterMessage{.sid = context.symbol_id<".">()});

                auto d2 = context.emplace_diagnostic(
                    lhs_exec.span, diag_code::compt_expressions_do_not_have_addrs_so_no_ptrs,
                    diag_type::note,
                    DiagnosticSubCode{.sub_code = diag_code::value_is_not_a_pointer});
                context.link_diagnostic(d0, d1);
                context.link_diagnostic(d1, d2);
                return std::nullopt; // poisoned
            }

            const ast_expr_t* rhs = expr->expr.binary.rhs;

            auto matches_len_builtin = [this](token_ptr_slice_t id_slice) {
                return id_slice.len == 1
                       && context.symbol_id(id_slice.start[0]) == context.symbol_id<"len">();
            };

            if (lhs_exec.holds<ExecExprListLiteral>() && rhs->type == AST_EXPR_ID) {
                const auto id_slice = rhs->expr.id.slice;
                if (matches_len_builtin(id_slice)) {
                    return solve_list_len(lhs_exec, Span{context, fid, id_slice});
                }
            }

            if (lhs_exec.holds<ExecConst>() && lhs_exec.as<ExecConst>().holds<SymbolId>()
                && rhs->type == AST_EXPR_ID) {
                const auto id_slice = rhs->expr.id.slice;
                if (matches_len_builtin(id_slice)) {
                    return solve_str_len(lhs_exec, Span{context, fid, id_slice});
                }
            }

            // be helpful if we do this:
            // [1, 2, 3].len() => should be just `len`
            // "123123123".len() => should be just `len`
            if (((lhs_exec.holds<ExecExprListLiteral>())
                 || (lhs_exec.holds<ExecConst>() && lhs_exec.as<ExecConst>().holds<SymbolId>()))
                && rhs->type == AST_EXPR_FN_CALL
                && rhs->expr.fn_call.left_expr->type == AST_EXPR_ID) {
                const auto id_slice = rhs->expr.fn_call.left_expr->expr.id.slice;
                if (matches_len_builtin(id_slice)) {
                    const auto sid = context.symbol_id<"len">();
                    auto d0 = context.emplace_diagnostic_with_message_value(
                        Span{context, fid, rhs}, diag_code::tried_to_call_property,
                        diag_type::error, DiagnosticSymbolAfterMessage{.sid = sid});
                    auto d1 = context.emplace_diagnostic_with_message_value(
                        Span{context, fid, rhs}, diag_code::replace_with, diag_type::help,
                        DiagnosticSymbolAfterMessage{.sid = sid});
                    context.link_diagnostic(d0, d1);
                    return std::nullopt;
                }
            }

            const ast_expr_t* rhs_expr = expr->expr.binary.rhs;
            Span rhs_span{context, fid, rhs_expr};

            if (lhs_exec.holds<ExecExprUnionInit>()) {
                {
                    const Def& union_def = context.def(def_visitor.visit_as_transparent(
                        lhs_exec.as<ExecExprUnionInit>().union_def_id));
                    assert(union_def.holds<DefUnion>());
                }
                token_ptr_slice_t id_slice = rhs_expr->expr.id.slice;
                if (id_slice.len > 1) {
                    context.emplace_diagnostic(
                        rhs_span, diag_code::scoped_identifer_not_allowed_here, diag_type::error);
                }

                auto maybe_mem_var = context.look_up_variable(
                    context.def(lhs_exec.as<ExecExprUnionInit>().union_def_id)
                        .template as<DefUnion>()
                        .scope,
                    context.symbol_id(id_slice.start[0]));

                if (maybe_mem_var.empty()) {
                    const Def& union_def
                        = context.def(lhs_exec.as<ExecExprUnionInit>().union_def_id);
                    Span spn{context, fid, id_slice};
                    auto d0 = context.emplace_diagnostic_with_message_value(
                        spn, diag_code::does_not_name_a_field_of_union, diag_type::error,
                        DiagnosticSymbolAfterMessage{.sid = union_def.name});
                    auto d1 = context.emplace_diagnostic_with_message_value(
                        union_def.span, diag_code::declared_here, diag_type::note,
                        DiagnosticSymbolBeforeMessage{.sid = union_def.name});
                    context.link_diagnostic(d0, d1);
                    return {};
                }

                const Def& union_def = context.def(lhs_exec.as<ExecExprUnionInit>().union_def_id);
                const Def& var_def = context.def(maybe_mem_var.as_id());
                if (var_def.member_idx != lhs_exec.as<ExecExprUnionInit>().active_member_idx) {
                    const Def& curr_member
                        = context.def(union_def.as<DefUnion>().ordered_members.get(
                            lhs_exec.as<ExecExprUnionInit>().active_member_idx));
                    DiagLinker dlinker{context};
                    dlinker.link(context.emplace_diagnostic_with_message_value(
                        Span{context, fid, id_slice}, diag_code::compt_union_does_not_hold_field,
                        diag_type::error, DiagnosticSymbolAfterMessage{var_def.name}));
                    dlinker.link(context.emplace_diagnostic_with_message_value(
                        Span{context, fid, id_slice}, diag_code::compt_union_holds_field,
                        diag_type::note, DiagnosticSymbolAfterMessage{curr_member.name}));
                    dlinker.link(context.emplace_diagnostic_with_message_value(
                        union_def.span, diag_code::declared_here, diag_type::note,
                        DiagnosticSymbolBeforeMessage{.sid = union_def.name}));
                    return {};
                }

                const Exec& mem_exec = context.exec(lhs_exec.as<ExecExprUnionInit>().member_init);

                if (!exec_is_compt_viable(mem_exec)) {

                    context.emplace_diagnostic(Span{context, fid, expr},
                                               diag_code::cannot_resolve_at_compt,
                                               diag_type::error);
                    return std::nullopt;
                }
                return context.emplace_exec(mem_exec.value, Span{context, fid, expr}, true);
            }

            if (!lhs_exec.holds<ExecExprStructInit>()) {
                auto d0 = context.emplace_diagnostic(Span{context, fid, expr->expr.binary.op},
                                                     diag_code::cannot_access_a_member_value,
                                                     diag_type::error);
                if (const auto maybe_tid = infer_type_from_exec(lhs_eid); maybe_tid.has_value()) {
                    auto d1 = context.emplace_diagnostic(
                        lhs_exec.span, diag_code::value_is_of_type, diag_type::note,
                        DiagnosticTypeAfterMessage{.tid = maybe_tid.as_id()},
                        DiagnosticSubCode{.sub_code = diag_code::is_not_a_struct});
                    context.link_diagnostic(d0, d1);
                } else {
                    auto d1 = context.emplace_diagnostic(
                        lhs_exec.span, diag_code::value_not_a_struct, diag_type::note,
                        DiagnosticSubCode{.sub_code = diag_code::is_not_a_struct});

                    context.link_diagnostic(d0, d1);
                }
                return std::nullopt;
            }
            const Def& struct_def = context.def(lhs_exec.as<ExecExprStructInit>().struct_def_id);

            if (rhs_expr->type == AST_EXPR_ID) {
                assert(struct_def.holds<DefStruct>());
                token_ptr_slice_t id_slice = rhs_expr->expr.id.slice;
                if (id_slice.len > 1) {
                    context.emplace_diagnostic(
                        rhs_span, diag_code::scoped_identifer_not_allowed_here, diag_type::error);
                }

                auto maybe_mem_var = context.look_up_member_var_guarding_hid(
                    struct_def, context.symbol_id(id_slice.start[0]), rhs_span, scope);

                if (maybe_mem_var.empty()) {
                    return std::nullopt; // posioned
                }

                const Def& var_def = context.def(maybe_mem_var.as_id());

                const Exec& mem_exec = context.exec(
                    lhs_exec.as<ExecExprStructInit>().member_inits.get(var_def.member_idx));

                const Exec& mem_exec_val
                    = context.exec(mem_exec.as<ExecExprStructMemberInit>().value);

                if (!exec_is_compt_viable(mem_exec_val)) {

                    context.emplace_diagnostic(Span{context, fid, expr},
                                               diag_code::cannot_resolve_at_compt,
                                               diag_type::error);
                    return std::nullopt;
                }
                return context.emplace_exec(mem_exec_val.value, Span{context, fid, expr}, true);
            }
            if (rhs_expr->type == AST_EXPR_FN_CALL) {
                return solve_fn_call(fid, scope, rhs_expr, lhs_eid);
            }
            context.emplace_diagnostic(Span{context, fid, expr},
                                       diag_code::value_does_not_refer_to_a_named_mem,
                                       diag_type::error);
            return std::nullopt;

        } else if (!cooked && maybe_bin_op.holds<binary_op>()) {
            OptId<ExecId> lhs = solve_expr(fid, scope, expr->expr.binary.lhs, std::nullopt);
            if (!lhs.has_value()) {
                cooked = true;
            }
            // try short circuit
            const binary_op op = maybe_bin_op.as<binary_op>();
            if (lhs.has_value() && (op == binary_op::bool_or || op == binary_op::bool_and)) {
                const Exec& lhs_exec = context.exec(lhs.as_id());
                if (lhs_exec.holds<ExecConst>()) {
                    const std::optional<ExecConst> econst
                        = lhs_exec.as<ExecConst>().try_safe_convert_to(builtin_type::boolean);
                    if (econst.has_value()) {
                        const bool bval = econst.value().as<bool>();
                        // false && ____ => always false
                        if ((op == binary_op::bool_and) && !bval) {
                            return context.emplace_compt_exec(ExecConst{false},
                                                              Span{context, fid, expr});
                        }
                        // true || ____ => always true
                        if ((op == binary_op::bool_or) && bval) {
                            return context.emplace_compt_exec(ExecConst{true},
                                                              Span{context, fid, expr});
                        }
                    }
                }
            }

            OptId<ExecId> rhs = solve_expr(fid, scope, expr->expr.binary.rhs, std::nullopt);
            if (!rhs.has_value()) {
                cooked = true;
            }
            if (cooked) {
                return std::nullopt; // already poisoned
            }
            // get res of binary op like +, -, etc.
            return solve_binary_compt_exec(lhs.as_id(), op, rhs.as_id());
        }
        return std::nullopt; // cooked / poisoned already, so just return since error
                             // should've already been reported
    }
    [[nodiscard]] OptId<ExecId> handle_defined(FileId fid, ScopeId scope, const ast_expr_t* expr) {
        assert(expr->type == AST_EXPR_DEFINED);
        Span span{context, fid, expr};

        bool defined = false;
        token_ptr_slice_t id_slice = expr->expr.defined.id;
        defined = context.defined(scope, context.symbol_slice(id_slice), span,
                                  expr->expr.defined.member);

        return context.emplace_exec(ExecConst{defined}, span, true);
    }
    [[nodiscard]] bool exec_is_compt_viable(const Exec& exec) {
        return exec.holds_any_of<ExecConst, ExecExprStructInit, ExecExprListLiteral,
                                 ExecExprUnionInit, ExecExprVariantInit>();
    }
    [[nodiscard]] OptId<ExecId> solve_fn_call(FileId fid, ScopeId scope, const ast_expr_t* expr,
                                              OptId<ExecId> maybe_self_val = std::nullopt) {
        assert(expr->type == AST_EXPR_FN_CALL);
        llvm::SmallVector<ExecId> arg_vec{};
        DefFunction func;
        Span func_span = Span::generated();
        SymbolId func_symbol;
        uint8_t mt_param_adjustment = 0;
        OptId<DefId> maybe_func_did{};

        const auto prior_diag_cnt = context.diagnostic_count();

        if (maybe_self_val.has_value()) {
            const auto self_val = maybe_self_val.as_id();
            const Exec& exec = context.exec(self_val);
            assert(exec.holds<ExecExprStructInit>());
            arg_vec.push_back(self_val);
            const ast_expr_t* called = expr->expr.fn_call.left_expr;
            if (called->type != AST_EXPR_ID) {
                context.emplace_diagnostic(Span{context, fid, called},
                                           diag_code::cannot_resolve_at_compt, diag_type::error);
                return std::nullopt;
            }
            token_ptr_slice_t id_slice = called->expr.id.slice;
            if (id_slice.len > 1) {
                context.emplace_diagnostic(Span{context, fid, called},
                                           diag_code::scoped_identifer_not_allowed_here,
                                           diag_type::error);
            }
            const token_t* id_tok = id_slice.start[0];
            const SymbolId func_name = context.symbol_id(id_tok);
            const Span fn_name_span{context, fid, id_tok};

            if ((expr->expr.fn_call.is_generic)) {
                if (const auto maybe_gen_arg_id
                    = lower_generic_args(fid, scope, expr->expr.fn_call.generic_args, false);
                    maybe_gen_arg_id.has_value()) {
                    maybe_func_did = context.look_up_generic_member_function_guarding_hid(
                        def_visitor, context.def(exec.as<ExecExprStructInit>().struct_def_id),
                        func_name, func_span, scope, maybe_gen_arg_id.as_id());
                }
            } else {
                maybe_func_did = context.look_up_member_function_guarding_hid(
                    def_visitor, context.def(exec.as<ExecExprStructInit>().struct_def_id),
                    func_name, fn_name_span, scope);
            }
            if (maybe_func_did.empty()) {
                return std::nullopt;
            }
            auto func_did = maybe_func_did.as_id();
            const Def& func_def = context.def(func_did);
            assert(func_def.holds<DefFunction>());

            func = func_def.as<DefFunction>();

            mt_param_adjustment = 1;

            if (func.posioned) {
                return std::nullopt;
            }

            func_span = func_def.span;
            func_symbol = func_def.name;
        } else {
            const ast_expr_t* called = expr->expr.fn_call.left_expr;
            if (called->type != AST_EXPR_ID) {
                context.emplace_diagnostic(Span{context, fid, called},
                                           diag_code::cannot_resolve_at_compt, diag_type::error);
                return std::nullopt;
            }
            token_ptr_slice_t id_slice = called->expr.id.slice;

            const Span called_span{context, fid, id_slice};

            const auto sid_slice = context.symbol_slice(id_slice);

            const auto prior_diag_cnt = context.diagnostic_count();

            if (expr->expr.fn_call.is_generic) {
                if (const auto maybe_gen_arg_id
                    = lower_generic_args(fid, scope, expr->expr.fn_call.generic_args, false);
                    maybe_gen_arg_id.has_value()) {
                    maybe_func_did = context.look_up_scoped_variable_generic(
                        def_visitor, scope, sid_slice, called_span, maybe_gen_arg_id.as_id());
                }
            } else {
                maybe_func_did = context.look_up_scoped_variable(scope, sid_slice, called_span);
            }
            if (maybe_func_did.empty()) {

                // try as variant init, since they take the form of function calls but live in the
                // "type" namespace
                if (expr->expr.fn_call.is_generic) {
                    if (const auto maybe_gen_arg_id
                        = lower_generic_args(fid, scope, expr->expr.fn_call.generic_args, false);
                        maybe_gen_arg_id.has_value()) {
                        maybe_func_did = context.look_up_scoped_type_generic(
                            def_visitor, scope, sid_slice, called_span, maybe_gen_arg_id.as_id());
                    }
                } else {
                    maybe_func_did = context.look_up_scoped_type(scope, sid_slice, called_span);
                }

                if (maybe_func_did.has_value()) {
                    return handle_variant_init(fid, scope, expr, maybe_func_did.as_id());
                }

                // no corresponding variant either, so this is just the
                // use_of_undeclared_identifier
                if (prior_diag_cnt == context.diagnostic_count()) {
                    context.emplace_diagnostic(called_span, diag_code::use_of_undeclared_identifier,
                                               diag_type::error);
                }

                return std::nullopt;
            }

            maybe_func_did = context.try_func_did(maybe_func_did.as_id());

            const Def& called_def
                = context.def(def_visitor.visit_as_dependent(maybe_func_did.as_id()));
            if (!called_def.holds<DefFunction>()) {
                const auto code = (called_def.holds<DefGenericFunction>())
                                      ? diag_code::raw_use_of_generic_function
                                      : diag_code::not_a_function;
                context.emplace_diagnostic(called_span, diag_code::value_is_not_callable,
                                           diag_type::error, DiagnosticSubCode{.sub_code = code});
                return std::nullopt;
            }
            func = called_def.as<DefFunction>();
            func_span = called_def.span;
            func_symbol = called_def.name;
        }

        const auto func_did = maybe_func_did.as_id();

        const IdSlice<DefId> params = func.params;

        const ast_slice_of_exprs_t exprs = expr->expr.fn_call.args;

        HirSize total_arg_cnt = 0;
        bool issue = false;
        for (HirSize i = 0; i < exprs.len; i++) {
            const ast_expr_t* arg = exprs.start[i];

            const auto param_index = i + mt_param_adjustment;

            OptId<TypeId> maybe_into_tid = (param_index < params.len())
                                               ? OptId<TypeId>{context.def(params.get(param_index))
                                                                   .template as<DefVariable>()
                                                                   .type_id}
                                               : std::nullopt;

            OptId<ExecId> maybe_arg_eid = solve_expr(fid, scope, arg, maybe_into_tid);
            if (maybe_arg_eid.empty()) {
                issue = true;
            } else {
                arg_vec.push_back(maybe_arg_eid.as_id());
            }
            total_arg_cnt++;
        }

        auto adjusted_arg_len = total_arg_cnt;
        auto adjusted_param_len = params.len() - mt_param_adjustment;

        if (adjusted_arg_len != adjusted_param_len) {

            auto params_len_sym_id = ExecConst{adjusted_param_len}.to_symbol_id(context);
            auto args_len_sym_id = ExecConst{adjusted_arg_len}.to_symbol_id(context);

            Span span_of_interest
                = ((adjusted_arg_len < adjusted_param_len) || exprs.len == 0)
                      ? Span{context, fid, expr->last}
                      : Span::combine(Span{context, fid, exprs.start[0]->first},
                                      Span{context, fid, exprs.start[exprs.len - 1]->last});

            context.emplace_diagnostic_with_message_value(
                span_of_interest, diag_code::expected, diag_type::error,
                DiagnosticSymButGotSym{
                    .leading = func_symbol, .sid1 = params_len_sym_id, .sid2 = args_len_sym_id});

            issue = true;
        }

        const ast_stmt_t* fn_stmt = context.def_ast_node(func_did);
        assert(fn_stmt->type == AST_STMT_FN_DECL);

        if (!func.poisoned() && issue) {
            if (context.diagnostic_count() > prior_diag_cnt) {
                auto d = context.emplace_diagnostic_with_message_value(
                    {context, fid, fn_stmt->stmt.fn_decl.name}, diag_code::declared_here,
                    diag_type::note, DiagnosticSymbolBeforeMessage{.sid = func_symbol});
                context.force_link_diagnostic(d);
            }
        }

        // final guard before compt exec forward params -> execs
        if (issue || (arg_vec.size() != params.len())) {
            return std::nullopt; // just in case
        }

        if (context.def(func_did).template as<DefFunction>().posioned) {
            return std::nullopt; // already posioned so don't even try it
        }

        // mark that we're starting another call
        enter_compt_fn();
        if (call_depth > MAX_COMPT_CALL_FRAMES) {
            auto d0 = context.emplace_diagnostic_with_message_value(
                Span{context, fid, expr}, diag_code::only_message_value_is_meaningful,
                diag_type::error, DiagnosticComptStackOverflow{.function_sid = func_symbol});
            auto d1 = context.emplace_diagnostic_with_message_value(
                func_span, diag_code::declared_here, diag_type::note,
                DiagnosticSymbolBeforeMessage{.sid = func_symbol});

            context.link_diagnostic(d0, d1);

            // poison before exit to prevent cascading diags
            context.def(func_did).template as<DefFunction>().poison_infinite_recursion();

            exit_compt_fn();

            return std::nullopt;
        }

        const auto maybe_existing = context.try_scope_for_top_level_def(func_did);

        ScopeId temp_scope
            = maybe_existing.has_value()
                  ? context.make_compt_temp_scope(maybe_existing.as_id(), params.len())
                  : context.make_compt_temp_scope(context.containing_scope(func_did), params.len());

        for (HirSize i = 0; i < params.len(); i++) {
            const Def& param_def = context.def(params.get(i));
            assert(param_def.holds<DefVariable>());
            const DefVariable& param_var = param_def.as<DefVariable>();
            ExecId eid = arg_vec[i];
            const auto param = context.register_compt_def(
                param_def.name, param_def.span, func_did,
                DefVariable{.type_id = param_var.type_id, .compt_value = eid});
            context.insert_variable(temp_scope, context.def(params.get(i)).name, param);
        }

        if (!fn_stmt->stmt.fn_decl.only_expr) {
            auto d0 = context.emplace_diagnostic(
                Span{context, fid, expr}, diag_code::cannot_evaluate_non_pure_expr_fn_at_compt,
                diag_type::error);
            auto d1 = context.emplace_diagnostic_with_message_value(
                func_span, diag_code::declared_here, diag_type::note,
                DiagnosticSymbolBeforeMessage{.sid = func_symbol});
            context.link_diagnostic(d0, d1);
            if (context.def(func_did).compt) {
                auto d2 = context.emplace_diagnostic_with_message_value(
                    func_span, diag_code::declare_using_pure_expression_syntax_replacing_body_with,
                    diag_type::help,
                    DiagnosticSymbolAfterMessage{.sid = context.symbol_id<"=> (Expression)">()});
                context.link_diagnostic(d1, d2);
            }
            context.def(func_did).template as<DefFunction>().poison();
            exit_compt_fn();
            return std::nullopt;
        }

        const ast_expr_t* body_expr = fn_stmt->stmt.fn_decl.expr;

        // make sure to use the file_id for the function's expression
        OptId<ExecId> maybe_eid = solve_expr(context.def(func_did).span.file_id, temp_scope,
                                             body_expr, func.return_type);

        // try to get proper return type if possible
        if (maybe_eid.has_value() && func.return_type.has_value()) {
            const auto orig_eid = maybe_eid.as_id();
            maybe_eid = try_convert_to(maybe_eid.as_id(), func.return_type.as_id());
            if (maybe_eid.empty()) {
                if (const auto maybe_tid = infer_type_from_exec(orig_eid); maybe_tid.has_value()) {
                    context.emplace_diagnostic_with_message_value(
                        Span{context, fid, expr}, diag_code::cannot_convert_value_of_type,
                        diag_type::error,
                        DiagnosticTypeToType{.from = maybe_tid.as_id(),
                                             .to = func.return_type.as_id()});
                } else {
                    context.emplace_diagnostic_with_message_value(
                        Span{context, fid, expr}, diag_code::cannot_convert_expression_to_type,
                        diag_type::error,
                        DiagnosticTypeAfterMessage{.tid = func.return_type.as_id()});
                }
            }
        }

        // mark that we're done
        exit_compt_fn();

        const auto post_diag_cnt = context.diagnostic_count();
        if (!context.def(func_did).template as<DefFunction>().posioned
            && (maybe_eid.empty() || post_diag_cnt > prior_diag_cnt)) {
            const auto d = context.emplace_diagnostic_with_message_value(
                Span{context, fid, expr}, diag_code::called_here, diag_type::note,
                DiagnosticSymbolBeforeMessage{.sid = func_symbol});
            if (post_diag_cnt > prior_diag_cnt) {
                context.force_link_diagnostic(d);
            }
        }

        if (maybe_eid.empty()) {
            return {};
        }

        return context.emplace_exec(context.exec(maybe_eid.as_id()).value, Span{context, fid, expr},
                                    true);
    }

    [[nodiscard]] OptId<ExecId> try_convert_to(ExecId eid, TypeId tid) {
        OptId<TypeId> maybe_inferred_etid = infer_type_from_exec(eid);
        if (maybe_inferred_etid.has_value()
            && context.equivalent_type(maybe_inferred_etid.as_id(), tid)) {
            return eid;
        }

        const Exec& exec = context.exec(eid);

        if (!exec.holds<ExecConst>()) {
            return std::nullopt;
        }
        const Type& type = context.type(tid);
        if (!type.holds<TypeBuiltin>()) {
            return std::nullopt;
        }
        auto conv = exec.as<ExecConst>().try_safe_convert_to(type.as<TypeBuiltin>().type);
        if (!conv.has_value()) {
            return std::nullopt;
        }
        return context.emplace_exec(ExecConst{conv.value()}, exec.span, exec.compt);
    }
    [[nodiscard]] OptId<ExecId> solve_expr_borrow(FileId fid, ScopeId scope, const ast_expr_t* expr,
                                                  OptId<TypeId> maybe_into_tid) {
        assert(expr->type == AST_EXPR_BORROW);

        if (expr->expr.borrow.mut) {
            context.emplace_diagnostic(Span{context, fid, expr->expr.borrow.mut},
                                       diag_code::compt_values_cannot_be_mut_borrowed,
                                       diag_type::error);
        }

        return solve_expr(fid, scope, expr->expr.borrow.borrowed, maybe_into_tid);
    }

    [[nodiscard]] OptId<ExecId> solve_expr_subscript(FileId fid, ScopeId scope,
                                                     const ast_expr_t* expr) {
        assert(expr->type == AST_EXPR_SUBSCRIPT);

        OptId<ExecId> maybe_lhs_eid = solve_expr(fid, scope, expr->expr.subscript.lhs);

        if (maybe_lhs_eid.empty()) {
            return std::nullopt; // poisoned
        }

        const auto lhs_eid = maybe_lhs_eid.as_id();
        const Exec& ordered_exec = context.exec(maybe_lhs_eid.as_id());

        if (ordered_exec.holds<ExecExprListLiteral>()) {
            ExecExprListLiteral list = ordered_exec.as<ExecExprListLiteral>();
            OptId<ExecId> maybe_idx
                = solve_expr(fid, scope, expr->expr.subscript.subexpr,
                             context.emplace_type(TypeBuiltin{.type = builtin_type::u64},
                                                  Span::generated(), false));
            if (maybe_idx.empty()) {
                return std::nullopt; // poisoned
            }
            const auto idx_eid = maybe_idx.as_id();
            const Exec& idx_exec = context.exec(idx_eid);
            const usize idx = idx_exec.as<ExecConst>().as<usize>();

            if (idx >= list.len()) {
                context.emplace_diagnostic_with_message_value(
                    Span{context, fid, expr->expr.subscript.subexpr},
                    diag_code::only_message_value_is_meaningful, diag_type::error,
                    DiagnosticIdxOutOfBounds{.idx_sid = context.symbol_id(std::to_string(idx)),
                                             .length_sid
                                             = context.symbol_id(std::to_string(list.len()))});
                return std::nullopt;
            }
            const Exec& exec = context.exec(list.elems.get(idx));
            return context.emplace_exec(exec.value, Span{context, fid, expr}, true);
        }
        if (ordered_exec.holds<ExecConst>() && ordered_exec.as<ExecConst>().holds<SymbolId>()) {
            SymbolId sid = ordered_exec.as<ExecConst>().as<SymbolId>();
            OptId<ExecId> maybe_idx
                = solve_expr(fid, scope, expr->expr.subscript.subexpr,
                             context.emplace_type(TypeBuiltin{.type = builtin_type::u64},
                                                  Span::generated(), false));
            if (maybe_idx.empty()) {
                return std::nullopt; // poisoned
            }
            const auto idx_eid = maybe_idx.as_id();
            const Exec& idx_exec = context.exec(idx_eid);
            const usize idx = idx_exec.as<ExecConst>().as<usize>();

            const std::string_view sv = context.symbol(sid);

            if (idx >= sv.size()) {
                context.emplace_diagnostic_with_message_value(
                    Span{context, fid, expr->expr.subscript.subexpr},
                    diag_code::only_message_value_is_meaningful, diag_type::error,
                    DiagnosticIdxOutOfBounds{.idx_sid = context.symbol_id(std::to_string(idx)),
                                             .length_sid
                                             = context.symbol_id(std::to_string(sv.size()))});
                return std::nullopt;
            }
            return context.emplace_exec(ExecConst{sv.at(idx)}, Span{context, fid, expr}, true);
        }

        OptId<TypeId> maybe_tid = infer_type_from_exec(lhs_eid);
        if (maybe_tid.empty()) {
            return std::nullopt; // posioned
        }
        const auto tid = maybe_tid.as_id();
        context.emplace_diagnostic_with_message_value(Span{context, fid, expr->expr.subscript.lhs},
                                                      diag_code::remove, diag_type::error,
                                                      DiagnosticTypeAfterMessage{.tid = tid});
        return std::nullopt;
    }
    [[nodiscard]] OptId<ExecId> solve_list_len(const Exec& list_exec, Span len_span) {
        assert(list_exec.holds<ExecExprListLiteral>());
        return context.emplace_exec(ExecConst{list_exec.as<ExecExprListLiteral>().len()},
                                    Span::combine(list_exec.span, len_span), true);
    }
    [[nodiscard]] OptId<ExecId> solve_str_len(const Exec& str_exec, Span len_span) {
        assert(str_exec.holds<ExecConst>() && str_exec.as<ExecConst>().holds<SymbolId>());
        return context.emplace_exec(
            ExecConst{context.symbol(str_exec.as<ExecConst>().as<SymbolId>()).size()},
            Span::combine(str_exec.span, len_span), true);
    }
    // two Execs holding ExecExprListLiteral and a binary op holding bool_equal or
    // bool_not_equal should be passed
    [[nodiscard]] OptId<ExecId> solve_list_eq(const Exec& list1, const Exec& list2,
                                              binary_op eq_neq) {
        assert(list1.holds_same<ExecExprListLiteral>(list2));
        assert(bin_op_is_eq_neq(eq_neq));
        ExecExprListLiteral l1 = list1.as<ExecExprListLiteral>();
        ExecExprListLiteral l2 = list2.as<ExecExprListLiteral>();

        // decides true/false for == and != based on equality
        auto emplace_val_based_on_eq = [this, eq_neq, &list1, &list2](const bool eq) {
            const bool cond = (eq_neq == binary_op::bool_equal) ? eq : !eq;
            return context.emplace_compt_exec(ExecConst{cond},
                                              Span::combine(list1.span, list2.span));
        };

        if (l1.len() != l2.len()) {
            emplace_val_based_on_eq(false);
        }
        if ((l1.elem_type_id.has_value() && l2.elem_type_id.has_value())
            && !context.equivalent_type(l1.elem_type_id.as_id(), l2.elem_type_id.as_id())) {
            auto d0 = context.emplace_diagnostic(Span::combine(list1.span, list2.span),
                                                 diag_code::invalid_operand_for_binary_expression,
                                                 diag_type::error);
            auto d1 = context.emplace_diagnostic_with_message_value(
                list1.span, diag_code::value_is_a_compt_list_holding_type, diag_type::note,
                DiagnosticTypeAfterMessage{.tid = l1.elem_type_id.as_id()});
            auto d2 = context.emplace_diagnostic_with_message_value(
                list2.span, diag_code::value_is_a_compt_list_holding_type, diag_type::note,
                DiagnosticTypeAfterMessage{.tid = l2.elem_type_id.as_id()});
            context.link_diagnostic(d0, d1);
            context.link_diagnostic(d1, d2);
        }
        for (HirSize i = 0; i < l1.len(); i++) {
            const auto e1 = context.exec_id(l1.elems.get(i));
            const auto e2 = context.exec_id(l2.elems.get(i));
            const OptId<ExecId> eid = solve_binary_compt_exec(e1, binary_op::bool_equal, e2);
            if (eid.empty()) {
                return std::nullopt;
            }
            const Exec& exec = context.exec(eid.as_id());
            assert(exec.holds<ExecConst>() && exec.as<ExecConst>().holds<bool>());
            const bool eq = exec.as<ExecConst>().as<bool>();
            if (!eq) {
                return emplace_val_based_on_eq(false);
            }
        }
        // all checks passed, fine
        return emplace_val_based_on_eq(true);
    }

    [[nodiscard]] OptId<ExecId> solve_struct_eq(const Exec& list1, const Exec& list2,
                                                binary_op eq_neq) {
        assert(list1.holds_same<ExecExprStructInit>(list2));
        assert(bin_op_is_eq_neq(eq_neq));
        ExecExprStructInit s1 = list1.as<ExecExprStructInit>();
        ExecExprStructInit s2 = list2.as<ExecExprStructInit>();

        // decides true/false for == and != based on equality
        auto emplace_val_based_on_eq = [this, eq_neq, &list1, &list2](const bool eq) {
            const bool cond = (eq_neq == binary_op::bool_equal) ? eq : !eq;
            return context.emplace_compt_exec(ExecConst{cond},
                                              Span::combine(list1.span, list2.span));
        };

        if (s1.struct_def_id != s2.struct_def_id) {

            const auto gargs1
                = context.def(s1.struct_def_id).template as<DefStruct>().maybe_generic_args;
            auto t1 = context.emplace_type(TypeStruct{.def_id = s1.struct_def_id,
                                                      .gen_args_slice = gargs1,
                                                      .generic = gargs1.has_value()},
                                           Span::generated(), false);
            const auto gargs2
                = context.def(s2.struct_def_id).template as<DefStruct>().maybe_generic_args;
            auto t2 = context.emplace_type(TypeStruct{.def_id = s2.struct_def_id,
                                                      .gen_args_slice = gargs2,
                                                      .generic = gargs2.has_value()},
                                           Span::generated(), false);
            auto d0 = context.emplace_diagnostic(Span::combine(list1.span, list2.span),
                                                 diag_code::invalid_operand_for_binary_expression,
                                                 diag_type::error);
            auto d1 = context.emplace_diagnostic_with_message_value(
                list1.span, diag_code::value_is_of_type, diag_type::note,
                DiagnosticTypeAfterMessage{.tid = t1});
            auto d2 = context.emplace_diagnostic_with_message_value(
                list2.span, diag_code::value_is_of_type, diag_type::note,
                DiagnosticTypeAfterMessage{.tid = t2});
            context.link_diagnostic(d0, d1);
            context.link_diagnostic(d1, d2);
            return std::nullopt;
        }
        for (HirSize i = 0; i < s1.member_inits.len(); i++) {
            const auto e1 = context.exec(s1.member_inits.get(i))
                                .template as<ExecExprStructMemberInit>()
                                .value;
            const auto e2 = context.exec(s2.member_inits.get(i))
                                .template as<ExecExprStructMemberInit>()
                                .value;
            const OptId<ExecId> eid = solve_binary_compt_exec(e1, binary_op::bool_equal, e2);
            if (eid.empty()) {
                return std::nullopt;
            }
            const Exec& exec = context.exec(eid.as_id());
            assert(exec.holds<ExecConst>() && exec.as<ExecConst>().holds<bool>());
            const bool eq = exec.as<ExecConst>().as<bool>();
            if (!eq) {
                return emplace_val_based_on_eq(false);
            }
        }
        // all checks passed, fine
        return emplace_val_based_on_eq(true);
    }

    [[nodiscard]] OptId<ExecId> solve_variant_eq(ExecId variant_eid1, ExecId variant_eid2,
                                                 binary_op eq_neq) {
        const bool equiv = equivalent_exec(context, variant_eid1, variant_eid2);
        return context.emplace_compt_exec(
            ExecConst{(eq_neq == binary_op::bool_equal) ? equiv : !equiv},
            Span::combine(context.exec(variant_eid1).span, context.exec(variant_eid2).span));
    }

    [[nodiscard]] OptId<ExecId> solve_compt_closure(FileId fid, ScopeId scope,
                                                    const ast_expr_t* expr) {
        assert(expr->type == AST_EXPR_CLOSURE);
        // TODO
        return std::nullopt;
    }
    [[nodiscard]] OptId<ExecId> handle_has_contract(FileId fid, ScopeId scope,
                                                    const ast_expr_t* expr) {
        assert(expr->type == AST_EXPR_HAS_CONTRACT);
        Span span{context, fid, expr};

        const ast_expr_has_contract_t has_ctr = expr->expr.has_contract;
        OptId<TypeId> maybe_tid = resolve_type(fid, scope, has_ctr.type);

        if (maybe_tid.empty()) {
            return std::nullopt; // poisoned
        }

        const auto tid = maybe_tid.as_id();

        Span contract_id_span{context, fid, has_ctr.contract};
        OptId<DefId> maybe_did{};
        if (has_ctr.contract->type == AST_EXPR_ID) {
            maybe_did = context.look_up_scoped_type(
                scope, context.symbol_slice(has_ctr.contract->expr.id.slice), contract_id_span);
        } else if (has_ctr.contract->type == AST_EXPR_GENERIC_ID) {
            const auto maybe_generic_args
                = lower_generic_args(fid, scope, has_ctr.contract->expr.generic_id.args, false);
            if (maybe_generic_args.empty()) {
                return {}; // poisoned
            }
            maybe_did = context.look_up_scoped_type_generic(
                def_visitor, scope, context.symbol_slice(has_ctr.contract->expr.generic_id.slice),
                contract_id_span, maybe_generic_args.as_id());
        }

        if (maybe_did.empty()) {
            context.emplace_diagnostic(contract_id_span, diag_code::use_of_undeclared_identifier,
                                       diag_type::error,
                                       DiagnosticSubCode{.sub_code = diag_code::not_a_contract});
            return std::nullopt;
        }

        const auto did = maybe_did.as_id();
        const Def& def = context.def(did);
        if (!def.holds<DefContract>()) {
            auto d0 = context.emplace_diagnostic(
                contract_id_span, diag_code::invalid_contract, diag_type::error,
                DiagnosticSubCode{.sub_code = diag_code::not_a_contract});
            auto d1 = context.emplace_diagnostic_with_message_value(
                context.name_span_for_def(did), diag_code::declared_here, diag_type::note,
                DiagnosticSymbolBeforeMessage{.sid = def.name});
            context.link_diagnostic(d0, d1);
            return std::nullopt;
        }

        return context.emplace_exec(ExecConst{context.type_has_contract(tid, did)}, span, true);
    }

    [[nodiscard]] OptId<ExecId> handle_variant_init(FileId fid, ScopeId scope,
                                                    const ast_expr_t* fn_call_expr,
                                                    DefId variant_field_did) {
        assert(fn_call_expr->type == AST_EXPR_FN_CALL);
        const ast_expr_t* called_expr = fn_call_expr->expr.fn_call.left_expr;
        const Def& def = context.def(variant_field_did);
        if (!def.holds<DefVariantField>()) {
            context.emplace_diagnostic(Span{context, fid, called_expr},
                                       diag_code::value_is_not_callable, diag_type::error,
                                       DiagnosticSubCode{.sub_code = diag_code::not_a_function});
            return {};
        }
        DefVariantField var_field_def = def.as<DefVariantField>();
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
        llvm::SmallVector<ExecId> member_init_vec;
        for (size_t i = 0; i < args.len; i++) {
            const ast_expr_t* arg = args.start[i];
            TypeId tid
                = context.def(var_field_def.members.get(i)).template as<DefVariable>().type_id;
            OptId<ExecId> maybe_eid = solve_expr(fid, scope, arg, tid);
            if (maybe_eid.empty()) {
                cooked = true;
                continue; // poisoned
            }
            member_init_vec.push_back(maybe_eid.as_id());
        }
        if (cooked) {
            const Def& def = context.def(variant_field_did);
            context.force_link_diagnostic(context.emplace_diagnostic_with_message_value(
                def.span, diag_code::declared_here, diag_type::note,
                DiagnosticSymbolBeforeMessage{.sid = def.name}));
            return {};
        }
        const auto member_inits = context.freeze_id_vec(member_init_vec);
        const Span span{context, fid, fn_call_expr};
        ExecId field_init = context.emplace_compt_exec(
            ExecVariantFieldInit{.member_inits = member_inits,
                                 .variant_field_def_id = variant_field_did},
            span);

        return context.emplace_compt_exec(
            ExecExprVariantInit{.payload_init = field_init,
                                .variant_def_id = context.def(variant_field_did).parent.as_id(),
                                .active_member_idx = context.def(variant_field_did).member_idx},
            span);
    }

    [[nodiscard]] OptId<ExecId> solve_is(FileId fid, ScopeId scope, ExecId eid,
                                         const ast_expr_t* pattern_expr) {
        const Exec& exec = context.exec(eid);
        if (!exec.holds<ExecExprVariantInit>()) {
            context.emplace_diagnostic(exec.span, diag_code::cannot_use_is_for_non_variant_values,
                                       diag_type::error,
                                       DiagnosticSubCode{.sub_code = diag_code::not_a_variant});
            return {};
        }
        const ExecExprVariantInit var_init = exec.as<ExecExprVariantInit>();
        const auto ordered_variant_fields
            = context.def(var_init.variant_def_id).template as<DefVariant>().ordered_members;
        if (pattern_expr->type != AST_EXPR_VARIANT_DECOMP) {
            return {}; // poisoned
        }
        const auto sid_slice = context.symbol_slice(pattern_expr->expr.variant_decomp.id);
        scope = context.def(var_init.variant_def_id).as<DefVariant>().scope;
        const auto maybe_var_field
            = context.look_up_scoped_type(scope, sid_slice, Span{context, fid, pattern_expr});
        if (maybe_var_field.empty()) {
            context.emplace_diagnostic(
                Span{context, fid, pattern_expr},
                diag_code::identifer_does_not_name_a_valid_pattern, diag_type::error,
                DiagnosticSubCode{.sub_code = diag_code::not_a_variant_field});
            return {};
        }

        const auto hopefully_var_field = maybe_var_field.as_id();

        if (!context.def(hopefully_var_field).template holds<DefVariantField>()) {
            context.emplace_diagnostic(
                Span{context, fid, pattern_expr},
                diag_code::identifer_does_not_name_a_valid_pattern, diag_type::error,
                DiagnosticSubCode{.sub_code = diag_code::not_a_variant_field});
            return {};
        }

        if (pattern_expr->expr.variant_decomp.vars.len != 0) {
            Span span{context, fid, pattern_expr};
            const auto d0 = context.emplace_diagnostic(
                span, diag_code::variant_decomposition_not_allowed_here, diag_type::error);
            const auto d1 = context.emplace_diagnostic_with_message_value(
                span, diag_code::replace_with, diag_type::help,
                DiagnosticIdentifierAfterMessage{.sid_slice = sid_slice});
            context.link_diagnostic(d0, d1);
            // don't return here, since we can safely just move on here
        }

        if (!context.check_variant_field_has_parent(hopefully_var_field, var_init.variant_def_id,
                                                    Span{context, fid, pattern_expr})) {
            return {};
        }
        const auto active_member
            = context.def_id(ordered_variant_fields.get(var_init.active_member_idx));

        return context.emplace_compt_exec(
            ExecConst{active_member == hopefully_var_field},
            Span::combine(context.exec(eid).span, Span{context, fid, pattern_expr}));
    }
    [[nodiscard]] OptId<ExecId> handle_match(FileId fid, ScopeId scope,
                                             const ast_expr_t* match_expr) {
        assert(match_expr->type == AST_EXPR_MATCH);
        const auto maybe_matched_eid = solve_expr(fid, scope, match_expr->expr.match_expr.matched);
        if (maybe_matched_eid.empty()) {
            return {}; // poisoned
        }
        const auto matched_eid = maybe_matched_eid.as_id();

        const auto maybe_matched_tid = infer_type_from_exec(matched_eid);

        if (maybe_matched_tid.empty()) {
            return {}; // poisoned
        }

        const auto matched_tid = maybe_matched_tid.as_id();

        const Type& ty = context.type(matched_tid);

        bool valid_branches_and_exhaustive = false;
        OptId<ScopeId> maybe_pattern_scope{};
        if (ty.holds<TypeVariant>()) {
            const auto variant_did = ty.as<TypeVariant>().def_id;
            maybe_pattern_scope = context.def(variant_did).template as<DefVariant>().scope;
            valid_branches_and_exhaustive = valid_exhaustive_match_for_variant(
                *this, maybe_pattern_scope.as_id(), fid, variant_did, match_expr);
        } else {
            valid_branches_and_exhaustive
                = valid_exhaustive_match_for_non_variant(*this, scope, fid, match_expr);
        }

        if (!valid_branches_and_exhaustive) {
            return {};
        }

        const ast_slice_of_exprs_t branches = match_expr->expr.match_expr.branches;

        const ast_expr_t* else_val_expr = nullptr;

        for (size_t i = 0; i < branches.len; ++i) {

            const ast_expr_match_branch_t branch = branches.start[i]->expr.match_branch;

            const ast_expr_t* val_expr = branch.value;

            for (size_t k = 0; k < branch.patterns.len; ++k) {

                const ast_expr_t* pattern_expr = branch.patterns.start[k];

                if (pattern_expr->type == AST_EXPR_ELSE_MATCH_PATTERN) {
                    else_val_expr = val_expr;
                    continue;
                }

                if (check_for_multi_decomps_on_match_branch(context, fid, branch)) {
                    return {}; // malformed decomps on this branch
                }

                const ScopeId pattern_scope
                    = maybe_pattern_scope.has_value() ? maybe_pattern_scope.as_id() : scope;
                if (pattern_matches(fid, pattern_scope, pattern_expr, matched_eid)) {
                    const ScopeId branch_scope
                        = context.make_compt_temp_scope(scope, 8); // decently sized
                    try_variant_decomp(fid, pattern_scope, branch_scope, pattern_expr, matched_eid);
                    return solve_expr(fid, branch_scope, val_expr);
                }
            }
        }

        // fallback to else since no other pattern matched
        if (else_val_expr) {
            return solve_expr(fid, scope, else_val_expr);
        }

        // fallback, this should've already been handled by the branch validity check
        context.emplace_diagnostic(Span{context, fid, match_expr},
                                   diag_code::match_expression_is_not_exhaustive, diag_type::error);
        return {};
    }

    bool pattern_matches(FileId fid, ScopeId scope, const ast_expr_t* pattern_expr,
                         ExecId matched_eid) {
        const Exec& matched_exec = context.exec(matched_eid);

        if (matched_exec.holds<ExecExprVariantInit>()) {
            const auto active_field_idx = matched_exec.as<ExecExprVariantInit>().active_member_idx;
            const auto variant_field_def_idx
                = context.ordered_defs_for(matched_exec.as<ExecExprVariantInit>().variant_def_id)
                      .get(active_field_idx);
            const DefId variant_field_def_id = context.def_id(variant_field_def_idx);

            if (pattern_expr->type == AST_EXPR_ID
                || pattern_expr->type == AST_EXPR_VARIANT_DECOMP) {
                const Span span{context, fid, pattern_expr};
                const token_ptr_slice_t id_slice = (pattern_expr->type == AST_EXPR_ID)
                                                       ? pattern_expr->expr.id.slice
                                                       : pattern_expr->expr.variant_decomp.id;
                const auto maybe_did
                    = context.look_up_scoped_type(scope, context.symbol_slice(id_slice), span);
                if (maybe_did.empty()) {
                    context.emplace_diagnostic(
                        span, diag_code::use_of_undeclared_identifier, diag_type::error,
                        DiagnosticSubCode{.sub_code = diag_code::not_declared_in_this_scope});
                    return {};
                }
                return maybe_did.as_id() == variant_field_def_id;
            }
            return false; // (poison)
        }

        const auto maybe_pattern_val = solve_expr(fid, scope, pattern_expr);

        if (maybe_pattern_val.empty()) {
            return {};
        }

        // TODO consider giving diagnostics to warn that patterns will never match if types do not
        // match
        return equivalent_exec(context, maybe_pattern_val.as_id(), matched_eid);
    }

    /// returns true on variant decomp, else false
    bool try_variant_decomp(FileId fid, ScopeId pattern_scope, ScopeId branch_scope,
                            const ast_expr_t* pattern_expr, ExecId variant_eid) {
        if (pattern_expr->type != AST_EXPR_VARIANT_DECOMP) {
            return false;
        }

        const ast_expr_variant_decomp_t decomp = pattern_expr->expr.variant_decomp;

        Span decomp_span{context, fid, pattern_expr};
        OptId<DefId> maybe_variant_field_did = context.look_up_scoped_type(
            pattern_scope, context.symbol_slice(pattern_expr->expr.variant_decomp.id), decomp_span);

        if (maybe_variant_field_did.empty()) {
            return false;
        }

        const auto variant_field_did = maybe_variant_field_did.as_id();

        const Def& variant_field_def = context.def(variant_field_did);

        if (!variant_field_def.holds<DefVariantField>()) {
            return false;
        }

        const DefVariantField variant_field = variant_field_def.as<DefVariantField>();

        if (decomp.vars.len > variant_field.members.len()) {
            const auto d0 = context.emplace_diagnostic(
                decomp_span, diag_code::too_many_decompositions_for_variant, diag_type::error);
            const auto d1 = context.emplace_diagnostic(variant_field_def.span,
                                                       diag_code::declared_here, diag_type::note);

            context.link_diagnostic(d0, d1);

            return false;
        }

        for (size_t i = 0; i < decomp.vars.len; ++i) {
            ast_param_t* const decomped_var = decomp.vars.start[i];
            if (!decomped_var->valid) {
                continue;
            }

            const OptId<TypeId> maybe_tid = resolve_type(fid, branch_scope, decomped_var->type);
            if (maybe_tid.empty()) {
                continue;
            }
            const auto written_tid = maybe_tid.as_id();

            const SymbolId name = context.symbol_id(decomped_var->name);

            const DefVariable variants_field_variable
                = context.def(variant_field.members.get(i)).template as<DefVariable>();

            const TypeId needed_tid = variants_field_variable.type_id;

            const bool type_matches = context.type_inferable_as(written_tid, needed_tid);

            Span span{context, fid, decomped_var->first, decomped_var->last};

            if (!type_matches) {
                const auto d0 = context.emplace_diagnostic_with_message_value(
                    context.type(written_tid).span,
                    diag_code::cannot_match_type_for_decomposed_variant_from, diag_type::error,
                    DiagnosticTypeToType{.from = written_tid, .to = needed_tid});
                const auto member_did = context.def_id(variant_field.members.get(i));
                const auto d1 = context.emplace_diagnostic_with_message_value(
                    context.def(member_did).span, diag_code::declared_here, diag_type::note,
                    DiagnosticSymbolBeforeMessage{.sid = context.def(member_did).name});
                context.link_diagnostic(d0, d1);
                continue;
            }

            const auto members
                = context
                      .exec(
                          context.exec(variant_eid).template as<ExecExprVariantInit>().payload_init)
                      .template as<ExecVariantFieldInit>()
                      .member_inits;
            if (i < members.len()) {
                ExecId value = context.exec_id(members.get(i));

                const DefId did = context.register_compt_def(
                    name, span, {},
                    DefVariable{.type_id = needed_tid, .compt_value = value, .moved = false});

                context.insert_variable(branch_scope, name, did);
            }
        }

        return true;
    }
    [[nodiscard]] OptId<GenericArgId> lower_generic_arg(FileId fid, ScopeId scope,
                                                        const ast_generic_arg_t* gen_arg,
                                                        bool need_layout_info) {
        if (!gen_arg->valid) {
            return {};
        }
        switch (gen_arg->tag) {
        case AST_GENERIC_ARG_TYPE: {
            const ast_type_t* type = gen_arg->arg.type;
            // try to interpret as an expr if its benerficial
            if (type->tag == AST_TYPE_BASE && !type->type.base.mut
                && !token_is_builtin_type(type->type.base.id.start[0]->type)) {
                const auto id_slice = type->type.base.id;
                const auto sid_slice = context.symbol_slice(id_slice);
                const auto maybe_tid
                    = context.look_up_scoped_type(scope, sid_slice, Span{context, fid, id_slice});
                if (maybe_tid.empty()) {
                    ast_expr_t expr;
                    expr.type = AST_EXPR_ID;
                    expr.first = id_slice.start[0];
                    expr.last = id_slice.start[id_slice.len - 1];
                    expr.expr.id = ast_expr_id_t{.slice = id_slice};
                    const OptId<ExecId> maybe_eid = solve_expr(fid, scope, &expr);
                    if (maybe_eid.empty()) {
                        return {};
                    }
                    return context.emplace_generic_arg(maybe_eid.as_id());
                }
            }
            const OptId<TypeId> maybe_tid
                = resolve_type(fid, scope, gen_arg->arg.type, need_layout_info);
            if (maybe_tid.empty()) {
                return {};
            }
            return context.emplace_generic_arg(maybe_tid.as_id());
        }
        case AST_GENERIC_ARG_EXPR: {
            const ast_expr_t* expr = gen_arg->arg.expr;
            if (expr->type == AST_EXPR_ID) {
                const auto id_slice = expr->expr.id.slice;
                // only do this if a type actually exists for this identifer, otherwise treat it
                // as an expression
                if (token_is_builtin_type(expr->expr.id.slice.start[0]->type)
                    || context
                           .look_up_scoped_type(scope, context.symbol_slice(id_slice),
                                                Span{context, fid, id_slice})
                           .has_value()) {
                    ast_type_t type = {.type = {},
                                       .tag = AST_TYPE_BASE,
                                       .canonical_base = &type,
                                       .first = id_slice.start[0],
                                       .last = id_slice.start[id_slice.len - 1]};
                    type.type.base = {.id = id_slice, .mut = false};
                    const OptId<TypeId> maybe_tid
                        = resolve_type(fid, scope, &type, need_layout_info);
                    if (maybe_tid.empty()) {
                        return {};
                    }
                    return context.emplace_generic_arg(maybe_tid.as_id());
                }
            }
            const OptId<ExecId> maybe_eid = solve_expr(fid, scope, expr);
            if (maybe_eid.empty()) {
                return {};
            }
            return context.emplace_generic_arg(maybe_eid.as_id());
            break;
        }
        }
        return {};
    }

  public:
    [[nodiscard]] OptId<GenericArgIdSliceId>
    lower_generic_args(FileId fid, ScopeId scope, ast_slice_of_generic_args_t gen_args,
                       bool need_layout_info) {
        if (!gen_args.valid || gen_args.len == 0) {
            return {};
        }
        llvm::SmallVector<GenericArgId> gen_arg_ids{};

        bool cooked = false;
        for (size_t i = 0; i < gen_args.len; ++i) {
            const auto* gen_arg = gen_args.start[i];
            const auto maybe_gen_arg_id = lower_generic_arg(fid, scope, gen_arg, need_layout_info);
            if (maybe_gen_arg_id.empty()) {
                cooked = true;
                continue;
            }
            gen_arg_ids.push_back(maybe_gen_arg_id.as_id());
        }
        if (cooked) {
            return {};
        }

        return context.emplace_generic_arg_id_slice(context.freeze_id_vec(gen_arg_ids));
    }
};

static_assert(IsExprSolver<ComptExprSolver<TopLevelDefVisitor>>);

} // namespace hir
#endif
