//     /                              /
//    /                              /
//   /_____  _____  _____  _____    /  _____   _  _  _____
//  /     / /____  /____/ /____/   /  /____/  /\  / /  __
// /_____/ /____  /    / /   \    /  /    /  /  \/ /____/
// Copyright (C) 2025-2026 Zachary Mahan
// Licensed under the Apache License 2.0. See LICENSE for details.

#include "compiler/hir/run_time_solver.hpp"
#include "compiler/ast/expr.h"
#include "compiler/ast/stmt.h"
#include "compiler/hir/compt_expr_solver.hpp"
#include "compiler/hir/context.hpp"
#include "compiler/hir/def.hpp"
#include "compiler/hir/diagnostic.hpp"
#include "compiler/hir/exec.hpp"
#include "compiler/hir/exec_ops.hpp"
#include "compiler/hir/expr_solver_common.hpp"
#include "compiler/hir/indexing.hpp"
#include "compiler/hir/matching.hpp"
#include "compiler/hir/scope.hpp"
#include "compiler/hir/span.hpp"
#include "compiler/hir/type.hpp"
#include "compiler/hir/type_resolver.hpp"
#include "compiler/token.h"
#include <algorithm>
#include <bit>

namespace hir {

struct MoveExit {
    MoveMapId map;
    OptId<ExecId> body;
};

static void write_back_moves(Context& context, MoveMapId parent,
                             llvm::ArrayRef<MoveExit> children) {
    llvm::SmallVector<std::pair<DefId, llvm::SmallVector<ExecId>>> moves{};
    for (const MoveExit child : children) {
        if (child.body.has_value() && definitely_diverges(context, child.body.as_id())) {
            continue;
        }
        context.move_map(child.map).for_each_local_move([&](DefId did, MoveResult res) {
            auto* it = std::ranges::find(moves, did, &decltype(moves)::value_type::first);
            if (it == moves.end()) {
                it = &moves.emplace_back(did, llvm::SmallVector<ExecId>{});
            }
            const auto push_unique = [&it](ExecId eid) {
                if (std::ranges::find(it->second, eid) == it->second.end()) {
                    it->second.push_back(eid);
                }
            };
            if (res.holds<ExecId>()) {
                push_unique(res.as<ExecId>());
                return;
            }
            const IdSlice<ExecId> slice = context.exec_id_slice(res.as<ExecIdSliceId>());
            for (auto eidx : slice) {
                push_unique(context.exec_id(eidx));
            }
        });
    }
    for (const auto& [did, eids] : moves) {
        if (eids.size() == 1) {
            context.move_map(parent).insert(did, eids.front());
        } else {
            const auto esid = context.emplace_exec_id_slice(context.freeze_id_vec(eids));
            context.move_map(parent).insert(did, esid);
        }
    }
}

template <class F> static void for_each_move_eid(const Context& context, MoveResult res, F&& f) {
    if (res.holds<ExecId>()) {
        f(res.as<ExecId>());
        return;
    }
    if (res.holds<ExecIdSliceId>()) {
        for (const auto eidx : context.exec_id_slice(res.as<ExecIdSliceId>())) {
            f(context.exec_id(eidx));
        }
    }
}

static MoveMapId snapshot_moves(Context& context, MoveMapId from, MoveMapId until) {
    llvm::SmallVector<MoveExit> chain{};
    OptId<MoveMapId> curr = from;
    while (curr.has_value() && curr.as_id() != until) {
        chain.push_back(MoveExit{.map = curr.as_id(), .body = {}});
        curr = context.move_map(curr.as_id()).parent_map();
    }
    assert(curr.has_value());
    const MoveMapId snapshot = context.make_persistent_move_map(until);
    write_back_moves(context, snapshot, chain);
    return snapshot;
}

[[nodiscard]] OptId<ExecId> RuntimeSolver::solve_expr(FileId fid, ScopeId scope,
                                                      const ast_expr_t* expr, TypeId into_tid) {
    return solve_expr(fid, LexicalCtx{.scope = scope, .map = context.make_persistent_move_map({})},
                      expr, into_tid);
}

[[nodiscard]] OptId<ExecId> RuntimeSolver::solve_expr(FileId fid, ScopeId scope,
                                                      const ast_expr_t* expr) {
    return solve_expr(fid, LexicalCtx{.scope = scope, .map = context.make_persistent_move_map({})},
                      expr);
}

[[nodiscard]] OptId<ExecId> RuntimeSolver::solve_expr(FileId fid, LexicalCtx lctx,
                                                      const ast_expr_t* expr) {
    const auto maybe_eid = handle_any_typed_expr(fid, lctx, expr);
    if (maybe_eid.empty()) {
        return {};
    }
    return consume(lctx, maybe_eid.as_id());
}

[[nodiscard]] OptId<ExecId> RuntimeSolver::solve_place(FileId fid, LexicalCtx lctx,
                                                       const ast_expr_t* expr) {
    while (expr->type == AST_EXPR_GROUPING) {
        expr = expr->expr.grouping.expr;
    }
    return handle_any_typed_expr(fid, lctx, expr);
}

[[nodiscard]] OptId<ExecId> RuntimeSolver::consume(LexicalCtx lctx, ExecId eid) {
    if (!context.is_lvalue(eid)) {
        return eid;
    }
    const auto maybe_tid = infer_type_from_exec(eid);
    if (maybe_tid.empty() || context.type_is_copyable(maybe_tid.as_id())) {
        return eid;
    }
    const Exec& exec = context.exec(eid);
    const Span span = exec.span;
    const auto maybe_did = context.try_def_for_exec(eid);
    if (!exec.holds<ExecVariable>() && !exec.holds<ExecAssignment>()) {
        context.emplace_diagnostic_with_message_value(
            span,
            exec.holds<ExecDeref>()
                ? diag_code::cannot_move_out_of_a_dereferenced_value_of_non_copyable_type
                : diag_code::cannot_move_out_of_a_member_or_element_of_non_copyable_type,
            diag_type::error, DiagnosticTypeAfterMessage{.tid = maybe_tid.as_id()});
        return {};
    }
    const DefId did = maybe_did.as_id();
    const ExecId move_eid = context.emplace_exec(ExecMove{.moved_def = did, .exec = eid}, span);
    context.move_map(lctx.map).insert(did, move_eid);
    return move_eid;
}

[[nodiscard]] OptId<ExecId> RuntimeSolver::solve_expr(FileId fid, LexicalCtx lctx,
                                                      const ast_expr_t* expr,
                                                      OptId<TypeId> maybe_into_tid) {
    if (maybe_into_tid) {
        return solve_expr(fid, lctx, expr, maybe_into_tid.as_id());
    }
    return solve_expr(fid, lctx, expr);
}

[[nodiscard]] OptId<TypeId> RuntimeSolver::resolve_type(FileId fid, LexicalCtx lctx,
                                                        const ast_type_t* type) {
    return TypeResolver{context, def_visitor}.resolve_type(fid, lctx.scope, type);
}

[[nodiscard]] OptId<ExecId> RuntimeSolver::solve_expr(FileId fid, LexicalCtx lctx,
                                                      const ast_expr_t* expr, TypeId into_tid) {
    if (context.type(into_tid).holds<TypeVar>()) {
        return solve_expr(fid, lctx, expr);
    }

    OptId<ExecId> maybe_eid{};

    switch (expr->type) {
        // ------------------ these are purely compt eval'd ------------------
    case AST_EXPR_COMPT:
    case AST_EXPR_STATIC_ASSERT:
    case AST_EXPR_DEFINED:
    case AST_EXPR_HAS_CONTRACT:
    case AST_EXPR_INFERABLE_AS:
    case AST_EXPR_DIAGNOSTIC:
    case AST_EXPR_MEMBERS_OF:
    case AST_EXPR_STATICS_OF:
    case AST_EXPR_REFLECTED_ID:
    case AST_EXPR_REFLECTED_SCOPED_ID:
    case AST_EXPR_ALIGNOF:
    case AST_EXPR_SAME_TYPE:
    case AST_EXPR_SIZEOF:
    case AST_EXPR_TUDU:
    case AST_EXPR_TYPE_ID:
    case AST_EXPR_TYPE_TO_STR:
        return ComptExprSolver{def_visitor}.solve_expr(fid, lctx.scope, expr, into_tid);
        // ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
    case AST_EXPR_LITERAL:
        maybe_eid = handle_literal(fid, expr, into_tid); // tries conversion if possible internally
        break;
    case AST_EXPR_ID:
        maybe_eid = handle_any_id(fid, lctx, expr, true, into_tid);
        break;
    case AST_EXPR_GENERIC_ID:
        maybe_eid = handle_any_generic_id(fid, lctx, expr);
        break;
    case AST_EXPR_STRUCT_INIT:
        maybe_eid = handle_struct_or_union_init(fid, lctx, expr, into_tid);
        break;
    case AST_EXPR_LIST_LITERAL:
        maybe_eid = handle_list_literal(fid, lctx, expr, into_tid);
        break;
    case AST_EXPR_GROUPING:
        maybe_eid = handle_grouping(fid, lctx, expr, {});
        break;
    case AST_EXPR_TUPLE_INIT:
        maybe_eid = handle_tuple_init(fid, lctx, expr);
        break;
    case AST_EXPR_BORROW:
        maybe_eid = handle_borrow(fid, lctx, expr);
        break;
    case AST_EXPR_PRE_UNARY:
        maybe_eid = handle_preunary(fid, lctx, expr);
        break;
    case AST_EXPR_POST_UNARY:
        maybe_eid = handle_postunary(fid, lctx, expr);
        break;
    case AST_EXPR_ADDR_OF:
        maybe_eid = handle_addr_of(fid, lctx, expr);
        break;
    case AST_EXPR_BINARY:
        maybe_eid = handle_binary_expr(fid, lctx, expr);
        break;
    case AST_EXPR_SUBSCRIPT:
        maybe_eid = handle_subscript(fid, lctx, expr);
        break;
    case AST_EXPR_FN_CALL:
        maybe_eid = handle_fn_call(fid, lctx, expr, into_tid);
        break;
    case AST_EXPR_MATCH:
        maybe_eid = handle_match(fid, lctx, expr, into_tid);
        break;
    case AST_EXPR_TERNARY_IF:
        maybe_eid = handle_ternary_if(fid, lctx, expr, into_tid);
        break;
    case AST_EXPR_CLOSURE:
    case AST_EXPR_BLOCK:
        diag_not_yet_implemented(Span{context, fid, expr});
        break;
    case AST_EXPR_STRUCT_MEMBER_INIT:
    case AST_EXPR_VARIANT_DECOMP:
    case AST_EXPR_MATCH_BRANCH:
    case AST_EXPR_ELSE_MATCH_PATTERN:
    case AST_EXPR_TYPE:
    case AST_EXPR_INVALID:
        break;
    }

    if (maybe_eid.empty()) {
        return {};
    }

    // empty list literal is not inferable, so do this here
    if (const auto& exec = context.exec(maybe_eid.as_id());
        exec.holds<ExecListLiteral>() && !exec.as<ExecListLiteral>().len()
        && context.type(into_tid).holds_any_of<TypeSlice, TypeArr>()) {
        return maybe_eid;
    }

    if (context.exec(maybe_eid.as_id()).holds<ExecConst>()) {
        if (const auto maybe_conv = context.try_convert_to(maybe_eid.as_id(), into_tid)) {
            maybe_eid = maybe_conv;
        }
    }
    const auto maybe_tid = context.infer_type_from_exec(maybe_eid.as_id());
    if (maybe_tid.empty()) {
        return {};
    }
    if (!context.assignable_and_inferable_from_type_to(maybe_tid.as_id(), into_tid)) {
        context.emplace_diagnostic_with_message_value(
            Span{context, fid, expr}, diag_code::cannot_convert_value_of_type, diag_type::error,
            DiagnosticTypeToType{.from = maybe_tid.as_id(), .to = into_tid});
        const Type& into_ty = context.type(into_tid);

        // recommend a borrow
        if (into_ty.holds<TypeRef>() && into_ty.try_inner().has_value()
            && (context.is_lvalue(maybe_eid.as_id())
                || (context.exec(maybe_eid.as_id()).compt && !into_ty.mut))
            && context.assignable_and_inferable_from_type_to(maybe_tid.as_id(),
                                                             into_ty.try_inner().as_id())) {

            context.prev_diagnostic()->value = DiagnosticInfoValue{
                DiagnosticSubCode{.sub_code = diag_code::did_you_mean_to_borrow}};

            const Exec& exec = context.exec(maybe_eid.as_id());
            std::string replacement_str{'&'};
            if (into_ty.mut) {
                replacement_str += "mut ";
            }
            replacement_str += context.symbol(context.symbol_id(exec.span));

            context.force_link_diagnostic(context.emplace_diagnostic_with_message_value(
                exec.span, diag_code::replace_with, diag_type::help,
                DiagnosticSymbolAfterMessage{
                    .sid = context.symbol_id(replacement_str),
                }));
        }
        const Type& from_ty = context.type(maybe_tid.as_id());

        const Exec& exec = context.exec(maybe_eid.as_id());

        // recommend an address of
        if (into_ty.holds<TypePtr>() && into_ty.try_inner().has_value()
            && context.is_lvalue(maybe_eid.as_id())
            && context.assignable_and_inferable_from_type_to(maybe_tid.as_id(),
                                                             into_ty.try_inner().as_id())) {

            context.prev_diagnostic()->value = DiagnosticInfoValue{
                DiagnosticSubCode{.sub_code = diag_code::did_you_mean_to_take_the_address}};

            std::string replacement_str{'^'};
            replacement_str += context.symbol(context.symbol_id(exec.span));

            context.force_link_diagnostic(context.emplace_diagnostic_with_message_value(
                exec.span, diag_code::replace_with, diag_type::help,
                DiagnosticSymbolAfterMessage{
                    .sid = context.symbol_id(replacement_str),
                }));
        }
        // recommend a cast
        else if (from_ty.castable_to(into_ty) && !exec.compt) {
            context.prev_diagnostic()->value = DiagnosticInfoValue{
                DiagnosticSubCode{.sub_code = diag_code::did_you_mean_to_cast}};
            if (!exec.span.is_generated()) {
                std::string str{};
                if (exec.holds_any_of<ExecBinary, ExecAssignment, ExecCompoundAssignment,
                                      ExecTernaryIf>()) {
                    str += '(';
                }
                str += exec.span.as_sv(context);
                if (exec.holds_any_of<ExecBinary, ExecAssignment, ExecCompoundAssignment,
                                      ExecTernaryIf>()) {
                    str += ')';
                }
                str += " as ";
                if (into_ty.mut && !into_ty.holds<TypeRef>()) {
                    str += into_ty.holds<TypeBuiltin>()
                               ? type_to_string(context, context.type_for_builtin(
                                                             into_ty.as<TypeBuiltin>().type))
                               : type_to_string(context, context.emplace_type(into_ty.value,
                                                                              into_ty.span, false));
                } else {
                    str += type_to_string(context, into_tid);
                }
                context.force_link_diagnostic(context.emplace_diagnostic_with_message_value(
                    exec.span, diag_code::replace_with, diag_type::help,
                    DiagnosticSymbolAfterMessage{.sid = context.symbol_id(str)}));
            }
        }

        return {};
    }

    return consume(lctx, maybe_eid.as_id());
}

[[nodiscard]] OptId<ExecId> RuntimeSolver::solve_block_requiring_return(FileId fid, LexicalCtx lctx,
                                                                        ast_slice_of_stmts_t stmts,
                                                                        Span span) {
    return solve_block(fid, lctx, stmts, span, true);
}

[[nodiscard]] OptId<ExecId> RuntimeSolver::solve_block(FileId fid, LexicalCtx lctx,
                                                       const ast_stmt_t* stmt,
                                                       bool require_return) {
    assert(stmt->type == AST_STMT_BLOCK);
    return solve_block(fid, lctx, stmt->stmt.block.stmts, Span{context, fid, stmt}, require_return);
}

[[nodiscard]] OptId<GenericArgIdSliceId>
RuntimeSolver::lower_generic_args(FileId fid, ScopeId scope, ast_slice_of_generic_args_t gen_args,
                                  bool need_layout_info) {
    return ComptExprSolver{def_visitor}.lower_generic_args(fid, scope, gen_args, need_layout_info);
}

[[nodiscard]] OptId<ExecId> RuntimeSolver::solve_block(FileId fid, LexicalCtx parent_lctx,
                                                       ast_slice_of_stmts_t stmts, Span block_span,
                                                       bool require_return) {

    InProgressBlock in_prog_block{};

    LexicalCtx curr_lctx{.scope = context.make_small_scope(parent_lctx.scope, block_span),
                         .map = parent_lctx.map};

    bool hit_block_terminator{false};

    const auto must_return
        = [this, require_return]() { return this->return_tid.has_value() && require_return; };

    for (auto i = 0uz; i < stmts.len; ++i) {
        const auto maybe_eid = handle_stmt(fid, curr_lctx, in_prog_block, stmts.start[i]);

        // if this is the last statement (and we didn't already see a (premature) return) and we're
        // expecting a return type
        if (!hit_block_terminator && i == stmts.len - 1 && must_return()
            && (maybe_eid.empty()
                || (maybe_eid.has_value() && !definitely_returns(context, maybe_eid.as_id())))) {

            Span span{context, fid, stmts.start[i]};

            DiagLinker dl{context};
            dl.link(context.emplace_diagnostic(
                span, diag_code::function_may_not_return_a_value_in_all_control_flow_paths,
                diag_type::error));
            dl.link(context.emplace_diagnostic_with_message_value(
                context.type(this->return_tid.as_id()).span, diag_code::function_has_return_type,
                diag_type::note, DiagnosticTypeAfterMessage{.tid = this->return_tid.as_id()}));
            dl.link(context.emplace_diagnostic(
                span, diag_code::end_function_body_with_a_return_statement, diag_type::help));
        }

        if (maybe_eid.empty()) {
            continue;
        }

        // update this so we don't get annoying repetitive diagnostics (we don't want to see a dead
        // code warning AND that function_may_not_return_a_value_in_all_control_flow_paths
        // diagnostic nor do we want to see this more than once after the first premature return)
        if (!hit_block_terminator && maybe_eid.has_value()
            && (context.exec(maybe_eid.as_id()).holds_any_of<ExecReturn, ExecJump, ExecYield>()
                || (context.exec(maybe_eid.as_id()).holds<ExecLoop>()
                    && definitely_returns(context, maybe_eid.as_id())))) {

            hit_block_terminator = true;

            if (i < stmts.len - 1) {
                // span is all statements following the CURRENT return statement (that's why it's i
                // + 1, and this is safe since we know i<len-1)
                Span span{context, fid, stmts.start[i + 1]->first,
                          stmts.start[stmts.len - 1]->last};

                DiagLinker dl{context};
                dl.link(context.emplace_diagnostic(
                    span, diag_code::code_is_unreachable_following_a_block_terminating_statement,
                    diag_type::error));
                dl.link(context.emplace_diagnostic(Span{context, fid, stmts.start[i]},
                                                   diag_code::block_terminating_statement_here,
                                                   diag_type::note));
            }
        }
    }

    if (!stmts.len) {
        if (must_return()) {
            DiagLinker dl{context};
            dl.link(context.emplace_diagnostic(
                block_span, diag_code::function_may_not_return_a_value_in_all_control_flow_paths,
                diag_type::error));
            dl.link(context.emplace_diagnostic_with_message_value(
                context.type(this->return_tid.as_id()).span, diag_code::function_has_return_type,
                diag_type::note, DiagnosticTypeAfterMessage{.tid = this->return_tid.as_id()}));
            dl.link(context.emplace_diagnostic(
                block_span, diag_code::end_function_body_with_a_return_statement, diag_type::help));
        } else {
            context.emplace_diagnostic(block_span, diag_code::empty_block_does_nothing,
                                       diag_type::warning);
        }
    }

    return context.emplace_exec(
        ExecBlock{context.emplace_block(Block{.execs = context.freeze_id_vec(in_prog_block.execs),
                                              .defs = context.freeze_id_vec(in_prog_block.defs),
                                              .lctx = curr_lctx})},
        block_span);
}

OptId<ExecId> RuntimeSolver::handle_use(FileId fid, LexicalCtx lctx, const ast_stmt_t* stmt) {
    assert(stmt->type == AST_STMT_USE);
    def_visitor.resolve_use_stmt(fid, lctx.scope, stmt);
    return {};
}

void RuntimeSolver::ban_returning_ref_to_local(ExecId returnee_eid) {
    ExecId eid = returnee_eid;
    while (context.exec(eid).holds<ExecCast>()) {
        eid = context.exec(eid).as<ExecCast>().exec;
    }
    if (!context.exec(eid).holds<ExecBorrow>()) {
        return;
    }

    const ExecId borrowee_eid = context.exec(eid).as<ExecBorrow>().borrowee;
    // only flag borrows whose storage lives in the variable itself: walk members & array
    // subscripts down to the root variable
    //
    // - bail on derefs and slice subscripts since those point at storage owned
    // elsewhere
    for (ExecId curr = borrowee_eid;;) {
        const Exec& e = context.exec(curr);
        if (e.holds<ExecMemberAccess>()) {
            curr = e.as<ExecMemberAccess>().owner;
        } else if (e.holds<ExecSubscript>()) {
            curr = e.as<ExecSubscript>().base;
            const auto maybe_base_tid = context.infer_type_from_exec(curr);
            if (maybe_base_tid.empty() || !context.type(maybe_base_tid.as_id()).holds<TypeArr>()) {
                return;
            }
        } else if (e.holds<ExecVariable>()) {
            break;
        } else {
            return;
        }
    }

    const auto maybe_did = context.try_def_for_exec(borrowee_eid);
    if (maybe_did.empty()) {
        return;
    }
    const DefId did = maybe_did.as_id();
    auto* const it = std::lower_bound(locals.begin(), locals.end(), did,
                                      [](DefId a, DefId b) { return a.raw() < b.raw(); });
    const bool is_local = it != locals.end() && *it == did;
    bool is_param = false;
    for (const auto param_didx : params) {
        if (context.def_id(param_didx) == did) {
            is_param = true;
            break;
        }
    }
    if (!is_local && !is_param) {
        return;
    }

    DiagLinker dl{context};
    dl.link(context.emplace_diagnostic(context.exec(returnee_eid).span,
                                       diag_code::cannot_return_a_reference_to_a_local_variable,
                                       diag_type::error));
    const Def& def = context.def(did);
    dl.link(context.emplace_diagnostic_with_message_value(
        def.span, diag_code::declared_here, diag_type::note,
        DiagnosticSymbolBeforeMessage{.sid = def.name}));
}

OptId<ExecId> RuntimeSolver::handle_return(FileId fid, LexicalCtx lctx, InProgressBlock& block,
                                           const ast_stmt_t* stmt) {
    assert(stmt->type == AST_STMT_RETURN);

    if (this->return_tid.has_value()) {
        if (!stmt->stmt.return_stmt.expr) {
            DiagLinker dl{context};

            dl.link(context.emplace_diagnostic(Span{context, fid, stmt},
                                               diag_code::function_expected_return_value,
                                               diag_type::error));
            dl.link(context.emplace_diagnostic_with_message_value(
                context.type(this->return_tid.as_id()).span, diag_code::function_has_return_type,
                diag_type::note, DiagnosticTypeAfterMessage{.tid = this->return_tid.as_id()}));
        } else {
            const auto maybe_value_eid
                = solve_expr(fid, lctx, stmt->stmt.return_stmt.expr, this->return_tid);
            if (maybe_value_eid.has_value()) {
                ban_returning_ref_to_local(maybe_value_eid.as_id());
            }
            const auto eid = context.emplace_exec(ExecReturn{.return_value = maybe_value_eid},
                                                  Span{context, fid, stmt});
            block.push_back_exec(eid);
            return eid;
        }
    } else {
        if (stmt->stmt.return_stmt.expr) {
            const auto _ = solve_expr(fid, lctx, stmt->stmt.return_stmt.expr);
            DiagLinker dl{context};
            const ast_expr_t* expr = stmt->stmt.return_stmt.expr;
            dl.link(context.emplace_diagnostic(Span{context, fid, expr},
                                               diag_code::function_does_not_return_a_value,
                                               diag_type::error));
            Span span{context, fid, expr};
            dl.link(context.emplace_diagnostic_with_message_value(
                span, diag_code::remove, diag_type::help,
                DiagnosticSymbolAfterMessage{context.symbol_id(span)}));
        }
    }
    const auto eid = context.emplace_exec(ExecReturn{.return_value = {}}, Span{context, fid, stmt});
    block.push_back_exec(eid);
    return eid;
}

OptId<ExecId> RuntimeSolver::handle_stmt(FileId fid, LexicalCtx lctx, InProgressBlock& block,
                                         const ast_stmt_t* stmt, storage storage, compt compt,
                                         uint8_t align) {

    switch (stmt->type) {
    case AST_STMT_USE:
        return handle_use(fid, lctx, stmt);
    case AST_STMT_RETURN:
        return handle_return(fid, lctx, block, stmt);
    case AST_STMT_COMPT_MODIFIER:
        return handle_compt(fid, lctx, block, stmt, align);
    case AST_STMT_STATIC_MODIFIER:
        return handle_static(fid, lctx, block, stmt, storage, compt, align);
    case AST_STMT_ALIGNAS_MODIFIER:
        return handle_alignas(fid, lctx, block, stmt, storage, compt, align);
    case AST_STMT_VAR_DECL:
        return handle_var_decl(fid, lctx, block, stmt, storage, compt, align);
    case AST_STMT_VAR_INIT_DECL:
        return handle_var_init_decl(fid, lctx, block, stmt, storage, compt, align);
    case AST_STMT_DEFTYPE:
        return handle_deftype(fid, lctx, stmt);
    case AST_STMT_BLOCK:
        return handle_block(fid, lctx, block, stmt);
    case AST_STMT_EXPR:
        return handle_expr_stmt(fid, lctx, block, stmt);
    case AST_STMT_EMPTY:
        return {};
    case AST_STMT_BREAK:
        return handle_break(fid, lctx, block, stmt);
    case AST_STMT_CONTINUE:
        return handle_continue(fid, lctx, block, stmt);
    case AST_STMT_IF:
        return handle_if(fid, lctx, block, stmt);
    case AST_STMT_WHILE:
        return handle_while(fid, lctx, block, stmt);
    case AST_STMT_FOR:
        return handle_for(fid, lctx, block, stmt);
    case AST_STMT_YIELD:
        return handle_yield(fid, lctx, block, stmt);
    case AST_STMT_FOR_IN:
        return handle_for_in(fid, lctx, block, stmt);

        // poisoned/malformed:
    case AST_STMT_ELSE:
    case AST_STMT_VISIBILITY_MODIFIER:
    case AST_STMT_STRUCT_DEF:
    case AST_STMT_CONTRACT_DEF:
    case AST_STMT_UNION_DEF:
    case AST_STMT_VARIANT_DEF:
    case AST_STMT_VARIANT_FIELD_DECL:
    case AST_STMT_FN_DECL:
    case AST_STMT_EXTERN_BLOCK:
    case AST_STMT_FILE:
    case AST_STMT_MODULE:
    case AST_STMT_FN_PROTOTYPE:
    case AST_STMT_IMPORT:
    case AST_STMT_INVALID:
        break;
    }
    return {};
}

OptId<ExecId> RuntimeSolver::handle_compt(FileId fid, LexicalCtx lctx, InProgressBlock& block,
                                          const ast_stmt_t* stmt, uint8_t align) {
    assert(stmt->type == AST_STMT_COMPT_MODIFIER);
    // just check for this since duplicate compt is already handled
    return handle_stmt(fid, lctx, block, stmt->stmt.compt_modifier.stmt, storage::statik,
                       compt::compt, align);
}

OptId<ExecId> RuntimeSolver::handle_static(FileId fid, LexicalCtx lctx, InProgressBlock& block,
                                           const ast_stmt_t* stmt, storage storage, compt compt,
                                           uint8_t align) {
    assert(stmt->type == AST_STMT_STATIC_MODIFIER);
    if (storage == storage::statik) {
        Span span{context, fid, stmt->first};

        DiagLinker dl{context};

        dl.link(context.emplace_diagnostic(span, diag_code::redundant_static_qualifier,
                                           diag_type::warning));

        if (compt == compt::compt) {
            dl.link(context.emplace_diagnostic(span, diag_code::compt_vars_are_implicitly_static,
                                               diag_type::note, DiagnosticInfoNoPreview{}));
        }

        dl.link(context.emplace_diagnostic_with_message_value(
            span, diag_code::remove, diag_type::help,
            DiagnosticSymbolAfterMessage{.sid = context.symbol_id<"static">()}));
    }
    return handle_stmt(fid, lctx, block, stmt->stmt.compt_modifier.stmt, storage::statik, compt,
                       align);
}

OptId<ExecId> RuntimeSolver::handle_alignas(FileId fid, LexicalCtx lctx, InProgressBlock& block,
                                            const ast_stmt_t* stmt, storage storage, compt compt,
                                            uint8_t align) {
    assert(stmt->type == AST_STMT_ALIGNAS_MODIFIER);

    DiagLinker dl{context};

    Span span{context, fid, stmt->first};

    if (compt == compt::compt) {
        dl.link(context.emplace_diagnostic(span, diag_code::aligning_a_compt_variable_does_nothing,
                                           diag_type::warning));
    }
    if (align) {
        dl.link(context.emplace_diagnostic(span, diag_code::multiple_alignas_on_one_def,
                                           diag_type::error));
    }

    OptId<ExecId> maybe_align_eid = ComptExprSolver{def_visitor}.solve_builtin_compt_expr(
        fid, lctx.scope, stmt->stmt.alignaz.align_expr, builtin_type::u8);

    // just do this without aligning in order to minimize cascading errors
    const auto fail_align
        = [this, fid, lctx, &block, stmt, storage, compt, align] [[nodiscard]] () {
              return handle_stmt(fid, lctx, block, stmt->stmt.alignaz.inner, storage, compt, align);
          };

    if (maybe_align_eid.empty()) {
        return fail_align();
    }

    const Exec& exec = context.exec(maybe_align_eid.as_id());

    if (!exec.holds<ExecConst>()) {
        return fail_align();
    }

    if (exec.as<ExecConst>().holds<u8>()) {
        const auto val = exec.as<ExecConst>().as<u8>();
        if (!std::has_single_bit(val)) {
            dl.link(context.emplace_diagnostic(
                exec.span, diag_code::alignas_value_should_be_a_power_of_2, diag_type::error));
            return fail_align();
        }
        return handle_stmt(fid, lctx, block, stmt->stmt.alignaz.inner, storage, compt, val);
    }
    return fail_align();
}

OptId<ExecId> RuntimeSolver::handle_var_decl(FileId fid, LexicalCtx lctx, InProgressBlock& block,
                                             const ast_stmt_t* stmt, storage storage, compt compt,
                                             uint8_t align) {
    assert(stmt->type == AST_STMT_VAR_DECL);
    const SymbolId name = context.symbol_id(stmt->stmt.var_decl.name);
    const Span span{context, fid, stmt};

    OptId<TypeId> maybe_tid = TypeResolver{context, def_visitor}.resolve_type(
        fid, lctx.scope, stmt->stmt.var_decl.type);

    if (maybe_tid.empty()) {
        return {};
    }

    if (TypeTransformer<TypeContainsVar>{context}(maybe_tid.as_id())) {
        context.emplace_diagnostic_with_message_value(
            context.type(maybe_tid.as_id()).span,
            diag_code::should_have_explicit_type_since_it_has_no_initializer, diag_type::error,
            DiagnosticSymbolBeforeMessage{.sid = name});
    }

    OptId<ExecId> maybe_compt_eid{};
    OptId<ExecId> maybe_runtime_eid{};

    if (compt == compt::compt || storage == storage::statik) {
        maybe_compt_eid
            = context.try_default_value_for_type(maybe_tid.as_id(), span, /*compt=*/true);
    } else {
        maybe_runtime_eid
            = context.try_default_value_for_type(maybe_tid.as_id(), span, /*compt=*/false);
    }

    const DefId did = context.register_def(name, compt == compt::compt, storage == storage::statik,
                                           align, span, stmt,
                                           DefVariable{
                                               .type_id = maybe_tid.as_id(),
                                               .compt_value = maybe_compt_eid,
                                           },
                                           {});

    // record in scope
    context.insert_variable(lctx.scope, name, did);

    // always emplace the did
    block.push_back_def(did);

    if (storage != storage::statik) {
        locals.push_back(did);
    }

    if (maybe_runtime_eid.has_value()) {
        ExecId assign_eid
            = context.emplace_exec(ExecAssignment{.lhs = context.emplace_exec(
                                                      ExecVariable{
                                                          .def_id = did,
                                                          .type_id = maybe_tid.as_id(),
                                                      },
                                                      span),
                                                  .rhs = maybe_runtime_eid.as_id()},
                                   span);
        block.push_back_exec(assign_eid);
    }

    return maybe_runtime_eid;
}

OptId<ExecId> RuntimeSolver::handle_var_init_decl(FileId fid, LexicalCtx lctx,
                                                  InProgressBlock& block, const ast_stmt_t* stmt,
                                                  storage storage, compt compt, uint8_t align) {
    assert(stmt->type == AST_STMT_VAR_INIT_DECL);

    const SymbolId name = context.symbol_id(stmt->stmt.var_init_decl.name);
    const Span span{context, fid, stmt};

    OptId<TypeId> maybe_written_tid = TypeResolver{context, def_visitor}.resolve_type(
        fid, lctx.scope, stmt->stmt.var_init_decl.type);

    if (maybe_written_tid.empty()) {
        return {};
    }

    TypeId fall_back_tid_which_may_contain_var = maybe_written_tid.as_id();

    OptId<ExecId> maybe_compt_eid{};
    OptId<ExecId> maybe_runtime_eid{};

    if (compt == compt::compt || storage == storage::statik) {
        maybe_compt_eid = ComptExprSolver{def_visitor}.solve_expr(
            fid, lctx.scope, stmt->stmt.var_init_decl.rhs, maybe_written_tid);
        if (TypeTransformer<TypeContainsVar>{context}(maybe_written_tid.as_id())
            && maybe_compt_eid.has_value()) {
            maybe_written_tid = infer_type_from_exec(maybe_compt_eid.as_id());
        }
    } else {
        maybe_runtime_eid = solve_expr(fid, lctx, stmt->stmt.var_init_decl.rhs, maybe_written_tid);
        if (TypeTransformer<TypeContainsVar>{context}(maybe_written_tid.as_id())
            && maybe_runtime_eid.has_value()) {
            bool mut = context.type(maybe_written_tid.as_id()).mut;
            const auto maybe_inferred_tid = infer_type_from_exec(maybe_runtime_eid.as_id());
            if (maybe_inferred_tid.has_value()) {
                // this handles diagnostics, but continue onward regardless as to reduce cascades
                context.guard_non_assignable_inferred_type(
                    maybe_inferred_tid.as_id(), maybe_written_tid.as_id(),
                    context.exec(maybe_runtime_eid.as_id()).span);
                maybe_written_tid
                    = context.emplace_type(context.type(maybe_inferred_tid.as_id()).value,
                                           context.type(maybe_inferred_tid.as_id()).span, mut);
            }
        }
    }

    const DefId did = context.register_def(
        name, compt == compt::compt, storage == storage::statik, align, span, stmt,
        DefVariable{
            .type_id
            = maybe_written_tid ? maybe_written_tid.as_id() : fall_back_tid_which_may_contain_var,
            .compt_value = maybe_compt_eid,
        },
        {});

    // record in scope
    context.insert_variable(lctx.scope, name, did);

    // always emplace the did
    block.push_back_def(did);

    if (storage != storage::statik) {
        locals.push_back(did);
    }

    if (maybe_runtime_eid.has_value()) {
        ExecId assign_eid
            = context.emplace_exec(ExecAssignment{.lhs = context.emplace_exec(
                                                      ExecVariable{
                                                          .def_id = did,
                                                          .type_id = maybe_written_tid.as_id(),
                                                      },
                                                      span),
                                                  .rhs = maybe_runtime_eid.as_id()},
                                   span);
        block.push_back_exec(assign_eid);
    }

    return {};
}

OptId<ExecId> RuntimeSolver::handle_deftype(FileId fid, LexicalCtx lctx, const ast_stmt_t* stmt) {
    assert(stmt->type == AST_STMT_DEFTYPE);
    if (stmt->stmt.deftype.aliased_type_expr->type != AST_EXPR_TYPE) {
        return {}; // posioned
    }
    OptId<TypeId> maybe_tid = TypeResolver{context, def_visitor}.resolve_type(
        fid, lctx.scope, stmt->stmt.deftype.aliased_type_expr->expr.type_expr.type);
    if (!maybe_tid.has_value()) {
        return {}; // posioned
    }

    if (const Type& ty = context.type(maybe_tid.as_id()); ty.mut) {
        context.report_deftype_to_mut_type(maybe_tid.as_id());
    }

    SymbolId name = context.symbol_id(stmt->stmt.deftype.alias_id);

    const DefId did
        = context.register_def(name, /*compt=*/false, /*statik=*/false, 0, Span{context, fid, stmt},
                               stmt, DefDeftype{.type = maybe_tid.as_id()}, {});

    // shadowing is fine for local deftypes

    context.insert_type(lctx.scope, name, did);

    return {};
}

OptId<ExecId> RuntimeSolver::handle_block(FileId fid, LexicalCtx lctx, InProgressBlock& block,
                                          const ast_stmt_t* stmt) {
    assert(stmt->type == AST_STMT_BLOCK);
    const auto maybe_eid = solve_block(fid, lctx, stmt);
    if (maybe_eid.has_value()) {
        block.push_back_exec(maybe_eid.as_id());
    }
    return maybe_eid;
}

OptId<ExecId> RuntimeSolver::handle_expr_stmt(FileId fid, LexicalCtx lctx, InProgressBlock& block,
                                              const ast_stmt_t* stmt) {
    assert(stmt->type == AST_STMT_EXPR);
    const auto maybe_eid = solve_expr(fid, lctx, stmt->stmt.stmt_expr.expr);
    if (maybe_eid.empty()) {
        return {};
    }
    DiagLinker dl{context};
    // guard unused expr result
    if (!exec_result_is_used(context, maybe_eid.as_id())) {
        dl.link(context.emplace_diagnostic(
            context.exec(maybe_eid.as_id()).span, diag_code::result_of_expression_is_unused,
            diag_type::warning,
            DiagnosticSubCode{.sub_code = diag_code::expression_has_no_side_effects}));
    }
    // guard wrongly discarded values
    if (const Exec& exec = context.exec(maybe_eid.as_id()); exec.holds<ExecFnCall>()) {
        if (OptId<DefId> maybe_did = context.try_def_for_exec(exec.as<ExecFnCall>().callee)) {

            const Def& def = context.def(maybe_did.as_id());

            if ((def.holds<DefFunction>() && !def.as<DefFunction>().discardable
                 && def.as<DefFunction>().return_type)
                || (def.holds<DefFunctionPrototype>() && def.as<DefFunctionPrototype>().return_type
                    && !def.as<DefFunctionPrototype>().discardable)) {

                dl.link(context.emplace_diagnostic_with_message_value(
                    exec.span, diag_code::ignoring_return_value_of_non_discardable_function,
                    diag_type::error, DiagnosticSymbolAfterMessage{.sid = def.name}));
                dl.link(context.emplace_diagnostic_with_message_value(
                    context.function_header_span_for_def(maybe_did.as_id()),
                    diag_code::declared_here_as_non_discardable, diag_type::note,
                    DiagnosticSymbolBeforeMessage{.sid = def.name}));
            }
        }
    }
    block.push_back_exec(maybe_eid.as_id());
    return maybe_eid;
}

OptId<ExecId> RuntimeSolver::handle_break(FileId fid, LexicalCtx lctx, InProgressBlock& block,
                                          const ast_stmt_t* stmt) {
    assert(stmt->type == AST_STMT_BREAK);
    if (inside_loop_header) {
        context.emplace_diagnostic(Span{context, fid, stmt},
                                   diag_code::break_statement_inside_of_loop_header,
                                   diag_type::error);
        return {};
    }
    if (inside_compt_for) {
        context.emplace_diagnostic(Span{context, fid, stmt},
                                   diag_code::break_statement_inside_of_compt_for,
                                   diag_type::error);
        return {};
    }
    if (current_loop_block_eid.empty()) {
        context.emplace_diagnostic(Span{context, fid, stmt},
                                   diag_code::break_statement_outside_of_loop, diag_type::error);
        return {};
    }
    current_loop_has_break = true;
    current_loop_exit_maps->push_back(
        snapshot_moves(context, lctx.map, current_loop_base_map.as_id()));
    const auto eid = context.emplace_exec(
        ExecJump{.block = current_loop_block_eid.as_id(), .spot = jump_spot::end},
        Span{context, fid, stmt});
    block.push_back_exec(eid);
    return eid;
}

OptId<ExecId> RuntimeSolver::handle_continue(FileId fid, LexicalCtx lctx, InProgressBlock& block,
                                             const ast_stmt_t* stmt) {
    assert(stmt->type == AST_STMT_CONTINUE);
    if (inside_loop_header) {
        context.emplace_diagnostic(Span{context, fid, stmt},
                                   diag_code::continue_statement_inside_of_loop_header,
                                   diag_type::error);
        return {};
    }
    if (inside_compt_for) {
        context.emplace_diagnostic(Span{context, fid, stmt},
                                   diag_code::continue_statement_inside_of_compt_for,
                                   diag_type::error);
        return {};
    }
    if (current_loop_block_eid.empty()) {
        context.emplace_diagnostic(Span{context, fid, stmt},
                                   diag_code::continue_statement_outside_of_loop, diag_type::error);
        return {};
    }
    const MoveMapId snapshot = snapshot_moves(context, lctx.map, current_loop_base_map.as_id());
    current_loop_exit_maps->push_back(snapshot);
    current_loop_back_edge_maps->push_back(snapshot);
    const auto eid = context.emplace_exec(
        ExecJump{.block = current_loop_block_eid.as_id(), .spot = jump_spot::start},
        Span{context, fid, stmt});

    block.push_back_exec(eid);
    return eid;
}

OptId<ExecId> RuntimeSolver::handle_yield(FileId fid, LexicalCtx lctx, InProgressBlock& block,
                                          const ast_stmt_t* stmt) {
    assert(stmt->type == AST_STMT_YIELD);
    assert(current_match);
    const auto maybe_value_eid
        = solve_expr(fid, lctx, stmt->stmt.yield_stmt.expr, current_match->result_tid);
    if (maybe_value_eid.empty()) {
        current_match->poisoned = true;
    } else if (current_match->result_tid.empty()) {
        current_match->result_tid = infer_type_from_exec(maybe_value_eid.as_id());
    }
    current_match->exit_maps.push_back(snapshot_moves(context, lctx.map, current_match->base_map));
    const auto eid
        = context.emplace_exec(ExecYield{.yield_value = maybe_value_eid}, Span{context, fid, stmt});
    block.push_back_exec(eid);
    return eid;
}

OptId<ExecId> RuntimeSolver::solve_loop(FileId fid, LexicalCtx lctx, Span span,
                                        const ast_expr_t* cond_expr, const ast_expr_t* update_expr,
                                        const ast_stmt_t* body_stmt) {
    bool cooked = false;
    const bool outer_inside_loop_header = inside_loop_header;
    const LexicalCtx cond_lctx{.scope = lctx.scope,
                               .map = context.make_persistent_move_map(lctx.map)};

    inside_loop_header = true;
    const auto before_solving_cond_diag_cnt = context.diagnostic_count();
    const auto maybe_cond_eid
        = solve_expr(fid, cond_lctx, cond_expr, context.type_for_builtin(builtin_type::boolean));
    const auto after_solving_cond_diag_cnt = context.diagnostic_count();
    if (maybe_cond_eid.has_value()) {
        try_warn_suspicious_assignment_in_cond(maybe_cond_eid.as_id());
    }
    cooked |= maybe_cond_eid.empty();
    if (maybe_cond_eid.has_value()) {
        if (const std::optional<bool> maybe_compt_bool_cond
            = try_compt_bool(maybe_cond_eid.as_id());
            maybe_compt_bool_cond.has_value() && !maybe_compt_bool_cond.value()) {
            const bool cond_already_warned
                = after_solving_cond_diag_cnt > before_solving_cond_diag_cnt;
            const auto d = context.emplace_diagnostic(
                context.exec(maybe_cond_eid.as_id()).span,
                diag_code::condition_is_always_false_so_loop_body_never_executes,
                cond_already_warned ? diag_type::note : diag_type::warning,
                cond_already_warned ? DiagnosticInfoValue{DiagnosticInfoNoPreview{}}
                                    : DiagnosticNoOtherInfo{});
            if (cond_already_warned) {
                context.force_link_diagnostic(d);
            }
        }
    }

    inside_loop_header = outer_inside_loop_header;
    const auto maybe_loop_eid = solve_loop_core(fid, cond_lctx, span, maybe_cond_eid, cooked,
                                                update_expr, body_stmt, {}, true);
    write_back_moves(context, lctx.map, {MoveExit{.map = cond_lctx.map, .body = {}}});
    return maybe_loop_eid;
}

OptId<ExecId> RuntimeSolver::solve_loop_core(FileId fid, LexicalCtx lctx, Span span,
                                             OptId<ExecId> maybe_cond_eid, bool cooked,
                                             const ast_expr_t* update_expr,
                                             const ast_stmt_t* body_stmt, LoopPrelude prelude,
                                             bool cond_reruns_in_lctx_map) {
    OptId<MoveMapId> maybe_cond_moves{};
    if (cond_reruns_in_lctx_map) {
        maybe_cond_moves = context.make_persistent_move_map();
        write_back_moves(context, maybe_cond_moves.as_id(),
                         {MoveExit{.map = lctx.map, .body = {}}});
    }
    const bool outer_inside_loop_header = inside_loop_header;
    const bool outer_inside_compt_for = inside_compt_for;

    const ExecId loop_eid = context.reserve_exec(span);

    const OptId<ExecId> outer_loop_block_eid = current_loop_block_eid;
    const bool outer_loop_has_break = current_loop_has_break;
    const OptId<MoveMapId> outer_loop_base_map = current_loop_base_map;
    llvm::SmallVector<MoveMapId>* const outer_loop_exit_maps = current_loop_exit_maps;
    llvm::SmallVector<MoveMapId>* const outer_loop_back_edge_maps = current_loop_back_edge_maps;
    llvm::SmallVector<MoveMapId> loop_exit_maps{};
    llvm::SmallVector<MoveMapId> loop_back_edge_maps{};
    current_loop_block_eid = loop_eid;
    current_loop_has_break = false;
    current_loop_base_map = lctx.map;
    current_loop_exit_maps = &loop_exit_maps;
    current_loop_back_edge_maps = &loop_back_edge_maps;
    inside_loop_header = false;
    inside_compt_for = false;

    const Span body_span{context, fid, body_stmt};
    const LexicalCtx body_lctx{.scope = context.make_small_scope(lctx.scope, body_span),
                               .map = context.make_persistent_move_map(lctx.map)};
    InProgressBlock prelude_block{};
    if (prelude) {
        cooked |= !prelude(loop_eid, body_lctx, prelude_block);
    }
    auto maybe_body_eid = solve_block(fid, body_lctx, body_stmt);
    if (maybe_body_eid.has_value() && prelude) {
        prelude_block.push_back_exec(maybe_body_eid.as_id());
        maybe_body_eid = emplace_block_exec(prelude_block, body_lctx, body_span);
    }
    cooked |= maybe_body_eid.empty();
    inside_compt_for = outer_inside_compt_for;

    const bool has_break = current_loop_has_break;
    current_loop_block_eid = outer_loop_block_eid;
    current_loop_has_break = outer_loop_has_break;
    current_loop_base_map = outer_loop_base_map;
    current_loop_exit_maps = outer_loop_exit_maps;
    current_loop_back_edge_maps = outer_loop_back_edge_maps;

    OptId<ExecId> maybe_update_eid{};
    const MoveMapId update_map = context.make_persistent_move_map(body_lctx.map);
    if (update_expr) {
        inside_loop_header = true;
        maybe_update_eid
            = solve_expr(fid, LexicalCtx{.scope = lctx.scope, .map = update_map}, update_expr);
        cooked |= maybe_update_eid.empty();
    }
    inside_loop_header = outer_inside_loop_header;

    llvm::SmallVector<MoveExit> loop_exits{MoveExit{.map = body_lctx.map, .body = maybe_body_eid},
                                           MoveExit{.map = update_map, .body = {}}};
    for (const MoveMapId exit_map : loop_exit_maps) {
        loop_exits.push_back(MoveExit{.map = exit_map, .body = {}});
    }
    write_back_moves(context, lctx.map, loop_exits);

    llvm::SmallVector<MoveExit> back_edges{MoveExit{.map = body_lctx.map, .body = maybe_body_eid},
                                           MoveExit{.map = update_map, .body = {}}};
    for (const MoveMapId back_edge_map : loop_back_edge_maps) {
        back_edges.push_back(MoveExit{.map = back_edge_map, .body = {}});
    }
    if (maybe_cond_moves.has_value()) {
        back_edges.push_back(
            MoveExit{.map = maybe_cond_moves.as_id(),
                     .body = loop_back_edge_maps.empty() ? maybe_body_eid : OptId<ExecId>{}});
    }
    const MoveMapId back_edge_moves = context.make_persistent_move_map(lctx.map);
    write_back_moves(context, back_edge_moves, back_edges);
    context.move_map(back_edge_moves).for_each_local_move([&](DefId did, MoveResult res) {
        if (context.look_up_variable(lctx.scope, context.def(did).name) != did) {
            return;
        }
        for_each_move_eid(context, res, [&](ExecId move_eid) {
            DiagLinker dl{context};
            dl.link(context.emplace_diagnostic(
                context.exec(move_eid).span,
                diag_code::value_declared_outside_of_loop_is_moved_inside_of_loop,
                diag_type::error));
            dl.link(context.emplace_diagnostic_with_message_value(
                context.def(did).span, diag_code::declared_here, diag_type::note,
                DiagnosticSymbolBeforeMessage{.sid = context.def(did).name}));
        });
    });

    // warn on infinitely looping for loops
    if (maybe_cond_eid.has_value() && update_expr) {
        if (const std::optional<bool> maybe_compt_bool_cond
            = try_compt_bool(maybe_cond_eid.as_id());
            maybe_compt_bool_cond.has_value() && maybe_compt_bool_cond.value()) {
            context.emplace_diagnostic(
                context.exec(maybe_cond_eid.as_id()).span,
                diag_code::for_loop_never_terminates_since_condition_is_always_true,
                diag_type::warning);
        }
    }

    if (cooked) {
        const ExecId empty_body_eid = context.emplace_exec(
            ExecBlock{context.emplace_block(Block{.execs = {}, .defs = {}, .lctx = lctx})}, span);
        context.set_reserved_exec_value(loop_eid, ExecLoop{.condition = {},
                                                           .body = empty_body_eid,
                                                           .update = {},
                                                           .has_break = has_break});
        return {};
    }

    context.set_reserved_exec_value(loop_eid, ExecLoop{.condition = maybe_cond_eid.as_id(),
                                                       .body = maybe_body_eid.as_id(),
                                                       .update = maybe_update_eid,
                                                       .has_break = has_break});
    return loop_eid;
}

OptId<ExecId> RuntimeSolver::handle_while(FileId fid, LexicalCtx lctx, InProgressBlock& block,
                                          const ast_stmt_t* stmt) {
    assert(stmt->type == AST_STMT_WHILE);
    const ast_stmt_while_t while_stmt = stmt->stmt.while_stmt;
    const auto maybe_loop_eid = solve_loop(fid, lctx, Span{context, fid, stmt},
                                           while_stmt.condition, nullptr, while_stmt.body_stmt);
    if (maybe_loop_eid.has_value()) {
        block.push_back_exec(maybe_loop_eid.as_id());
    }
    return maybe_loop_eid;
}

OptId<ExecId> RuntimeSolver::handle_for(FileId fid, LexicalCtx lctx, InProgressBlock& block,
                                        const ast_stmt_t* stmt) {
    assert(stmt->type == AST_STMT_FOR);
    const ast_stmt_for_t for_stmt = stmt->stmt.for_stmt;
    const Span span{context, fid, stmt};

    const LexicalCtx for_lctx{.scope = context.make_small_scope(lctx.scope, span), .map = lctx.map};
    InProgressBlock for_block{};
    handle_stmt(fid, for_lctx, for_block, for_stmt.init);

    const auto maybe_loop_eid
        = solve_loop(fid, for_lctx, span, for_stmt.condition, for_stmt.step, for_stmt.body_stmt);
    if (maybe_loop_eid.empty()) {
        return {};
    }
    for_block.push_back_exec(maybe_loop_eid.as_id());

    block.push_back_exec(context.emplace_exec(
        ExecBlock{context.emplace_block(Block{.execs = context.freeze_id_vec(for_block.execs),
                                              .defs = context.freeze_id_vec(for_block.defs),
                                              .lctx = for_lctx})},
        span));
    return maybe_loop_eid;
}

std::optional<bool> RuntimeSolver::try_compt_bool(ExecId eid) {

    const auto maybe_cond = context.exec(eid).try_as<ExecConst>();
    if (!maybe_cond.has_value()) {
        return std::nullopt;
    }
    return maybe_cond.value().try_as<bool>();
}

std::optional<bool> RuntimeSolver::solve_compt_condition(FileId fid, LexicalCtx lctx,
                                                         const ast_expr_t* cond) {
    const auto maybe_cond_eid = ComptExprSolver{def_visitor}.solve_expr(
        fid, lctx.scope, cond, context.type_for_builtin(builtin_type::boolean));
    if (maybe_cond_eid.empty()) {
        return std::nullopt;
    }
    return try_compt_bool(maybe_cond_eid.as_id());
}

OptId<ExecId> RuntimeSolver::handle_if(FileId fid, LexicalCtx lctx, InProgressBlock& block,
                                       const ast_stmt_t* stmt) {
    assert(stmt->type == AST_STMT_IF);

    const ast_stmt_if_t if_stmt = stmt->stmt.if_stmt;
    const TypeId bool_tid = context.type_for_builtin(builtin_type::boolean);
    const ast_stmt_t* else_body = if_stmt.has_else && if_stmt.else_stmt->type == AST_STMT_ELSE
                                      ? if_stmt.else_stmt->stmt.else_stmt.body_stmt
                                      : nullptr;
    bool cooked = if_stmt.has_else && !else_body;

    const auto solve_body
        = [this, fid](LexicalCtx body_lctx, const ast_stmt_t* body) -> OptId<ExecId> {
        if (body->type == AST_STMT_BLOCK) {
            return solve_block(fid, body_lctx, body);
        }
        const Span body_span{context, fid, body};
        const LexicalCtx inner_lctx{.scope = context.make_small_scope(body_lctx.scope, body_span),
                                    .map = body_lctx.map};
        InProgressBlock inner{};
        if (handle_stmt(fid, inner_lctx, inner, body).empty() && inner.execs.empty()) {
            return {};
        }
        return context.emplace_exec(
            ExecBlock{context.emplace_block(Block{.execs = context.freeze_id_vec(inner.execs),
                                                  .defs = context.freeze_id_vec(inner.defs),
                                                  .lctx = inner_lctx})},
            body_span);
    };

    const LexicalCtx then_lctx{
        .scope = context.make_small_scope(lctx.scope, Span{context, fid, if_stmt.body_stmt}),
        .map = context.make_persistent_move_map(lctx.map)};
    InProgressBlock then_prelude{};

    OptId<ExecId> maybe_cond_eid{};
    if (if_stmt.compt) {
        const auto maybe_bool = solve_compt_condition(fid, lctx, if_stmt.condition);

        // never solve the non-taken branch
        if (maybe_bool.has_value() && !cooked) {
            const ast_stmt_t* taken = maybe_bool.value() ? if_stmt.body_stmt : else_body;
            if (!taken) {
                return {};
            }
            const auto maybe_taken_eid = solve_body(lctx, taken);
            if (maybe_taken_eid.has_value()) {
                block.push_back_exec(maybe_taken_eid.as_id());
            }
            return maybe_taken_eid;
        }
        cooked = true;
    } else {
        const auto before_solving_cond_diag_cnt = context.diagnostic_count();
        const ast_expr_t* cond_expr = if_stmt.condition;
        while (cond_expr->type == AST_EXPR_GROUPING) {
            cond_expr = cond_expr->expr.grouping.expr;
        }
        const bool is_decomp
            = cond_expr->type == AST_EXPR_BINARY
              && ComptBinaryOp{cond_expr->expr.binary.op}.holds<is_as_op>()
              && ComptBinaryOp{cond_expr->expr.binary.op}.as<is_as_op>() == is_as_op::is
              && cond_expr->expr.binary.rhs->type == AST_EXPR_VARIANT_DECOMP
              && cond_expr->expr.binary.rhs->expr.variant_decomp.vars.len;
        if (is_decomp) {
            IsDecompTarget decomp_target{
                .then_lctx = then_lctx, .outer_block = block, .then_block = then_prelude};
            maybe_cond_eid = handle_is(fid, lctx, cond_expr, &decomp_target);
        } else {
            maybe_cond_eid = solve_expr(fid, lctx, if_stmt.condition, bool_tid);
        }
        const auto after_solving_cond_diag_cnt = context.diagnostic_count();
        if (maybe_cond_eid.has_value()) {
            try_warn_suspicious_assignment_in_cond(maybe_cond_eid.as_id());
        }
        cooked |= maybe_cond_eid.empty();

        // warn cond always true/false
        if (maybe_cond_eid.has_value()) {
            if (const std::optional<bool> maybe_compt_bool_cond
                = try_compt_bool(maybe_cond_eid.as_id());
                maybe_compt_bool_cond.has_value()) {
                const bool cond_is_true = maybe_compt_bool_cond.value();
                const bool cond_already_warned
                    = after_solving_cond_diag_cnt > before_solving_cond_diag_cnt;
                if (!cond_already_warned) {
                    context.emplace_diagnostic_with_message_value(
                        context.exec(maybe_cond_eid.as_id()).span,
                        diag_code::if_condition_always_evaluates_to, diag_type::warning,
                        DiagnosticSymbolAfterMessage{.sid = cond_is_true
                                                                ? context.symbol_id<"true">()
                                                                : context.symbol_id<"false">()});
                }
                context.force_link_diagnostic(context.emplace_diagnostic(
                    Span{context, fid, if_stmt.body_stmt},
                    cond_is_true ? diag_code::if_body_will_always_execute
                                 : diag_code::if_body_will_never_execute,
                    cond_already_warned ? diag_type::note : diag_type::warning));
                if (!cond_already_warned) {
                    context.force_link_diagnostic(context.emplace_diagnostic(
                        Span{context, fid, stmt->first},
                        diag_code::for_conditional_compilation_use_if_compt, diag_type::help));
                    context.force_link_diagnostic(context.emplace_diagnostic_with_message_value(
                        Span{context, fid, stmt->first}, diag_code::replace_with, diag_type::help,
                        DiagnosticSymbolAfterMessage{.sid = context.symbol_id<"if compt">()}));
                }
            }
        }
    }

    OptId<ExecId> maybe_then_eid = solve_body(then_lctx, if_stmt.body_stmt);
    cooked |= maybe_then_eid.empty();
    if (maybe_then_eid.has_value() && !then_prelude.defs.empty()) {
        then_prelude.push_back_exec(maybe_then_eid.as_id());
        maybe_then_eid
            = context.emplace_exec(ExecBlock{context.emplace_block(
                                       Block{.execs = context.freeze_id_vec(then_prelude.execs),
                                             .defs = context.freeze_id_vec(then_prelude.defs),
                                             .lctx = then_lctx})},
                                   Span{context, fid, if_stmt.body_stmt});
    }

    OptId<ExecId> maybe_else_eid{};
    if (else_body) {
        const LexicalCtx else_lctx{
            .scope = context.make_small_scope(lctx.scope, Span{context, fid, else_body}),
            .map = context.make_persistent_move_map(lctx.map)};
        maybe_else_eid = solve_body(else_lctx, else_body);
        cooked |= maybe_else_eid.empty();
        write_back_moves(context, lctx.map,
                         {MoveExit{.map = then_lctx.map, .body = maybe_then_eid},
                          MoveExit{.map = else_lctx.map, .body = maybe_else_eid}});
    } else {
        write_back_moves(context, lctx.map,
                         {MoveExit{.map = then_lctx.map, .body = maybe_then_eid}});
    }

    if (cooked) {
        return {};
    }

    const ExecId eid = context.emplace_exec(ExecBranch{.condition = maybe_cond_eid.as_id(),
                                                       .then_block = maybe_then_eid.as_id(),
                                                       .else_block = maybe_else_eid},
                                            Span{context, fid, stmt});
    block.push_back_exec(eid);
    return eid;
}

[[nodiscard]] OptId<ExecId> RuntimeSolver::handle_any_typed_expr(FileId fid, LexicalCtx lctx,
                                                                 const ast_expr_t* expr) {
    switch (expr->type) {
        // ------------------ these are purely compt eval'd ------------------
    case AST_EXPR_COMPT:
    case AST_EXPR_STATIC_ASSERT:
    case AST_EXPR_DEFINED:
    case AST_EXPR_HAS_CONTRACT:
    case AST_EXPR_INFERABLE_AS:
    case AST_EXPR_DIAGNOSTIC:
    case AST_EXPR_MEMBERS_OF:
    case AST_EXPR_STATICS_OF:
    case AST_EXPR_REFLECTED_ID:
    case AST_EXPR_REFLECTED_SCOPED_ID:
    case AST_EXPR_ALIGNOF:
    case AST_EXPR_SAME_TYPE:
    case AST_EXPR_SIZEOF:
    case AST_EXPR_TYPE_ID:
    case AST_EXPR_TUDU:
    case AST_EXPR_TYPE_TO_STR:
        return ComptExprSolver{def_visitor}.solve_expr(fid, lctx.scope, expr);
        // ^^^^^^^^^^^^^^^^^^^^^^^ ^^^^^^^^^^^^^^^^^^^^^ ^^^^^^^^^^^^^^^^^^^^^
    case AST_EXPR_LITERAL:
        return handle_literal(fid, expr);
    case AST_EXPR_ID:
        return handle_any_id(fid, lctx, expr);
    case AST_EXPR_GENERIC_ID:
        return handle_any_generic_id(fid, lctx, expr);
    case AST_EXPR_STRUCT_INIT:
        return handle_struct_or_union_init(fid, lctx, expr, {});
    case AST_EXPR_LIST_LITERAL:
        return handle_list_literal(fid, lctx, expr, {});
    case AST_EXPR_GROUPING:
        return handle_grouping(fid, lctx, expr, {});
    case AST_EXPR_TUPLE_INIT:
        return handle_tuple_init(fid, lctx, expr);
    case AST_EXPR_BORROW:
        return handle_borrow(fid, lctx, expr);
    case AST_EXPR_PRE_UNARY:
        return handle_preunary(fid, lctx, expr);
    case AST_EXPR_POST_UNARY:
        return handle_postunary(fid, lctx, expr);
    case AST_EXPR_ADDR_OF:
        return handle_addr_of(fid, lctx, expr);
    case AST_EXPR_BINARY:
        return handle_binary_expr(fid, lctx, expr);
    case AST_EXPR_SUBSCRIPT:
        return handle_subscript(fid, lctx, expr);
    case AST_EXPR_FN_CALL:
        return handle_fn_call(fid, lctx, expr);
    case AST_EXPR_MATCH:
        return handle_match(fid, lctx, expr, {});
    case AST_EXPR_TERNARY_IF:
        return handle_ternary_if(fid, lctx, expr, {});
    case AST_EXPR_BLOCK:
    case AST_EXPR_CLOSURE:
        diag_not_yet_implemented(Span{context, fid, expr});
        break;

    // the are malformed to end up here:
    case AST_EXPR_MATCH_BRANCH:
    case AST_EXPR_TYPE:
    case AST_EXPR_ELSE_MATCH_PATTERN:
    case AST_EXPR_STRUCT_MEMBER_INIT:
    case AST_EXPR_VARIANT_DECOMP:
    case AST_EXPR_INVALID:
        break;
    }
    return {};
}

OptId<ExecId> RuntimeSolver::handle_any_id(FileId fid, LexicalCtx lctx, const ast_expr_t* expr,
                                           bool check_moved, OptId<TypeId> maybe_into_tid) {
    assert(expr->type == AST_EXPR_ID);
    const auto maybe_eid = handle_any_id_impl<compt_or_runtime::runtime>(
        context, def_visitor, fid, lctx.scope, expr->expr.id.slice, {}, maybe_into_tid);
    if (!check_moved || maybe_eid.empty()) {
        return maybe_eid;
    }
    const auto maybe_var = context.exec(maybe_eid.as_id()).try_as<ExecVariable>();
    if (!maybe_var.has_value()) {
        return maybe_eid;
    }
    const DefId did = maybe_var.value().def_id;
    const MoveResult res = context.moved(lctx.map, did);
    if (res.empty()) {
        return maybe_eid;
    }
    DiagLinker dl{context};
    dl.link(context.emplace_diagnostic_with_message_value(
        context.exec(maybe_eid.as_id()).span, diag_code::use_of_moved_value, diag_type::error,
        DiagnosticSymbolAfterMessage{.sid = context.def(did).name}));
    for_each_move_eid(context, res, [&](ExecId move_eid) {
        dl.link(context.emplace_diagnostic(context.exec(move_eid).span, diag_code::value_moved_here,
                                           diag_type::note));
    });
    return {};
}

OptId<ExecId> RuntimeSolver::handle_any_generic_id(FileId fid, LexicalCtx lctx,
                                                   const ast_expr_t* expr) {
    assert(expr->type == AST_EXPR_GENERIC_ID);

    ComptExprSolver solver{def_visitor};
    const auto maybe_gen_args
        = solver.lower_generic_args(fid, lctx.scope, expr->expr.generic_id.args);
    if (maybe_gen_args.empty()) {
        return {};
    }

    return handle_any_id_impl<compt_or_runtime::runtime>(
        context, def_visitor, fid, lctx.scope, expr->expr.generic_id.slice, maybe_gen_args.as_id());
}

OptId<ExecId> RuntimeSolver::handle_literal(FileId fid, const ast_expr_t* expr) {
    const auto maybe_value = solve_expr_literal(context, fid, expr);
    if (!maybe_value) {
        return {};
    }
    return context.emplace_compt_exec(maybe_value.value(), Span{context, fid, expr});
}

OptId<ExecId> RuntimeSolver::handle_literal(FileId fid, const ast_expr_t* expr, TypeId into_tid) {
    const auto maybe_value = solve_expr_literal(context, fid, expr);
    if (!maybe_value) {
        return {};
    }
    const Type& ty = context.type(into_tid);
    if (!ty.holds<TypeBuiltin>()) {
        return context.emplace_compt_exec(maybe_value.value(), Span{context, fid, expr});
    }
    const auto bin_type = ty.as<TypeBuiltin>().type;
    const auto maybe_converted = maybe_value.value().try_down_convert_to(bin_type);
    // take converted if successful, else take the original value
    return context.emplace_compt_exec(
        maybe_converted ? maybe_converted.value() : maybe_value.value(), Span{context, fid, expr});
}

OptId<ExecId> RuntimeSolver::handle_struct_or_union_init(FileId fid, LexicalCtx lctx,
                                                         const ast_expr_t* expr,
                                                         OptId<TypeId> maybe_into_tid) {
    return handle_struct_or_union_init_impl(*this, fid, lctx.scope, lctx.map, expr, maybe_into_tid);
}

OptId<ExecId> RuntimeSolver::handle_list_literal(FileId fid, LexicalCtx lctx,
                                                 const ast_expr_t* expr,
                                                 OptId<TypeId> maybe_into_tid) {
    return solve_list_literal_impl(*this, fid, lctx.scope, lctx.map, expr, maybe_into_tid);
}

OptId<ExecId> RuntimeSolver::handle_tuple_init(FileId fid, LexicalCtx lctx,
                                               const ast_expr_t* expr) {
    return solve_tuple_init_impl(*this, fid, lctx.scope, lctx.map, expr);
}

OptId<ExecId> RuntimeSolver::handle_fn_call(FileId fid, LexicalCtx lctx, const ast_expr_t* expr,
                                            OptId<TypeId> maybe_into_tid) {
    assert(expr->type == AST_EXPR_FN_CALL);
    return solve_fn_call_impl(*this, fid, lctx.scope, lctx.map, expr, std::nullopt, maybe_into_tid);
}

OptId<ExecId> RuntimeSolver::handle_match(FileId fid, LexicalCtx lctx, const ast_expr_t* expr,
                                          OptId<TypeId> maybe_into_tid) {
    assert(expr->type == AST_EXPR_MATCH);

    const auto maybe_scrutinee_eid = solve_place(fid, lctx, expr->expr.match_expr.matched);
    if (maybe_scrutinee_eid.empty()) {
        return {};
    }
    ExecId scrutinee_eid = maybe_scrutinee_eid.as_id();
    const auto maybe_scrutinee_tid = infer_type_from_exec(scrutinee_eid);
    if (maybe_scrutinee_tid.empty()) {
        return {};
    }
    const TypeId scrutinee_tid = maybe_scrutinee_tid.as_id();
    const ast_slice_of_exprs_t branches = expr->expr.match_expr.branches;
    const Span span{context, fid, expr};

    ComptExprSolver compt_solver{def_visitor};
    OptId<DefId> maybe_variant_did{};
    if (const Type& ty = context.type(context.try_decay(scrutinee_tid)); ty.holds<TypeVariant>()) {
        maybe_variant_did = def_visitor.visit_as_independent(ty.as<TypeVariant>().def_id);
        if (!context.def(maybe_variant_did.as_id()).holds<DefVariant>()) {
            return {};
        }
    }

    if (maybe_variant_did.has_value()) {
        if (!valid_exhaustive_match_for_variant(
                *this, lctx.scope, context.def(maybe_variant_did.as_id()).as<DefVariant>().scope,
                fid, maybe_variant_did.as_id(), expr)) {
            return {};
        }
    } else if (!valid_exhaustive_match_for_non_variant(compt_solver, lctx.scope, fid, expr,
                                                       scrutinee_eid)) {
        return {};
    }

    const auto field_for_pattern = [&](const ast_expr_t* pattern) -> OptId<DefId> {
        const token_ptr_slice_t id_slice = pattern->type == AST_EXPR_ID
                                               ? pattern->expr.id.slice
                                               : pattern->expr.variant_decomp.id;
        const auto sid_slice = context.symbol_slice(id_slice);
        const Span pattern_span{context, fid, pattern};
        const ScopeId variant_scope = context.def(maybe_variant_did.as_id()).as<DefVariant>().scope;
        for (const ScopeId scope : {variant_scope, lctx.scope}) {
            if (const auto maybe_did
                = context.look_up_scoped_type(scope, sid_slice, pattern_span)) {
                return maybe_did;
            }
        }
        return context.look_up_scoped_variable(lctx.scope, sid_slice, pattern_span);
    };

    const auto decomposes = +[](const ast_expr_t* pattern) {
        return pattern->type == AST_EXPR_VARIANT_DECOMP && pattern->expr.variant_decomp.vars.len;
    };

    bool any_decomp = false;
    for (size_t i = 0; i < branches.len; ++i) {
        const ast_expr_match_branch_t branch = branches.start[i]->expr.match_branch;
        if (check_for_multi_decomps_on_match_branch(context, fid, branch)) {
            return {};
        }
        any_decomp |= std::any_of(branch.patterns.start,
                                  branch.patterns.start + branch.patterns.len, decomposes);
    }

    const Exec& scrutinee_exec = context.exec(scrutinee_eid);
    const bool plain_variable
        = scrutinee_exec.holds<ExecVariable>()
          || (scrutinee_exec.holds<ExecDeref>()
              && context.exec(scrutinee_exec.as<ExecDeref>().dereferenced).holds<ExecVariable>());

    OptId<DefId> maybe_tmp_did{};
    ExecId scrutinee_ref_eid = scrutinee_eid;
    if (any_decomp && !plain_variable) {
        const Span scrutinee_span = scrutinee_exec.span;
        const auto maybe_consumed_eid = consume(lctx, scrutinee_eid);
        if (maybe_consumed_eid.empty()) {
            return {};
        }
        scrutinee_eid = maybe_consumed_eid.as_id();
        maybe_tmp_did
            = context.register_def(context.symbol_id<"match scrutinee">(), false, false, 0,
                                   scrutinee_span, nullptr, DefVariable{.type_id = scrutinee_tid});
        context.promote_mention_state_of(maybe_tmp_did.as_id(),
                                         Def::mention_state::used_and_mutated);
        locals.push_back(maybe_tmp_did.as_id());
        scrutinee_ref_eid = context.emplace_exec(
            ExecVariable{.def_id = maybe_tmp_did.as_id(), .type_id = scrutinee_tid},
            scrutinee_span);
    }

    MatchCtx match_ctx{
        .result_tid = maybe_into_tid, .base_map = lctx.map, .exit_maps = {}, .poisoned = false};
    MatchCtx* const outer_match = current_match;
    bool poisoned = false;
    llvm::SmallVector<ExecId> branch_eids{};

    for (size_t i = 0; i < branches.len; ++i) {
        const ast_expr_t* branch_expr = branches.start[i];
        const ast_expr_match_branch_t branch = branch_expr->expr.match_branch;
        const Span branch_span{context, fid, branch_expr};
        const LexicalCtx branch_lctx{.scope = context.make_small_scope(lctx.scope, branch_span),
                                     .map = context.make_persistent_move_map(lctx.map)};
        InProgressBlock block{};
        llvm::SmallVector<ExecId> pattern_eids{};

        for (size_t k = 0; k < branch.patterns.len; ++k) {
            const ast_expr_t* pattern = branch.patterns.start[k];
            if (pattern->type == AST_EXPR_ELSE_MATCH_PATTERN) {
                continue;
            }
            if (maybe_variant_did.empty()) {
                const auto maybe_pattern_eid = compt_solver.solve_expr(fid, lctx.scope, pattern);
                if (maybe_pattern_eid.empty()) {
                    return {};
                }
                pattern_eids.push_back(maybe_pattern_eid.as_id());
                continue;
            }
            const auto maybe_field_did = field_for_pattern(pattern);
            if (maybe_field_did.empty()) {
                return {};
            }
            const DefId field_did = maybe_field_did.as_id();
            pattern_eids.push_back(context.emplace_compt_exec(
                ExecConst{context.def(field_did).member_idx}
                    .try_down_convert_to(context.def(maybe_variant_did.as_id())
                                             .as<DefVariant>()
                                             .type_of_discriminant())
                    .value(),
                Span{context, fid, pattern}));
            if (decomposes(pattern)
                && !decompose_variant(fid, branch_lctx, block, pattern, field_did,
                                      scrutinee_ref_eid)) {
                poisoned = true;
            }
        }

        current_match = &match_ctx;
        OptId<ExecId> maybe_value_eid{};
        bool terminated = false;
        if (branch.value->type == AST_EXPR_BLOCK) {
            const ast_slice_of_stmts_t stmts = branch.value->expr.block.stmts;
            for (size_t k = 0; k < stmts.len && !terminated; ++k) {
                const ast_stmt_t* stmt = stmts.start[k];
                const auto maybe_eid = handle_stmt(fid, branch_lctx, block, stmt);
                terminated
                    = stmt->type == AST_STMT_BREAK || stmt->type == AST_STMT_CONTINUE
                      || stmt->type == AST_STMT_YIELD
                      || (maybe_eid.has_value() && definitely_diverges(context, maybe_eid.as_id()));
                if (terminated && k + 1 < stmts.len) {
                    DiagLinker dl{context};
                    dl.link(context.emplace_diagnostic(
                        Span{context, fid, stmts.start[k + 1]->first,
                             stmts.start[stmts.len - 1]->last},
                        diag_code::code_is_unreachable_following_a_block_terminating_statement,
                        diag_type::error));
                    dl.link(context.emplace_diagnostic(Span{context, fid, stmt},
                                                       diag_code::block_terminating_statement_here,
                                                       diag_type::note));
                }
            }
        } else {
            maybe_value_eid = solve_expr(fid, branch_lctx, branch.value, match_ctx.result_tid);
        }
        current_match = outer_match;

        if (branch.value->type == AST_EXPR_BLOCK) {
            if (!terminated) {
                const Span block_span{context, fid, branch.value};
                const auto d0 = context.emplace_diagnostic(
                    block_span, diag_code::match_branch_block_does_not_yield_a_value,
                    diag_type::error);
                const auto d1 = context.emplace_diagnostic(
                    block_span, diag_code::end_block_with_a_yield_statement, diag_type::help);
                context.link_diagnostic(d0, d1);
                poisoned = true;
                continue;
            }
        } else {
            if (maybe_value_eid.empty()) {
                poisoned = true;
                continue;
            }
            if (match_ctx.result_tid.empty()) {
                match_ctx.result_tid = infer_type_from_exec(maybe_value_eid.as_id());
            }
            block.push_back_exec(
                context.emplace_exec(ExecYield{.yield_value = maybe_value_eid}, branch_span));
            match_ctx.exit_maps.push_back(branch_lctx.map);
        }
        const ExecId body_eid = context.emplace_exec(
            ExecBlock{context.emplace_block(Block{.execs = context.freeze_id_vec(block.execs),
                                                  .defs = context.freeze_id_vec(block.defs),
                                                  .lctx = branch_lctx})},
            branch_span);
        branch_eids.push_back(context.emplace_exec(
            ExecMatchBranch{.patterns = context.freeze_id_vec(pattern_eids), .body = body_eid},
            branch_span));
    }

    llvm::SmallVector<MoveExit> match_exits{};
    for (const MoveMapId exit_map : match_ctx.exit_maps) {
        match_exits.push_back(MoveExit{.map = exit_map, .body = {}});
    }
    write_back_moves(context, lctx.map, match_exits);

    if (poisoned || match_ctx.poisoned) {
        return {};
    }

    return context.emplace_exec(ExecMatch{.scrutinee = scrutinee_eid,
                                          .branches = context.freeze_id_vec(branch_eids),
                                          .scrutinee_tmp = maybe_tmp_did,
                                          .result_tid = match_ctx.result_tid},
                                span);
}

OptId<ExecId> RuntimeSolver::handle_ternary_if(FileId fid, LexicalCtx lctx, const ast_expr_t* expr,
                                               OptId<TypeId> maybe_into_tid) {
    assert(expr->type == AST_EXPR_TERNARY_IF);
    const ast_expr_ternary_if_t tif = expr->expr.ternary_if;
    const TypeId bool_tid = context.type_for_builtin(builtin_type::boolean);

    OptId<ExecId> maybe_cond_eid{};
    const auto before_solving_cond_diag_cnt = context.diagnostic_count();
    if (tif.compt) {
        const auto maybe_bool = solve_compt_condition(fid, lctx, tif.condition);
        if (maybe_bool.has_value()) {
            return solve_expr(fid, lctx, maybe_bool.value() ? tif.happy_expr : tif.else_expr,
                              maybe_into_tid);
        }
    } else {
        maybe_cond_eid = solve_expr(fid, lctx, tif.condition, bool_tid);
        if (maybe_cond_eid.has_value()) {
            try_warn_suspicious_assignment_in_cond(maybe_cond_eid.as_id());
        }
    }
    const auto after_solving_cond_diag_cnt = context.diagnostic_count();

    if (maybe_cond_eid.has_value()) {
        if (const std::optional<bool> maybe_compt_bool_cond
            = try_compt_bool(maybe_cond_eid.as_id());
            maybe_compt_bool_cond.has_value()) {
            const bool cond_is_true = maybe_compt_bool_cond.value();
            const bool cond_already_warned
                = after_solving_cond_diag_cnt > before_solving_cond_diag_cnt;
            const auto d0 = context.emplace_diagnostic(
                context.exec(maybe_cond_eid.as_id()).span,
                diag_code::if_condition_always_evaluates_to,
                cond_already_warned ? diag_type::note : diag_type::warning,
                DiagnosticMessageValue{DiagnosticSymbolAfterMessage{
                    .sid
                    = cond_is_true ? context.symbol_id<"true">() : context.symbol_id<"false">()}},
                cond_already_warned ? DiagnosticInfoValue{DiagnosticInfoNoPreview{}}
                                    : DiagnosticNoOtherInfo{});
            if (cond_already_warned) {
                context.force_link_diagnostic(d0);
            } else {
                context.force_link_diagnostic(context.emplace_diagnostic(
                    Span{context, fid, tif.if_tok},
                    diag_code::for_conditional_compilation_use_if_compt, diag_type::help));
                context.force_link_diagnostic(context.emplace_diagnostic_with_message_value(
                    Span{context, fid, tif.if_tok}, diag_code::replace_with, diag_type::help,
                    DiagnosticSymbolAfterMessage{.sid = context.symbol_id<"if compt">()}));
            }
        }
    }

    const LexicalCtx then_lctx{.scope = lctx.scope,
                               .map = context.make_persistent_move_map(lctx.map)};
    const LexicalCtx else_lctx{.scope = lctx.scope,
                               .map = context.make_persistent_move_map(lctx.map)};
    const auto maybe_then_eid = solve_expr(fid, then_lctx, tif.happy_expr, maybe_into_tid);
    const auto maybe_else_eid = solve_expr(fid, else_lctx, tif.else_expr, maybe_into_tid);
    write_back_moves(context, lctx.map,
                     {MoveExit{.map = then_lctx.map, .body = maybe_then_eid},
                      MoveExit{.map = else_lctx.map, .body = maybe_else_eid}});

    if (maybe_cond_eid.empty() || maybe_then_eid.empty() || maybe_else_eid.empty()) {
        return {};
    }

    ExecId then_eid = maybe_then_eid.as_id();
    ExecId else_eid = maybe_else_eid.as_id();
    OptId<TypeId> maybe_result_tid = maybe_into_tid;

    if (maybe_result_tid.empty()) {
        const bool then_const = context.exec(then_eid).holds<ExecConst>();
        const bool else_const = context.exec(else_eid).holds<ExecConst>();
        maybe_result_tid = infer_type_from_exec(then_const && !else_const ? else_eid : then_eid);
        if (maybe_result_tid.empty()) {
            return {};
        }

        const TypeId result_tid = maybe_result_tid.as_id();
        bool cooked = false;
        for (ExecId* eid : {&then_eid, &else_eid}) {
            if (context.exec(*eid).holds<ExecConst>()) {
                if (const auto maybe_conv = context.try_convert_to(*eid, result_tid)) {
                    *eid = maybe_conv.as_id();
                }
            }
            const auto maybe_tid = infer_type_from_exec(*eid);
            if (maybe_tid.empty()) {
                cooked = true;
                continue;
            }
            if (!context.assignable_and_inferable_from_type_to(maybe_tid.as_id(), result_tid)) {
                context.emplace_diagnostic_with_message_value(
                    context.exec(*eid).span, diag_code::cannot_convert_value_of_type,
                    diag_type::error,
                    DiagnosticTypeToType{.from = maybe_tid.as_id(), .to = result_tid});

                cooked = true;
            }
        }

        if (cooked) {
            return {};
        }
    }

    return context.emplace_exec(ExecTernaryIf{.condition = maybe_cond_eid.as_id(),
                                              .then_value = then_eid,
                                              .else_value = else_eid,
                                              .result_tid = maybe_result_tid},
                                Span{context, fid, expr});
}

bool RuntimeSolver::decompose_variant(FileId fid, LexicalCtx branch_lctx, InProgressBlock& block,
                                      const ast_expr_t* decomp_expr, DefId variant_field_did,
                                      ExecId variant_eid) {
    const ast_expr_variant_decomp_t decomp = decomp_expr->expr.variant_decomp;
    if (diag_if_too_many_decomps(context, Span{context, fid, decomp_expr}, variant_field_did,
                                 decomp.vars.len)) {
        return false;
    }
    const auto members = context.def(variant_field_did).as<DefVariantField>().members;
    bool ok = true;
    bool moves_variant = false;
    for (size_t i = 0; i < decomp.vars.len; ++i) {
        const ast_param_t* var = decomp.vars.start[i];
        if (!var->valid) {
            ok = false;
            continue;
        }
        const auto maybe_written_tid = resolve_type(fid, branch_lctx, var->type);
        if (maybe_written_tid.empty()) {
            ok = false;
            continue;
        }
        const TypeId written_tid = maybe_written_tid.as_id();
        const DefId member_did = context.def_id(members.get(i));
        const TypeId member_tid = context.def(member_did).as<DefVariable>().type_id;
        const Type written_ty = context.type(written_tid);
        const bool by_ref
            = written_ty.holds<TypeRef>()
              && context.type_inferable_as(written_ty.as<TypeRef>().inner, member_tid);
        if (!by_ref && !decomp_type_matches(context, written_tid, member_did)) {
            ok = false;
            continue;
        }
        const SymbolId name = context.symbol_id(var->name);
        const Span span{context, fid, var->first, var->last};
        ExecId rhs_eid = context.emplace_exec(
            ExecMemberAccess{.owner = variant_eid, .member = member_did}, span);
        TypeId tid = member_tid;
        if (by_ref) {
            const auto maybe_borrow = try_checked_borrow(context, rhs_eid, written_ty.mut, span);
            if (maybe_borrow.empty()) {
                ok = false;
                continue;
            }
            rhs_eid = maybe_borrow.as_id();
            tid = written_tid;
        } else if (!context.type_is_copyable(member_tid)) {
            moves_variant = true;
        }
        const DefId did = context.register_def(name, false, false, 0, span, nullptr,
                                               DefVariable{.type_id = tid});
        context.insert_variable(branch_lctx.scope, name, did);
        block.push_back_def(did);
        locals.push_back(did);
        block.push_back_exec(context.emplace_exec(
            ExecAssignment{.lhs = context.mark_as_mutated(context.emplace_exec(
                               ExecVariable{.def_id = did, .type_id = tid}, span)),
                           .rhs = rhs_eid},
            span));
    }
    if (ok && moves_variant) {
        const auto maybe_move_eid = consume(branch_lctx, variant_eid);
        if (maybe_move_eid.empty()) {
            return false;
        }
        block.push_back_exec(maybe_move_eid.as_id());
    }
    return ok;
}

OptId<ExecId> RuntimeSolver::handle_borrow(FileId fid, LexicalCtx lctx, const ast_expr_t* expr) {
    assert(expr->type == AST_EXPR_BORROW);

    const auto maybe_inner_eid = solve_place(fid, lctx, expr->expr.borrow.borrowed);

    if (maybe_inner_eid.empty()) {
        return {};
    }

    return try_checked_borrow(context,
                              expr->expr.borrow.mut
                                  ? context.mark_as_mutated(maybe_inner_eid.as_id())
                                  : maybe_inner_eid.as_id(),
                              expr->expr.borrow.mut, Span{context, fid, expr});
}

OptId<ExecId> RuntimeSolver::handle_addr_of(FileId fid, LexicalCtx lctx, const ast_expr_t* expr) {
    assert(expr->type == AST_EXPR_ADDR_OF);

    const auto maybe_inner_eid = solve_place(fid, lctx, expr->expr.addr_of.inner);

    if (maybe_inner_eid.empty()) {
        return {};
    }

    const bool mut_addr_of = expr->expr.addr_of.mut;

    // make sure either:
    // - inner is an lvalue
    // or:
    // - inner is a promotable compt constant and this is an immut addr

    const bool inner_is_lvalue = context.is_lvalue(maybe_inner_eid.as_id());
    const bool inner_is_compt = context.exec(maybe_inner_eid.as_id()).compt;

    if (!inner_is_lvalue && !inner_is_compt) {
        const Exec& exec = context.exec(maybe_inner_eid.as_id());
        context.emplace_diagnostic(exec.span, diag_code::cannot_take_address_of_temporary_value,
                                   diag_type::error);
        return {};
    }

    const auto maybe_inner_tid = context.infer_type_from_exec(maybe_inner_eid.as_id());
    if (maybe_inner_tid.empty()) {
        return {};
    }

    return context.emplace_exec(
        ExecAddrOf{.addressed = maybe_inner_eid.as_id(), .mut = mut_addr_of},
        Span{context, fid, expr});
}

namespace {
/// the constant `1` of the given builtin type, or none if ++/-- is not viable on that type
[[nodiscard]] std::optional<ConstantValue> inc_dec_one_for(builtin_type bt) {
    switch (bt) {
    case builtin_type::u8:
        return ConstantValue{uint8_t{1}};
    case builtin_type::i8:
        return ConstantValue{int8_t{1}};
    case builtin_type::u16:
        return ConstantValue{uint16_t{1}};
    case builtin_type::i16:
        return ConstantValue{int16_t{1}};
    case builtin_type::u32:
        return ConstantValue{uint32_t{1}};
    case builtin_type::i32:
        return ConstantValue{int32_t{1}};
    case builtin_type::u64:
        return ConstantValue{uint64_t{1}};
    case builtin_type::i64:
        return ConstantValue{int64_t{1}};
    case builtin_type::f32:
        return ConstantValue{float{1}};
    case builtin_type::f64:
        return ConstantValue{double{1}};
    case builtin_type::charr:
        return ConstantValue{char{1}};
    case builtin_type::voidd:
    case builtin_type::str:
    case builtin_type::nullpointer:
    case builtin_type::boolean:
        break;
    }
    return std::nullopt;
}

/// error at the operator + linked note with the operand's type
void diag_invalid_unary_operand(Context& context, Span op_span, Span operand_span,
                                TypeId operand_tid) {
    auto d0 = context.emplace_diagnostic(op_span, diag_code::invalid_operand_for_unary_expression,
                                         diag_type::error);
    auto d1 = context.emplace_diagnostic_with_message_value(
        operand_span, diag_code::value_is_of_type, diag_type::note,
        DiagnosticTypeAfterMessage{.tid = operand_tid});
    context.link_diagnostic(d0, d1);
}
} // namespace

OptId<ExecId> RuntimeSolver::handle_preunary(FileId fid, LexicalCtx lctx, const ast_expr_t* expr) {
    assert(expr->type == AST_EXPR_PRE_UNARY);

    const Span op_span{context, fid, expr->expr.unary.op};
    const auto maybe_op = token_to_unary_op(expr->expr.unary.op);
    if (!maybe_op.has_value()) {
        context.emplace_diagnostic(op_span, diag_code::invalid_operand_for_unary_expression,
                                   diag_type::error);
        return {};
    }
    const unary_op op = maybe_op.value();

    switch (op) {
    case unary_op::inc:
    case unary_op::dec:
        return handle_inc_dec(fid, lctx, expr, op, /*post*/ false);
    case unary_op::plus:
    case unary_op::minus:
    case unary_op::bool_not:
    case unary_op::bit_not:
        break;
    }

    const auto maybe_inner_eid = solve_expr(fid, lctx, expr->expr.unary.expr);
    if (maybe_inner_eid.empty()) {
        return {};
    }

    const ExecId inner_eid = maybe_inner_eid.as_id();

    // fold constants thru the compt solver
    if (context.exec(inner_eid).holds<ExecConst>()) {
        return ComptExprSolver{def_visitor}.solve_preunary_exec(op, op_span, inner_eid);
    }

    const auto maybe_inner_tid = context.infer_type_from_exec(inner_eid);
    if (maybe_inner_tid.empty()) {
        return {};
    }
    const TypeId inner_tid = maybe_inner_tid.as_id();

    bool viable{};
    std::optional<builtin_type> maybe_unsigned_bt{};
    {
        const Type& ty = context.type(inner_tid);
        if (ty.holds<TypeBuiltin>()) {
            const builtin_type bt = ty.as<TypeBuiltin>().type;
            viable = builtin_type_has_unary_op(bt, op);
            if (op == unary_op::minus && is_unsigned_builtin(bt)) {
                maybe_unsigned_bt = bt;
            }
        }
    }
    if (maybe_unsigned_bt.has_value()) {
        diag_unsigned_negation(context, Span{context, fid, expr}, maybe_unsigned_bt.value());
        return {};
    }
    if (!viable) {
        diag_invalid_unary_operand(context, op_span, context.exec(inner_eid).span, inner_tid);
        return {};
    }

    return context.emplace_exec(ExecUnary{.operand = inner_eid, .op = op},
                                Span{context, fid, expr});
}

OptId<ExecId> RuntimeSolver::handle_postunary(FileId fid, LexicalCtx lctx, const ast_expr_t* expr) {
    assert(expr->type == AST_EXPR_POST_UNARY);

    switch (expr->expr.unary.op->type) {
    case TOK_INC:
        return handle_inc_dec(fid, lctx, expr, unary_op::inc, /*post*/ true);
    case TOK_DEC:
        return handle_inc_dec(fid, lctx, expr, unary_op::dec, /*post*/ true);
    case TOK_DEREF:
        return handle_deref(fid, lctx, expr);
    default:
        context.emplace_diagnostic(Span{context, fid, expr->expr.unary.op},
                                   diag_code::invalid_operand_for_unary_expression,
                                   diag_type::error);
        return {};
    }
}

OptId<ExecId> RuntimeSolver::handle_deref(FileId fid, LexicalCtx lctx, const ast_expr_t* expr) {
    assert(expr->type == AST_EXPR_POST_UNARY && expr->expr.unary.op->type == TOK_DEREF);

    const auto maybe_inner_eid = solve_place(fid, lctx, expr->expr.unary.expr);
    if (maybe_inner_eid.empty()) {
        return {};
    }
    const ExecId inner_eid = maybe_inner_eid.as_id();

    const auto maybe_inner_tid = context.infer_type_from_exec(inner_eid);
    if (maybe_inner_tid.empty()) {
        return {};
    }
    const TypeId inner_tid = maybe_inner_tid.as_id();

    // only typed pointers (not refs, which are auto-dereferenced, nor null or *void) can be
    // dereferenced
    bool ptr_viable{};
    {
        const Type& ty = context.type(inner_tid);
        if (ty.holds<TypePtr>()) {
            const Type& pointee = context.type(ty.as<TypePtr>().inner);
            ptr_viable = !pointee.holds<TypeBuiltin>()
                         || pointee.as<TypeBuiltin>().type != builtin_type::voidd;
        }
    }

    if (!ptr_viable) {
        diag_invalid_unary_operand(context, Span{context, fid, expr->expr.unary.op},
                                   context.exec(inner_eid).span, inner_tid);
        return {};
    }

    // unlike refs, top level ptr mut only means the ptr is rebindable, so the pointee type
    // carries its own mutability
    return context.emplace_exec(ExecDeref{.dereferenced = inner_eid, .mut = false},
                                Span{context, fid, expr});
}

OptId<ExecId> RuntimeSolver::handle_inc_dec(FileId fid, LexicalCtx lctx, const ast_expr_t* expr,
                                            unary_op op, bool post) {
    assert(expr->type == AST_EXPR_PRE_UNARY || expr->type == AST_EXPR_POST_UNARY);
    assert(op == unary_op::inc || op == unary_op::dec);

    const auto maybe_lhs_eid = solve_place(fid, lctx, expr->expr.unary.expr);
    if (maybe_lhs_eid.empty()) {
        return {};
    }
    const ExecId lhs_eid = context.mark_as_mutated(maybe_lhs_eid.as_id());

    const Span lhs_span = context.exec(lhs_eid).span;

    if (!context.is_lvalue(lhs_eid)) {
        context.emplace_diagnostic(lhs_span, diag_code::cannot_assign_to_temporary_value,
                                   diag_type::error);
        return {};
    }

    const auto maybe_lhs_tid = context.infer_type_from_exec(lhs_eid);
    if (maybe_lhs_tid.empty()) {
        return {};
    }
    const TypeId lhs_tid = maybe_lhs_tid.as_id();

    // figure out the constant 1 to add/subtract
    std::optional<ConstantValue> maybe_one{};
    bool lhs_mut{};
    {
        const Type& ty = context.type(lhs_tid);
        lhs_mut = ty.mut;
        if (ty.holds<TypeBuiltin>()) {
            maybe_one = inc_dec_one_for(ty.as<TypeBuiltin>().type);
        } else if (ty.holds<TypePtr>()) { // fn ptrs aren't viable
            const Type& pointee = context.type(ty.as<TypePtr>().inner);
            const bool void_pointee = pointee.holds<TypeBuiltin>()
                                      && pointee.as<TypeBuiltin>().type == builtin_type::voidd;
            if (!void_pointee) {
                // increment by the pointed to type's width
                maybe_one = ConstantValue{
                    context.pointer_size_bytes() == 8
                        // try_inner safe here cuz TypePtr
                        ? static_cast<uint64_t>(
                              context.layout_for_type(ty.try_inner().as_id()).width)
                        : static_cast<uint32_t>(
                              context.layout_for_type(ty.try_inner().as_id()).width)};
            }
        }
    }

    if (!maybe_one.has_value()) {
        diag_invalid_unary_operand(context, Span{context, fid, expr->expr.unary.op}, lhs_span,
                                   lhs_tid);
        return {};
    }

    if (!lhs_mut) {
        auto d0 = context.emplace_diagnostic(lhs_span, diag_code::immutable_value_is_not_assignable,
                                             diag_type::error);
        const auto maybe_did = context.try_def_for_exec(lhs_eid);
        if (maybe_did.has_value()) {
            const Def& def = context.def(maybe_did.as_id());
            auto d1 = context.emplace_diagnostic_with_message_value(
                def.span, diag_code::declared_here, diag_type::note,
                DiagnosticSymbolBeforeMessage{.sid = def.name});
            context.link_diagnostic(d0, d1);
        }
        // like borrowing, this doesn't poison anything beyond this expr, so keep going
    }

    const Span op_span{context, fid, expr->expr.unary.op};
    const ExecId one_eid = context.emplace_compt_exec(ExecConst{maybe_one.value()}, op_span);

    return context.emplace_exec(
        ExecCompoundAssignment{.lhs = lhs_eid,
                               .rhs = one_eid,
                               .op = (op == unary_op::inc) ? binary_op::plus : binary_op::minus,
                               .yield_old = post},
        Span{context, fid, expr});
}

OptId<ExecId> RuntimeSolver::handle_binary_expr(FileId fid, LexicalCtx lctx,
                                                const ast_expr_t* expr) {
    assert(expr->type == AST_EXPR_BINARY);

    ComptBinaryOp maybe_bin_op{expr->expr.binary.op};

    if (maybe_bin_op.holds<InvalidOp>()) {
        return {};
    }
    if (maybe_bin_op.holds<assign_op>()) {
        return handle_assign(fid, lctx, expr);
    }
    if (maybe_bin_op.holds<access_op>()) {
        return handle_access(fid, lctx, expr, maybe_bin_op.as<access_op>());
    }
    if (maybe_bin_op.holds<binary_op>()) {
        return handle_arithmetic_logical_or_bool_binary(fid, lctx, expr,
                                                        maybe_bin_op.as<binary_op>());
    }
    if (maybe_bin_op.holds<is_as_op>()) {
        if (maybe_bin_op.as<is_as_op>() == is_as_op::is) {
            return handle_is(fid, lctx, expr);
        }
        assert(maybe_bin_op.as<is_as_op>() == is_as_op::as);
        return handle_as(fid, lctx, expr);
    }
    return {};
}

namespace {
[[nodiscard]] std::optional<binary_op> compound_assign_op_to_binary_op(assign_op op) {
    switch (op) {
    case assign_op::assign_eq:
        return std::nullopt;
    case assign_op::assign_plus_eq:
        return binary_op::plus;
    case assign_op::assign_minus_eq:
        return binary_op::minus;
    case assign_op::assign_mult_eq:
        return binary_op::multiply;
    case assign_op::assign_div_eq:
        return binary_op::divide;
    case assign_op::assign_mod_eq:
        return binary_op::modulo;
    case assign_op::assign_and_eq:
        return binary_op::bit_and;
    case assign_op::assign_or_eq:
        return binary_op::bit_or;
    case assign_op::assign_xor_eq:
        return binary_op::bit_xor;
    case assign_op::assign_lsh_eq:
        return binary_op::left_bitshift;
    case assign_op::assign_rsh_eq:
        return binary_op::right_shift;
    }
    std::unreachable();
}
} // namespace

OptId<ExecId> RuntimeSolver::handle_assign(FileId fid, LexicalCtx lctx, const ast_expr_t* expr) {
    assert(expr->type == AST_EXPR_BINARY);

    const ast_expr_t* lhs_expr = expr->expr.binary.lhs;
    while (lhs_expr->type == AST_EXPR_GROUPING) {
        lhs_expr = lhs_expr->expr.grouping.expr;
    }
    const bool plain_assign
        = ComptBinaryOp{expr->expr.binary.op}.as<assign_op>() == assign_op::assign_eq;
    const auto maybe_lhs_eid = plain_assign && lhs_expr->type == AST_EXPR_ID
                                   ? handle_any_id(fid, lctx, lhs_expr, /*check_moved=*/false)
                                   : solve_place(fid, lctx, lhs_expr);
    if (maybe_lhs_eid.empty()) {
        return {};
    }
    context.mark_as_mutated(maybe_lhs_eid.as_id());
    const auto maybe_lhs_tid = infer_type_from_exec(maybe_lhs_eid.as_id());
    if (maybe_lhs_tid.empty()) {
        return {};
    }

    const auto maybe_compound_op
        = compound_assign_op_to_binary_op(ComptBinaryOp{expr->expr.binary.op}.as<assign_op>());

    const bool ptr_arith = maybe_compound_op.has_value()
                           && (maybe_compound_op.value() == binary_op::plus
                               || maybe_compound_op.value() == binary_op::minus)
                           && context.type(maybe_lhs_tid.as_id()).holds<TypePtr>();

    const bool compound_op_viable = [&] {
        if (!maybe_compound_op.has_value() || ptr_arith) {
            return true;
        }
        const Type& lhs_ty = context.type(maybe_lhs_tid.as_id());
        return lhs_ty.holds<TypeBuiltin>() && lhs_ty.as<TypeBuiltin>().type != builtin_type::str
               && builtin_type_has_binary_op(lhs_ty.as<TypeBuiltin>().type,
                                             maybe_compound_op.value());
    }();

    const TypeId rhs_target_tid = ptr_arith ? context.usize_type() : maybe_lhs_tid.as_id();

    const auto maybe_rhs_eid = compound_op_viable
                                   ? solve_expr(fid, lctx, expr->expr.binary.rhs, rhs_target_tid)
                                   : solve_expr(fid, lctx, expr->expr.binary.rhs);
    if (maybe_rhs_eid.empty()) {
        return {};
    }
    const auto maybe_rhs_tid = infer_type_from_exec(maybe_rhs_eid.as_id());
    if (maybe_rhs_tid.empty()) {
        return {};
    }

    if (!compound_op_viable) {
        context.emplace_diagnostic_with_message_value(
            Span{context, fid, expr}, diag_code::incompatible_types_for_binary_operator_colon,
            diag_type::error,
            DiagnosticTypeAndTypeForBinaryOp{.lhs_tid = maybe_lhs_tid.as_id(),
                                             .rhs_tid = maybe_rhs_tid.as_id(),
                                             .op = maybe_compound_op.value()});
        return {};
    }

    // lvalue check cuz ofc
    if (!context.is_lvalue(maybe_lhs_eid.as_id())) {
        context.emplace_diagnostic(Span{context, fid, expr->expr.binary.lhs},
                                   diag_code::cannot_assign_to_temporary_value, diag_type::error);
        return {};
    }

    // immut check
    if (!context.type(maybe_lhs_tid.as_id()).mut) {
        DiagLinker dl{context};
        dl.link(context.emplace_diagnostic(Span{context, fid, expr->expr.binary.lhs},
                                           diag_code::immutable_value_is_not_assignable,
                                           diag_type::error));
        const auto maybe_did = context.try_def_for_exec(maybe_lhs_eid.as_id());
        if (maybe_did) {
            const Def& d = context.def(maybe_did.as_id());
            dl.link(context.emplace_diagnostic_with_message_value(
                d.span, diag_code::declared_here, diag_type::note,
                DiagnosticSymbolBeforeMessage{.sid = d.name}));
        }
        return {};
    }

    // direct type check
    if (!context.assignable_from_type_to(maybe_rhs_tid.as_id(), rhs_target_tid)) {
        DiagLinker dl{context};
        dl.link(context.emplace_diagnostic_with_message_value(
            Span{context, fid, expr}, diag_code::cannot_convert_value_of_type, diag_type::error,
            DiagnosticTypeToType{.from = maybe_rhs_tid.as_id(), .to = rhs_target_tid}));
        const auto maybe_lhs_did = context.try_def_for_exec(maybe_lhs_eid.as_id());
        if (maybe_lhs_did) {
            const Def& d = context.def(maybe_lhs_did.as_id());
            dl.link(context.emplace_diagnostic_with_message_value(
                d.span, diag_code::declared_here, diag_type::note,
                DiagnosticSymbolAfterMessage{.sid = d.name}));
        }
        const auto maybe_rhs_did = context.try_def_for_exec(maybe_rhs_eid.as_id());
        if (maybe_rhs_did) {
            const Def& d = context.def(maybe_rhs_did.as_id());
            dl.link(context.emplace_diagnostic_with_message_value(
                d.span, diag_code::declared_here, diag_type::note,
                DiagnosticSymbolAfterMessage{.sid = d.name}));
        }
        return {};
    }
    if (ptr_arith) {
        const Type& pointee = context.type(context.type(maybe_lhs_tid.as_id()).as<TypePtr>().inner);
        if (pointee.holds<TypeBuiltin>() && pointee.as<TypeBuiltin>().type == builtin_type::voidd) {
            context.emplace_diagnostic_with_message_value(
                Span{context, fid, expr}, diag_code::incompatible_types_for_binary_operator_colon,
                diag_type::error,
                DiagnosticTypeAndTypeForBinaryOp{.lhs_tid = maybe_lhs_tid.as_id(),
                                                 .rhs_tid = maybe_rhs_tid.as_id(),
                                                 .op = maybe_compound_op.value()});
            return {};
        }
        const auto maybe_scaled_eid
            = scale_ptr_offset(maybe_lhs_tid.as_id(), maybe_rhs_eid.as_id());
        if (maybe_scaled_eid.empty()) {
            return {};
        }
        return context.emplace_exec(ExecCompoundAssignment{.lhs = maybe_lhs_eid.as_id(),
                                                           .rhs = maybe_scaled_eid.as_id(),
                                                           .op = maybe_compound_op.value(),
                                                           .yield_old = false},
                                    Span{context, fid, expr});
    }
    if (maybe_compound_op.has_value()) {
        if (guard_compt_rhs_operand(context.type(maybe_lhs_tid.as_id()).as<TypeBuiltin>().type,
                                    maybe_compound_op.value(), maybe_rhs_eid.as_id())) {
            return {};
        }
        return context.emplace_exec(ExecCompoundAssignment{.lhs = maybe_lhs_eid.as_id(),
                                                           .rhs = maybe_rhs_eid.as_id(),
                                                           .op = maybe_compound_op.value(),
                                                           .yield_old = false},
                                    Span{context, fid, expr});
    }

    return context.emplace_exec(
        ExecAssignment{.lhs = maybe_lhs_eid.as_id(), .rhs = maybe_rhs_eid.as_id()},
        Span{context, fid, expr});
}

OptId<ExecId> RuntimeSolver::handle_is(FileId fid, LexicalCtx lctx, const ast_expr_t* expr,
                                       IsDecompTarget* decomp_target) {
    assert(expr->type == AST_EXPR_BINARY);

    const auto maybe_lhs_eid = solve_place(fid, lctx, expr->expr.binary.lhs);
    if (maybe_lhs_eid.empty()) {
        return {};
    }

    const auto maybe_lhs_tid = infer_type_from_exec(maybe_lhs_eid.as_id());
    if (maybe_lhs_tid.empty()) {
        return {};
    }

    const Exec& lhs_exec = context.exec(maybe_lhs_eid.as_id());
    const Type& ty = context.type(maybe_lhs_tid.as_id());

    if (!ty.holds<TypeVariant>()) {
        const auto d0 = context.emplace_diagnostic(
            lhs_exec.span, diag_code::cannot_use_is_for_non_variant_values, diag_type::error,
            DiagnosticSubCode{.sub_code = diag_code::not_a_variant});
        const auto d1 = context.emplace_diagnostic_with_message_value(
            lhs_exec.span, diag_code::value_is_of_type, diag_type::note,
            DiagnosticTypeAfterMessage{.tid = maybe_lhs_tid.as_id()});
        context.link_diagnostic(d0, d1);
        return {};
    }
    const auto* pattern_expr = expr->expr.binary.rhs;
    if (pattern_expr->type != AST_EXPR_VARIANT_DECOMP) {
        return {}; // poisoned
    }
    const DefId variant_did = def_visitor.visit_as_independent(ty.as<TypeVariant>().def_id);
    if (!context.def(variant_did).holds<DefVariant>()) {
        return {};
    }
    const auto sid_slice = context.symbol_slice(pattern_expr->expr.variant_decomp.id);
    const auto scope = context.def(variant_did).template as<DefVariant>().scope;
    const auto maybe_var_field
        = context.look_up_scoped_type(scope, sid_slice, Span{context, fid, pattern_expr});
    if (maybe_var_field.empty()) {
        context.emplace_diagnostic(
            Span{context, fid, pattern_expr}, diag_code::identifer_does_not_name_a_valid_pattern,
            diag_type::error, DiagnosticSubCode{.sub_code = diag_code::not_a_variant_field});
        return {};
    }

    const DefId hopefully_var_field = maybe_var_field.as_id();

    if (!context.def(hopefully_var_field).template holds<DefVariantField>()) {
        context.emplace_diagnostic(
            Span{context, fid, pattern_expr}, diag_code::identifer_does_not_name_a_valid_pattern,
            diag_type::error, DiagnosticSubCode{.sub_code = diag_code::not_a_variant_field});
        return {};
    }

    DiagLinker dl{context};

    const bool decomposes = pattern_expr->expr.variant_decomp.vars.len != 0;
    if (decomposes) {
        if (!decomp_target) {
            Span span{context, fid, pattern_expr};
            dl.link(context.emplace_diagnostic(
                span, diag_code::variant_decomposition_not_allowed_here, diag_type::error));
            dl.link(context.emplace_diagnostic_with_message_value(
                span, diag_code::replace_with, diag_type::help,
                DiagnosticIdentifierAfterMessage{.sid_slice = sid_slice}));
        }
    }

    if (!context.check_variant_field_has_parent(dl, hopefully_var_field,
                                                ty.as<TypeVariant>().def_id,
                                                Span{context, fid, pattern_expr})) {
        return {};
    }

    const Span lhs_span = lhs_exec.span;
    const TypeId lhs_tid = maybe_lhs_tid.as_id();
    const DefId lhs_variant_did = ty.as<TypeVariant>().def_id;
    ExecId scrutinee_eid = maybe_lhs_eid.as_id();
    if (decomposes && decomp_target) {
        const Exec& scrutinee_exec = context.exec(scrutinee_eid);
        const bool plain_variable = scrutinee_exec.holds<ExecVariable>()
                                    || (scrutinee_exec.holds<ExecDeref>()
                                        && context.exec(scrutinee_exec.as<ExecDeref>().dereferenced)
                                               .holds<ExecVariable>());
        if (!plain_variable) {
            const auto maybe_consumed_eid = consume(lctx, scrutinee_eid);
            if (maybe_consumed_eid.empty()) {
                return {};
            }
            const DefId tmp_did
                = context.register_def(context.symbol_id<"is scrutinee">(), false, false, 0,
                                       lhs_span, nullptr, DefVariable{.type_id = lhs_tid});
            context.promote_mention_state_of(tmp_did, Def::mention_state::used);
            locals.push_back(tmp_did);
            decomp_target->outer_block.push_back_def(tmp_did);
            scrutinee_eid = context.emplace_exec(
                ExecVariable{.def_id = tmp_did, .type_id = lhs_tid}, lhs_span);
            decomp_target->outer_block.push_back_exec(
                context.emplace_exec(ExecAssignment{.lhs = context.mark_as_mutated(scrutinee_eid),
                                                    .rhs = maybe_consumed_eid.as_id()},
                                     lhs_span));
        }
        if (!decompose_variant(fid, decomp_target->then_lctx, decomp_target->then_block,
                               pattern_expr, hopefully_var_field, scrutinee_eid)) {
            return {};
        }
    }

    ExecId discrim_eid = context.emplace_exec(
        ExecBuiltinAccess{.from = scrutinee_eid, .kind = builtin_access::variant_discriminant},
        lhs_span);
    const Def& variant_def = context.def(lhs_variant_did);
    ExecId field_value_eid = context.emplace_exec(
        ExecConst{context.def(hopefully_var_field).member_idx}
            .try_down_convert_to(variant_def.as<DefVariant>().type_of_discriminant())
            .value(),
        lhs_span);

    return context.emplace_exec(
        ExecBinary{.lhs = discrim_eid, .rhs = field_value_eid, .op = binary_op::bool_equal},
        Span{context, fid, expr});
}
OptId<ExecId> RuntimeSolver::handle_as(FileId fid, LexicalCtx lctx, const ast_expr_t* expr) {
    assert(expr->type == AST_EXPR_BINARY);

    const auto maybe_lhs_eid = solve_expr(fid, lctx, expr->expr.binary.lhs);
    if (maybe_lhs_eid.empty()) {
        return {};
    }

    const ast_expr_t* rhs_expr = expr->expr.binary.rhs;
    if (rhs_expr->type != AST_EXPR_TYPE) {
        return {}; // poisoned
    }

    const auto maybe_into_tid = resolve_type(fid, lctx, rhs_expr->expr.type_expr.type);
    if (maybe_into_tid.empty()) {
        return {};
    }

    const auto maybe_lhs_tid = infer_type_from_exec(maybe_lhs_eid.as_id());
    if (maybe_lhs_tid.empty()) {
        return {};
    }

    // try constant fold
    if (context.exec(maybe_lhs_eid.as_id()).compt) {
        return ComptExprSolver{def_visitor}.solve_builtin_cast(maybe_lhs_eid.as_id(),
                                                               maybe_into_tid.as_id());
    }

    Span span{context, fid, expr};
    if (!context.type_castable_to(maybe_lhs_tid.as_id(), maybe_into_tid.as_id())) {
        context.emplace_diagnostic_with_message_value(
            span, diag_code::cannot_convert_value_of_type, diag_type::error,
            DiagnosticTypeToType{.from = maybe_lhs_tid.as_id(), .to = maybe_into_tid.as_id()});
        return {};
    }

    return context.emplace_exec(
        ExecCast{.exec = maybe_lhs_eid.as_id(), .target_tid = maybe_into_tid.as_id()}, span);
}

OptId<ExecId> RuntimeSolver::handle_subscript(FileId fid, LexicalCtx lctx, const ast_expr_t* expr) {
    assert(expr->type == AST_EXPR_SUBSCRIPT);

    const auto maybe_base_eid = solve_place(fid, lctx, expr->expr.subscript.lhs);
    if (maybe_base_eid.empty()) {
        return {}; // poisoned
    }
    const ExecId base_eid = maybe_base_eid.as_id();

    const auto maybe_base_tid = infer_type_from_exec(base_eid);
    if (maybe_base_tid.empty()) {
        return {};
    }
    const TypeId base_tid = maybe_base_tid.as_id();

    bool indexable{};
    bool void_ptr{};
    {
        const Type& ty = context.type(base_tid);
        if (ty.holds_any_of<TypeSlice, TypeArr>()
            || (ty.holds<TypeBuiltin>() && ty.as<TypeBuiltin>().type == builtin_type::str)) {
            indexable = true;
        } else if (ty.holds<TypePtr>()) {
            const Type& pointee = context.type(ty.as<TypePtr>().inner);
            void_ptr = pointee.holds<TypeBuiltin>()
                       && pointee.as<TypeBuiltin>().type == builtin_type::voidd;
            indexable = !void_ptr;
        }
    }

    const Span base_span = context.exec(base_eid).span;
    if (!indexable) {
        if (void_ptr) {
            context.emplace_diagnostic(
                base_span, diag_code::cannot_dereference_value_of_type, diag_type::error,
                DiagnosticTypeAfterMessage{.tid = base_tid},
                DiagnosticSubCode{.sub_code = diag_code::void_pointers_cannot_be_dereferenced});
            return {};
        }
        auto d0 = context.emplace_diagnostic(base_span, diag_code::value_is_not_indexable,
                                             diag_type::error);
        auto d1 = context.emplace_diagnostic_with_message_value(
            base_span, diag_code::value_is_of_type, diag_type::note,
            DiagnosticTypeAfterMessage{.tid = base_tid});
        context.link_diagnostic(d0, d1);
        return {};
    }

    // solve_expr w/ an into type emits the conversion diagnostic itself
    const auto maybe_idx_eid
        = solve_expr(fid, lctx, expr->expr.subscript.subexpr, context.usize_type());
    if (maybe_idx_eid.empty()) {
        return {};
    }

    // fold when both the base (a list literal or str) and the index are compt
    if (const Exec& idx_exec = context.exec(maybe_idx_eid.as_id());
        idx_exec.compt && idx_exec.holds<ExecConst>()) {
        const Exec& base_exec = context.exec(base_eid);
        const Span idx_span{context, fid, expr->expr.subscript.subexpr};
        const Span expr_span{context, fid, expr};
        if (base_exec.compt && base_exec.holds<ExecListLiteral>()) {
            return fold_compt_list_subscript(context, base_eid, maybe_idx_eid.as_id(), idx_span,
                                             expr_span);
        }
        if (base_exec.compt && base_exec.holds<ExecConst>()
            && base_exec.as<ExecConst>().holds<SymbolId>()) {
            return fold_compt_str_subscript(context, base_eid, maybe_idx_eid.as_id(), idx_span,
                                            expr_span);
        }
        // runtime arrays still have a statically known size, so bounds check compt indices
        if (const Type& base_ty = context.type(base_tid);
            base_ty.holds<TypeArr>()
            && diag_if_compt_subscript_out_of_bounds(
                context, compt_subscript_idx(context, maybe_idx_eid.as_id()),
                base_ty.as<TypeArr>().canonical_size, idx_span)) {
            return {};
        }
    }

    return context.emplace_exec(ExecSubscript{.base = base_eid, .index = maybe_idx_eid.as_id()},
                                Span{context, fid, expr});
}

OptId<ExecId> RuntimeSolver::handle_access(FileId fid, LexicalCtx lctx, const ast_expr_t* expr,
                                           access_op op) {
    assert(expr->type == AST_EXPR_BINARY);

    OptId<ExecId> maybe_lhs = solve_place(fid, lctx, expr->expr.binary.lhs);
    if (!maybe_lhs.has_value()) {
        return {}; // poisoned
    }
    ExecId lhs_eid = maybe_lhs.as_id();

    if (op == access_op::rarrow) {
        const auto maybe_lhs_tid = infer_type_from_exec(lhs_eid);
        if (maybe_lhs_tid.empty()) {
            return {};
        }
        bool ptr_viable{};
        bool void_ptr{};
        {
            const Type& ty = context.type(maybe_lhs_tid.as_id());
            if (ty.holds<TypePtr>()) {
                const Type& pointee = context.type(ty.as<TypePtr>().inner);
                void_ptr = pointee.holds<TypeBuiltin>()
                           && pointee.as<TypeBuiltin>().type == builtin_type::voidd;
                ptr_viable = !pointee.holds<TypeBuiltin>() || !void_ptr;
            }
        }
        const Span lhs_span = context.exec(lhs_eid).span;
        if (!ptr_viable) {
            context.emplace_diagnostic(
                lhs_span, diag_code::cannot_dereference_value_of_type, diag_type::error,
                DiagnosticTypeAfterMessage{.tid = maybe_lhs_tid.as_id()},
                DiagnosticSubCode{.sub_code = void_ptr
                                                  ? diag_code::void_pointers_cannot_be_dereferenced
                                                  : diag_code::value_is_not_a_pointer});
            return {};
        }
        op = access_op::dot;
        // pointee type carries its own mutability (top level ptr mut only means rebindable)
        lhs_eid = context.emplace_exec(ExecDeref{.dereferenced = lhs_eid, .mut = false}, lhs_span);
    }

    const ast_expr_t* rhs_expr = expr->expr.binary.rhs;
    const Span rhs_span{context, fid, rhs_expr};

    auto matches_len_builtin = [this](token_ptr_slice_t id_slice) {
        return id_slice.len == 1
               && context.symbol_id(id_slice.start[0]) == context.symbol_id<"len">();
    };

    const Exec lhs_exec = context.exec(lhs_eid);

    if (lhs_exec.compt) {
        if (lhs_exec.holds<ExecListLiteral>() && rhs_expr->type == AST_EXPR_ID
            && matches_len_builtin(rhs_expr->expr.id.slice)) {
            return context.emplace_compt_exec(ExecConst{lhs_exec.as<ExecListLiteral>().len()},
                                              Span{context, fid, expr});
        }

        if (lhs_exec.holds<ExecConst>() && lhs_exec.as<ExecConst>().holds<SymbolId>()
            && rhs_expr->type == AST_EXPR_ID && matches_len_builtin(rhs_expr->expr.id.slice)) {
            return context.emplace_compt_exec(
                ExecConst{context.symbol(lhs_exec.as<ExecConst>().as<SymbolId>()).size()},
                Span{context, fid, expr});
        }

        if (lhs_exec.holds<ExecUnionInit>() && rhs_expr->type == AST_EXPR_ID) {
            {
                const Def& union_def = context.def(
                    def_visitor.visit_as_transparent(lhs_exec.as<ExecUnionInit>().union_def_id));
                assert(union_def.holds<DefUnion>());
            }
            token_ptr_slice_t id_slice = rhs_expr->expr.id.slice;
            if (id_slice.len > 1) {
                context.emplace_diagnostic(rhs_span, diag_code::scoped_identifer_not_allowed_here,
                                           diag_type::error);
            }

            auto maybe_mem_var
                = context.look_up_variable(context.def(lhs_exec.as<ExecUnionInit>().union_def_id)
                                               .template as<DefUnion>()
                                               .scope,
                                           context.symbol_id(id_slice.start[0]));

            if (maybe_mem_var.empty()) {
                const Def& union_def = context.def(lhs_exec.as<ExecUnionInit>().union_def_id);
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

            const Def& union_def = context.def(lhs_exec.as<ExecUnionInit>().union_def_id);
            const Def& var_def = context.def(maybe_mem_var.as_id());
            if (var_def.member_idx != lhs_exec.as<ExecUnionInit>().active_member_idx) {
                const Def& curr_member = context.def(union_def.as<DefUnion>().ordered_members.get(
                    lhs_exec.as<ExecUnionInit>().active_member_idx));
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

            const Exec& mem_exec = context.exec(lhs_exec.as<ExecUnionInit>().member_init);

            return context.emplace_compt_exec(mem_exec.value, Span{context, fid, expr});
        }
    }

    const auto maybe_lhs_tid = infer_type_from_exec(lhs_eid);
    if (maybe_lhs_tid.empty()) {
        return {};
    }
    const TypeId lhs_tid = maybe_lhs_tid.as_id();
    const Type lhs_ty = context.type(lhs_tid);

    const bool lhs_has_len
        = lhs_ty.holds<TypeSlice>() || lhs_ty.holds<TypeArr>()
          || (lhs_ty.holds<TypeBuiltin>() && lhs_ty.as<TypeBuiltin>().type == builtin_type::str);

    if (lhs_has_len && rhs_expr->type == AST_EXPR_FN_CALL
        && rhs_expr->expr.fn_call.left_expr->type == AST_EXPR_ID
        && matches_len_builtin(rhs_expr->expr.fn_call.left_expr->expr.id.slice)) {
        const auto sid = context.symbol_id<"len">();
        auto d0 = context.emplace_diagnostic_with_message_value(
            rhs_span, diag_code::tried_to_call_property, diag_type::error,
            DiagnosticSymbolAfterMessage{.sid = sid});
        auto d1 = context.emplace_diagnostic_with_message_value(
            rhs_span, diag_code::replace_with, diag_type::help,
            DiagnosticSymbolAfterMessage{.sid = sid});
        context.link_diagnostic(d0, d1);
        return {};
    }

    if (rhs_expr->type == AST_EXPR_FN_CALL) {
        return solve_fn_call_impl(*this, fid, lctx.scope, lctx.map, rhs_expr, lhs_eid);
    }

    if (lhs_has_len && rhs_expr->type == AST_EXPR_ID
        && matches_len_builtin(rhs_expr->expr.id.slice)) {
        if (lhs_ty.holds<TypeArr>()) {
            const size_t size = lhs_ty.as<TypeArr>().canonical_size;
            return context.emplace_compt_exec(
                ExecConst{context.pointer_size_bytes() == 8
                              ? ConstantValue{static_cast<uint64_t>(size)}
                              : ConstantValue{static_cast<uint32_t>(size)}},
                Span{context, fid, expr});
        }
        return context.emplace_exec(ExecBuiltinAccess{.from = lhs_eid,
                                                      .kind = lhs_ty.holds<TypeSlice>()
                                                                  ? builtin_access::slice_len
                                                                  : builtin_access::str_len},
                                    Span{context, fid, expr});
    }

    if (lhs_ty.holds<TypeUnion>() && rhs_expr->type == AST_EXPR_ID) {
        const DefId union_did = def_visitor.visit_as_independent(lhs_ty.as<TypeUnion>().def_id);
        if (!context.def(union_did).holds<DefUnion>()) {
            return {};
        }
        token_ptr_slice_t id_slice = rhs_expr->expr.id.slice;
        if (id_slice.len > 1) {
            context.emplace_diagnostic(rhs_span, diag_code::scoped_identifer_not_allowed_here,
                                       diag_type::error);
        }
        const auto maybe_mem_var = context.look_up_variable(
            context.def(union_did).as<DefUnion>().scope, context.symbol_id(id_slice.start[0]));
        if (maybe_mem_var.empty()) {
            const Def& union_def = context.def(union_did);
            auto d0 = context.emplace_diagnostic_with_message_value(
                Span{context, fid, id_slice}, diag_code::does_not_name_a_field_of_union,
                diag_type::error, DiagnosticSymbolAfterMessage{.sid = union_def.name});
            auto d1 = context.emplace_diagnostic_with_message_value(
                union_def.span, diag_code::declared_here, diag_type::note,
                DiagnosticSymbolBeforeMessage{.sid = union_def.name});
            context.link_diagnostic(d0, d1);
            return {};
        }
        if (!context.def(maybe_mem_var.as_id()).holds<DefVariable>()) {
            return {};
        }
        return context.emplace_exec(
            ExecMemberAccess{.owner = lhs_eid, .member = maybe_mem_var.as_id()},
            Span{context, fid, expr});
    }

    if (!lhs_ty.holds<TypeStruct>()) {
        auto d0
            = context.emplace_diagnostic(Span{context, fid, expr->expr.binary.op},
                                         diag_code::cannot_access_a_member_value, diag_type::error);
        auto d1 = context.emplace_diagnostic(
            lhs_exec.span, diag_code::value_is_of_type, diag_type::note,
            DiagnosticTypeAfterMessage{.tid = lhs_tid},
            DiagnosticSubCode{.sub_code = diag_code::is_not_a_struct});
        context.link_diagnostic(d0, d1);
        return {};
    }

    const DefId struct_did = def_visitor.visit_as_independent(lhs_ty.as<TypeStruct>().def_id);
    if (!context.def(struct_did).holds<DefStruct>()) {
        return {};
    }

    if (rhs_expr->type == AST_EXPR_ID) {
        token_ptr_slice_t id_slice = rhs_expr->expr.id.slice;
        if (id_slice.len > 1) {
            context.emplace_diagnostic(rhs_span, diag_code::scoped_identifer_not_allowed_here,
                                       diag_type::error);
        }

        const auto maybe_mem_var = context.look_up_member_var_guarding_hid(
            context.def(struct_did), context.symbol_id(id_slice.start[0]), rhs_span, lctx.scope);
        if (maybe_mem_var.empty()) {
            return {}; // poisoned
        }

        if (lhs_exec.compt && lhs_exec.holds<ExecStructInit>()) {
            const HirSize idx = context.def(maybe_mem_var.as_id()).member_idx;
            const Exec& mem_exec
                = context.exec(lhs_exec.as<ExecStructInit>().member_inits.get(idx));
            return context.emplace_compt_exec(mem_exec.value, Span{context, fid, expr});
        }

        return context.emplace_exec(
            ExecMemberAccess{.owner = lhs_eid, .member = maybe_mem_var.as_id()},
            Span{context, fid, expr});
    }

    const auto prior_diag_cnt = context.diagnostic_count();

    // try a member index look-up: foo.0, foo.1, etc
    const auto maybe_rhs = ComptExprSolver{def_visitor}.solve_builtin_compt_expr(
        fid, lctx.scope, rhs_expr, builtin_type::u32);

    if (maybe_rhs.has_value()) {
        const std::optional<HirSize> maybe_mem_idx
            = context.try_member_index(struct_did, maybe_rhs.as_id());
        if (maybe_mem_idx.has_value()) {
            if (lhs_exec.compt && lhs_exec.holds<ExecStructInit>()) {
                const Exec& mem_exec = context.exec(
                    lhs_exec.as<ExecStructInit>().member_inits.get(maybe_mem_idx.value()));
                return context.emplace_compt_exec(mem_exec.value, Span{context, fid, expr});
            }
            const DefId member_did = context.def_id(
                context.def(struct_did).as<DefStruct>().ordered_members.get(maybe_mem_idx.value()));
            if (!context.def(member_did).holds<DefVariable>()) {
                return {};
            }
            return context.emplace_exec(ExecMemberAccess{.owner = lhs_eid, .member = member_did},
                                        Span{context, fid, expr});
        }
    }

    // force link this if diagnostics were just emitted
    if (context.diagnostic_count() > prior_diag_cnt) {
        context.force_link_diagnostic(context.emplace_diagnostic(
            rhs_span, diag_code::value_does_not_refer_to_a_member, diag_type::note));
    } else {
        context.emplace_diagnostic(rhs_span, diag_code::value_does_not_refer_to_a_member,
                                   diag_type::error);
    }

    return {};
}

void RuntimeSolver::diag_not_yet_implemented(Span span) {
    context.emplace_diagnostic(span, diag_code::not_yet_implemented_in_a_runtime_context,
                               diag_type::error);
}

DiagRange RuntimeSolver::warn_short_circuit(FileId fid, const ast_expr_t* expr, bool always) {
    DiagLinker dl{context};
    dl.link(context.emplace_diagnostic_with_message_value(
        Span{context, fid, expr}, diag_code::expression_always_evaluates_to, diag_type::warning,
        DiagnosticSymbolAfterMessage{.sid = always ? context.symbol_id<"true">()
                                                   : context.symbol_id<"false">()}));
    dl.link(context.emplace_diagnostic(Span{context, fid, expr->expr.binary.rhs},
                                       diag_code::right_hand_side_is_never_evaluated,
                                       diag_type::note));
    return dl.range();
}

std::optional<DiagRange> RuntimeSolver::try_warn_suspicious_assignment_in_cond(ExecId eid) {
    const Exec& e = context.exec(eid);
    if (e.holds<ExecAssignment>()) {
        DiagLinker dl{context};
        dl.link(context.emplace_diagnostic(e.span, diag_code::suspicious_assignment_in_condition,
                                           diag_type::warning));
        const Exec& lhs = context.exec(e.as<ExecAssignment>().lhs);
        const Exec& rhs = context.exec(e.as<ExecAssignment>().rhs);
        std::string str{lhs.span.as_sv(context)};
        str += " == ";
        str += rhs.span.as_sv(context);
        const SymbolId sid = context.symbol_id(str);
        dl.link(context.emplace_diagnostic_with_message_value(
            e.span, diag_code::replace_with, diag_type::help,
            DiagnosticSymbolAfterMessage{.sid = sid}));
        return dl.range();
    }
    return {};
}

OptId<ExecId> RuntimeSolver::handle_arithmetic_logical_or_bool_binary(FileId fid, LexicalCtx lctx,
                                                                      const ast_expr_t* expr,
                                                                      binary_op op) {
    assert(expr->type == AST_EXPR_BINARY);

    bool cooked = false;

    OptId<ExecId> lhs = solve_expr(fid, lctx, expr->expr.binary.lhs);
    if (!lhs.has_value()) {
        cooked = true;
    }

    if (lhs.has_value() && (op == binary_op::bool_or || op == binary_op::bool_and)) {
        const Exec& lhs_exec = context.exec(lhs.as_id());
        if (lhs_exec.holds<ExecConst>()) {
            const std::optional<ExecConst> econst
                = lhs_exec.as<ExecConst>().try_safe_convert_to(context, builtin_type::boolean);
            if (econst.has_value()) {
                const bool bval = econst.value().as<bool>();
                // false && ____ => always false
                if ((op == binary_op::bool_and) && !bval) {
                    warn_short_circuit(fid, expr, false);
                    const auto _ = solve_expr(fid, lctx, expr->expr.binary.rhs);
                    return context.emplace_compt_exec(ExecConst{false}, Span{context, fid, expr});
                }
                // true || ____ => always true
                if ((op == binary_op::bool_or) && bval) {
                    warn_short_circuit(fid, expr, true);
                    const auto _ = solve_expr(fid, lctx, expr->expr.binary.rhs);
                    return context.emplace_compt_exec(ExecConst{true}, Span{context, fid, expr});
                }
            }
        }
    }

    OptId<ExecId> rhs = solve_expr(fid, lctx, expr->expr.binary.rhs);
    if (!rhs.has_value()) {
        cooked = true;
    }
    if (cooked) {
        return std::nullopt; // already poisoned
    }

    if (context.exec(lhs.as_id()).compt && context.exec(rhs.as_id()).compt) {
        return ComptExprSolver{def_visitor}.solve_binary_compt_exec(lhs.as_id(), op, rhs.as_id(),
                                                                    Span{context, fid, expr});
    }

    return solve_binary_exec(lhs.as_id(), op, rhs.as_id(), Span{context, fid, expr});
}

OptId<ExecId> RuntimeSolver::scale_ptr_offset(TypeId ptr_tid, ExecId offset_eid) {
    const LayoutSize width
        = context.layout_for_type(context.type(ptr_tid).as<TypePtr>().inner).width;
    if (width == 1) {
        return offset_eid;
    }
    const Span span = context.exec(offset_eid).span;
    const ExecId width_eid = context.emplace_compt_exec(
        ExecConst{context.pointer_size_bytes() == 8 ? ConstantValue{static_cast<uint64_t>(width)}
                                                    : ConstantValue{static_cast<uint32_t>(width)}},
        span);
    if (context.exec(offset_eid).compt) {
        return ComptExprSolver{def_visitor}.solve_binary_compt_exec(offset_eid, binary_op::multiply,
                                                                    width_eid, span);
    }
    return context.emplace_exec(
        ExecBinary{.lhs = offset_eid, .rhs = width_eid, .op = binary_op::multiply}, span);
}

bool RuntimeSolver::guard_compt_rhs_operand(builtin_type lhs_bt, binary_op op, ExecId rhs_eid) {
    const Exec& rhs = context.exec(rhs_eid);
    if (!rhs.compt || !rhs.holds<ExecConst>()) {
        return false;
    }
    const ExecConst rhs_val = rhs.as<ExecConst>();
    const bool integral = builtin_type_has_binary_op(lhs_bt, binary_op::bit_and);

    if ((op == binary_op::divide || op == binary_op::modulo) && integral && rhs_val.equals_zero()) {
        context.emplace_diagnostic(rhs.span, diag_code::dividing_by_zero, diag_type::error);
        return true;
    }

    if (op == binary_op::left_bitshift || op == binary_op::right_shift) {
        const HirSize bits = context.layout_for_type(context.type_for_builtin(lhs_bt)).width * 8;
        const auto maybe_amount = rhs_val.try_safe_convert_to(context, builtin_type::i64);
        if (!maybe_amount.has_value() || maybe_amount.value().as<int64_t>() < 0
            || std::cmp_greater_equal(maybe_amount.value().as<int64_t>(), bits)) {
            context.emplace_diagnostic_with_message_value(
                rhs.span, diag_code::shift_amount_out_of_range_at_compt_is_illegal_for_type,
                diag_type::error,
                DiagnosticSymbolAfterMessage{.sid
                                             = context.symbol_id(builtin_type_to_cstr(lhs_bt))});
            return true;
        }
    }
    return false;
}

[[nodiscard]] static bool type_may_contain_float(Context& context, TypeId tid) {
    const Type& ty = context.type(tid);
    const auto members_may_contain_float = [&context](auto mems) {
        for (auto didx = mems.begin(); didx != mems.end(); ++didx) {
            const auto& def = context.def(didx);
            if (def.template holds<DefVariable>()
                && type_may_contain_float(context, def.template as<DefVariable>().type_id)) {
                return true;
            }
        }
        return false;
    };
    return ty.visit(Ovld{
        [](const TypeBuiltin& t) -> bool {
            return t.type == builtin_type::f32 || t.type == builtin_type::f64;
        },
        [&context, &members_may_contain_float](const TypeStruct& t) -> bool {
            return t.gen_args_slice.has_value()
                   || members_may_contain_float(context.ordered_defs_for(t.def_id));
        },
        [&context, &members_may_contain_float](const TypeVariant& t) -> bool {
            if (t.generic || t.gen_args_slice.has_value()) {
                return true;
            }
            const auto fields = context.ordered_defs_for(t.def_id);
            for (auto field_didx = fields.begin(); field_didx != fields.end(); ++field_didx) {
                const auto& field = context.def(field_didx);
                if (field.holds<DefVariantField>()
                    && members_may_contain_float(field.as<DefVariantField>().members)) {
                    return true;
                }
            }
            return false;
        },
        [&context, &members_may_contain_float](const TypeUnion& t) -> bool {
            return members_may_contain_float(context.ordered_defs_for(t.def_id));
        },
        [&context](const TypeDeftype& t) -> bool {
            return type_may_contain_float(context, t.true_type);
        },
        [&context](const TypeArr& t) -> bool { return type_may_contain_float(context, t.inner); },
        [&context](const TypeRef& t) -> bool { return type_may_contain_float(context, t.inner); },
        [&context](const TypeSlice& t) -> bool { return type_may_contain_float(context, t.inner); },
        [](const TypePtr&) -> bool { return false; },
        [](const TypeFnPtr&) -> bool { return false; },
        [](const TypeVar&) -> bool { return true; },
    });
}

OptId<ExecId> RuntimeSolver::solve_binary_exec(ExecId lhs_eid, binary_op op, ExecId rhs_eid,
                                               Span span) {
    const bool is_bool_conj = op == binary_op::bool_and || op == binary_op::bool_or;
    const bool is_range = op == binary_op::range_exclusive || op == binary_op::range_inclusive;

    const auto try_coerce_compt = [this](ExecId eid, TypeId target_tid) -> ExecId {
        const Exec& exec = context.exec(eid);
        const Type& target = context.type(target_tid);
        if (!exec.compt || !exec.holds<ExecConst>() || !target.holds<TypeBuiltin>()) {
            return eid;
        }
        const auto maybe_converted
            = exec.as<ExecConst>().try_safe_convert_to(context, target.as<TypeBuiltin>().type);
        if (!maybe_converted.has_value()) {
            return eid;
        }
        return context.emplace_compt_exec(maybe_converted.value(), exec.span);
    };

    {
        const auto maybe_lhs_tid = infer_type_from_exec(lhs_eid);
        const auto maybe_rhs_tid = infer_type_from_exec(rhs_eid);
        if (maybe_lhs_tid.empty() || maybe_rhs_tid.empty()) {
            return {};
        }
        if (is_bool_conj) {
            const TypeId bool_tid = context.type_for_builtin(builtin_type::boolean);
            lhs_eid = try_coerce_compt(lhs_eid, bool_tid);
            rhs_eid = try_coerce_compt(rhs_eid, bool_tid);
        } else if (context.exec(lhs_eid).compt) {
            lhs_eid = try_coerce_compt(lhs_eid, maybe_rhs_tid.as_id());
        } else if (context.exec(rhs_eid).compt) {
            rhs_eid = try_coerce_compt(rhs_eid, maybe_lhs_tid.as_id());
        }
    }

    const auto maybe_lhs_tid = infer_type_from_exec(lhs_eid);
    const auto maybe_rhs_tid = infer_type_from_exec(rhs_eid);
    if (maybe_lhs_tid.empty() || maybe_rhs_tid.empty()) {
        return {};
    }
    const TypeId lhs_tid = maybe_lhs_tid.as_id();
    const TypeId rhs_tid = maybe_rhs_tid.as_id();
    const Type lhs_ty = context.type(lhs_tid);
    const Type rhs_ty = context.type(rhs_tid);

    const auto is_builtin = +[](const Type& ty, builtin_type bt) {
        return ty.holds<TypeBuiltin>() && ty.as<TypeBuiltin>().type == bt;
    };

    const auto link_cast_help = [this](DiagLinker& dl, ExecId eid, TypeId target_tid) {
        if (!context.exec(eid).compt) {
            dl.link(context.emplace_diagnostic_with_message_value(
                context.exec(eid).span, diag_code::add_an_explicit_cast_to, diag_type::help,
                DiagnosticTypeAfterMessage{.tid = target_tid}));
            std::string str{'('};
            str += context.exec(eid).span.as_sv(context);
            str += " as ";
            str += type_to_string(context, target_tid);
            str += ')';
            dl.link(context.emplace_diagnostic_with_message_value(
                context.exec(eid).span, diag_code::replace_with, diag_type::help,
                DiagnosticSymbolAfterMessage{.sid = context.symbol_id(str)}));
        }
    };

    const auto is_always_true_or_always_false_bool_eq_neq = [this, lhs_eid, rhs_eid, op, lhs_tid] {
        if (bin_op_is_eq_neq(op) && !type_may_contain_float(context, lhs_tid)) {
            const bool neither_has_side_effect = !exec_may_have_side_effects(context, lhs_eid)
                                                 && !exec_may_have_side_effects(context, rhs_eid);
            const bool both_sides_equivalent
                = explicitly_equivalent_exec(context, lhs_eid, rhs_eid);
            return neither_has_side_effect && both_sides_equivalent;
        }
        return false;
    };

    const auto warn_and_emplace_always_true_or_always_false_bool_eq_neq = [this, op, &span] {
        const bool always_true = op == binary_op::bool_equal;
        context.emplace_diagnostic_with_message_value(
            span, diag_code::expression_always_evaluates_to, diag_type::warning,
            DiagnosticSymbolAfterMessage{.sid = always_true ? context.symbol_id<"true">()
                                                            : context.symbol_id<"false">()});
        return context.emplace_compt_exec(ExecComptConstant{always_true}, span);
    };

    if (is_bool_conj) {
        bool cooked = false;
        for (const auto& [eid, ty] : {std::pair{lhs_eid, lhs_ty}, std::pair{rhs_eid, rhs_ty}}) {
            if (!is_builtin(ty, builtin_type::boolean)) {
                context.emplace_diagnostic_with_message_value(
                    context.exec(eid).span, diag_code::value_is_not_contextually_convertible_to,
                    diag_type::error,
                    DiagnosticTypeAfterMessage{.tid
                                               = context.type_for_builtin(builtin_type::boolean)});
                cooked = true;
            }
        }
        if (cooked) {
            return {};
        }
        return context.emplace_exec(ExecBinary{.lhs = lhs_eid, .rhs = rhs_eid, .op = op}, span);
    }

    const auto diag_op_not_viable = [&]() {
        context.emplace_diagnostic_with_message_value(
            span, diag_code::incompatible_types_for_binary_operator_colon, diag_type::error,
            DiagnosticTypeAndTypeForBinaryOp{.lhs_tid = lhs_tid, .rhs_tid = rhs_tid, .op = op});
    };

    if ((op == binary_op::plus || op == binary_op::minus) && lhs_ty.holds<TypePtr>()) {
        const Type& pointee = context.type(lhs_ty.as<TypePtr>().inner);
        if (is_builtin(pointee, builtin_type::voidd)) {
            diag_op_not_viable();
            return {};
        }
        const TypeId usize_tid = context.usize_type();
        rhs_eid = try_coerce_compt(rhs_eid, usize_tid);
        const auto maybe_offset_tid = infer_type_from_exec(rhs_eid);
        if (maybe_offset_tid.empty()) {
            return {};
        }
        const Type& offset_ty = context.type(maybe_offset_tid.as_id());
        if (!is_builtin(offset_ty, context.builtin_usize_type())) {
            DiagLinker dl{context};
            dl.link(context.emplace_diagnostic_with_message_value(
                span, diag_code::incompatible_types_for_binary_expression, diag_type::error,
                DiagnosticTypeAndType{.lhs_tid = lhs_tid, .rhs_tid = maybe_offset_tid.as_id()}));
            if (offset_ty.holds<TypeBuiltin>()
                && builtin_type_has_binary_op(offset_ty.as<TypeBuiltin>().type,
                                              binary_op::bit_and)) {
                link_cast_help(dl, rhs_eid, usize_tid);
            }
            return {};
        }
        const auto maybe_scaled_eid = scale_ptr_offset(lhs_tid, rhs_eid);
        if (maybe_scaled_eid.empty()) {
            return {};
        }
        return context.emplace_exec(
            ExecBinary{.lhs = lhs_eid, .rhs = maybe_scaled_eid.as_id(), .op = op}, span);
    }

    if (bin_op_is_eq_neq(op) && (lhs_ty.holds<TypePtr>() || rhs_ty.holds<TypePtr>())) {
        const bool viable
            = (lhs_ty.holds<TypePtr>() && rhs_ty.holds<TypePtr>()
               && context.equivalent_type(lhs_ty.as<TypePtr>().inner, rhs_ty.as<TypePtr>().inner))
              || (lhs_ty.holds<TypePtr>() && is_builtin(rhs_ty, builtin_type::nullpointer))
              || (rhs_ty.holds<TypePtr>() && is_builtin(lhs_ty, builtin_type::nullpointer));
        if (!viable) {
            diag_op_not_viable();
            return {};
        }
        if (is_always_true_or_always_false_bool_eq_neq()) {
            return warn_and_emplace_always_true_or_always_false_bool_eq_neq();
        }

        return context.emplace_exec(ExecBinary{.lhs = lhs_eid, .rhs = rhs_eid, .op = op}, span);
    }

    if (!lhs_ty.holds<TypeBuiltin>() || !rhs_ty.holds<TypeBuiltin>()) {
        if (bin_op_is_eq_neq(op)
            && (context.assignable_from_type_to(rhs_tid, lhs_tid)
                || context.assignable_from_type_to(lhs_tid, rhs_tid))) {
            if (is_always_true_or_always_false_bool_eq_neq()) {
                return warn_and_emplace_always_true_or_always_false_bool_eq_neq();
            }
            return context.emplace_exec(ExecBinary{.lhs = lhs_eid, .rhs = rhs_eid, .op = op}, span);
        }
        diag_op_not_viable();
        return {};
    }

    const builtin_type lhs_bt = lhs_ty.as<TypeBuiltin>().type;
    const builtin_type rhs_bt = rhs_ty.as<TypeBuiltin>().type;

    if (lhs_bt != rhs_bt) {
        DiagLinker dl{context};
        dl.link(context.emplace_diagnostic_with_message_value(
            span, diag_code::incompatible_types_for_binary_expression, diag_type::error,
            DiagnosticTypeAndType{.lhs_tid = lhs_tid, .rhs_tid = rhs_tid}));
        link_cast_help(dl, rhs_eid, context.type_for_builtin(lhs_bt));
        return {};
    }

    if (!builtin_type_has_binary_op(lhs_bt, op)
        || (lhs_bt == builtin_type::str && op == binary_op::plus)) {
        diag_op_not_viable();
        return {};
    }

    if (guard_compt_rhs_operand(lhs_bt, op, rhs_eid)) {
        return {};
    }

    if (is_range) {
        return context.emplace_exec(ExecRange{.start = lhs_eid,
                                              .end = rhs_eid,
                                              .inclusive = op == binary_op::range_inclusive},
                                    span);
    }

    if (is_always_true_or_always_false_bool_eq_neq()) {
        return warn_and_emplace_always_true_or_always_false_bool_eq_neq();
    }

    return context.emplace_exec(ExecBinary{.lhs = lhs_eid, .rhs = rhs_eid, .op = op}, span);
}

ExecId RuntimeSolver::emplace_block_exec(const InProgressBlock& block, LexicalCtx lctx, Span span) {
    return context.emplace_exec(
        ExecBlock{context.emplace_block(Block{.execs = context.freeze_id_vec(block.execs),
                                              .defs = context.freeze_id_vec(block.defs),
                                              .lctx = lctx})},
        span);
}

DefId RuntimeSolver::emplace_hidden_local(InProgressBlock& block, SymbolId name, TypeId tid,
                                          Span span, OptId<ExecId> maybe_init_eid) {
    const DefId did
        = context.register_def(name, false, false, 0, span, nullptr, DefVariable{.type_id = tid});
    context.promote_mention_state_of(did, Def::mention_state::used);
    locals.push_back(did);
    block.push_back_def(did);
    if (maybe_init_eid.has_value()) {
        block.push_back_exec(context.emplace_exec(
            ExecAssignment{.lhs = context.mark_as_mutated(context.emplace_exec(
                               ExecVariable{.def_id = did, .type_id = tid}, span)),
                           .rhs = maybe_init_eid.as_id()},
            span));
    }
    return did;
}

void RuntimeSolver::diag_not_iterable(Span span, OptId<TypeId> maybe_tid, diag_code help) {
    DiagLinker dl{context};
    dl.link(context.emplace_diagnostic(span, diag_code::value_is_not_iterable, diag_type::error));
    if (maybe_tid.has_value()) {
        dl.link(context.emplace_diagnostic_with_message_value(
            span, diag_code::value_is_of_type, diag_type::note,
            DiagnosticTypeAfterMessage{.tid = maybe_tid.as_id()}));
    }
    dl.link(context.emplace_diagnostic(span, help, diag_type::help));
}

bool RuntimeSolver::bind_for_each_var(FileId fid, LexicalCtx body_lctx, InProgressBlock& prelude,
                                      const ast_param_t* param, ExecId elem_eid, for_each_src src,
                                      ExecId iterable_eid) {
    const auto maybe_written_tid = resolve_type(fid, body_lctx, param->type);
    if (maybe_written_tid.empty()) {
        return false;
    }
    const auto maybe_elem_tid = infer_type_from_exec(elem_eid);
    if (maybe_elem_tid.empty()) {
        return false;
    }
    const TypeId written_tid = maybe_written_tid.as_id();
    const TypeId elem_tid = maybe_elem_tid.as_id();
    const Type& written_ty = context.type(written_tid);
    const SymbolId name = context.symbol_id(param->name);
    const Span span{context, fid, param->first, param->last};
    const bool by_ref = src != for_each_src::range && written_ty.holds<TypeRef>()
                        && !context.type(elem_tid).holds<TypeRef>()
                        && context.assignable_and_inferable_from_type_to(
                            elem_tid, written_ty.as<TypeRef>().inner);

    TypeId tid = written_tid;
    OptId<ExecId> maybe_rhs_eid{};
    bool ok = true;
    if (by_ref) {

        const bool written_ty_contains_var = TypeTransformer<TypeContainsVar>{context}(written_tid);
        maybe_rhs_eid = try_checked_borrow(context, elem_eid, written_ty.mut, span);

        if (maybe_rhs_eid.has_value() && written_ty_contains_var) {
            const auto maybe_inferred_tid = infer_type_from_exec(maybe_rhs_eid.as_id());
            if (maybe_inferred_tid.empty()) {
                return false;
            }
            tid = maybe_inferred_tid.as_id();
        }
    } else if (context.assignable_and_inferable_from_type_to(elem_tid, written_tid)) {
        maybe_rhs_eid = elem_eid;
    } else {
        DiagLinker dl{context};
        dl.link(context.emplace_diagnostic_with_message_value(
            span, diag_code::cannot_convert_value_of_type, diag_type::error,
            DiagnosticTypeToType{.from = elem_tid, .to = written_tid}));
        const Exec& iterable_exec = context.exec(iterable_eid);
        if (const auto maybe_inferred_tid = infer_type_from_exec(iterable_eid)) {
            dl.link(context.emplace_diagnostic_with_message_value(
                iterable_exec.span, diag_code::iterable_value_is_of_type, diag_type::note,
                DiagnosticTypeAfterMessage{.tid = maybe_inferred_tid.as_id()}));
            if (const auto maybe_did = context.try_def_for_type(maybe_inferred_tid.as_id())) {
                const Def& d = context.def(maybe_did.as_id());
                dl.link(context.emplace_diagnostic_with_message_value(
                    d.span, diag_code::declared_here, diag_type::note,
                    DiagnosticTypeBeforeMessage{.tid = maybe_inferred_tid.as_id()}));
            }
        }
        ok = false;
    }

    if (maybe_rhs_eid.has_value() && !by_ref && src == for_each_src::indexable) {
        maybe_rhs_eid = consume(body_lctx, maybe_rhs_eid.as_id());
    }
    ok &= maybe_rhs_eid.has_value();

    if (TypeTransformer<TypeContainsVar>{context}(tid)) {
        // fill in the var from the element type, keeping the written top-level mut
        if (context.type(written_tid).mut == context.type(elem_tid).mut) {
            tid = elem_tid;
        } else {
            tid = context.emplace_type(context.type(elem_tid).value, span, written_ty.mut);
        }
    }

    const DefId did
        = context.register_def(name, false, false, 0, span, nullptr, DefVariable{.type_id = tid});
    context.insert_variable(body_lctx.scope, name, did);
    prelude.push_back_def(did);
    locals.push_back(did);

    // push_back regardless of `ok` to minimize cascades
    if (maybe_rhs_eid.has_value()) {
        prelude.push_back_exec(context.emplace_exec(
            ExecAssignment{.lhs = context.mark_as_mutated(context.emplace_exec(
                               ExecVariable{.def_id = did, .type_id = tid}, span)),
                           .rhs = maybe_rhs_eid.as_id()},
            span));
    }
    return ok;
}

OptId<ExecId> RuntimeSolver::handle_for_in(FileId fid, LexicalCtx lctx, InProgressBlock& block,
                                           const ast_stmt_t* stmt) {
    assert(stmt->type == AST_STMT_FOR_IN);
    const ast_stmt_for_in_t for_in = stmt->stmt.for_in_stmt;
    if (!for_in.each || !for_in.each->valid || !for_in.iterator || !for_in.body_stmt
        || for_in.body_stmt->type != AST_STMT_BLOCK) {
        return {};
    }
    if (for_in.compt) {
        return handle_for_compt(fid, lctx, block, stmt);
    }

    const Span span{context, fid, stmt};
    const LexicalCtx for_lctx{.scope = context.make_small_scope(lctx.scope, span), .map = lctx.map};
    InProgressBlock for_block{};

    OptId<ExecId> maybe_loop_eid{};
    const auto maybe_iterable_eid = solve_place(fid, for_lctx, for_in.iterator);
    const auto maybe_iterable_tid = maybe_iterable_eid.has_value()
                                        ? infer_type_from_exec(maybe_iterable_eid.as_id())
                                        : OptId<TypeId>{};
    if (maybe_iterable_tid.empty()) {
        maybe_loop_eid = solve_non_iterable_for_in(fid, for_lctx, stmt);
    } else if (context.exec(maybe_iterable_eid.as_id()).holds<ExecRange>()) {
        maybe_loop_eid
            = solve_range_for_in(fid, for_lctx, for_block, stmt, maybe_iterable_eid.as_id());
    } else if (const Type& decayed_ty = context.type(context.try_decay(maybe_iterable_tid.as_id()));
               decayed_ty.holds_any_of<TypeArr, TypeSlice>()
               || (decayed_ty.holds<TypeBuiltin>()
                   && decayed_ty.as<TypeBuiltin>().type == builtin_type::str)) {
        maybe_loop_eid
            = solve_indexable_for_in(fid, for_lctx, for_block, stmt, maybe_iterable_eid.as_id());
    } else if (context.type(maybe_iterable_tid.as_id()).holds<TypeStruct>()) {
        maybe_loop_eid
            = solve_iterator_for_in(fid, for_lctx, for_block, stmt, maybe_iterable_eid.as_id());
    } else {
        diag_not_iterable(context.exec(maybe_iterable_eid.as_id()).span, maybe_iterable_tid.as_id(),
                          diag_code::for_in_iterates_over_iterators_arrays_slices_strs_and_ranges);
        maybe_loop_eid = solve_non_iterable_for_in(fid, for_lctx, stmt);
    }

    if (maybe_loop_eid.empty()) {
        return {};
    }
    for_block.push_back_exec(maybe_loop_eid.as_id());
    block.push_back_exec(emplace_block_exec(for_block, for_lctx, span));
    return maybe_loop_eid;
}

OptId<ExecId> RuntimeSolver::solve_non_iterable_for_in(FileId fid, LexicalCtx lctx,
                                                       const ast_stmt_t* stmt) {
    // the iterable has already been diagnosed as not iterable, so code gen will never run; still
    // solve the body (with the each var bound to its written type, implicit or not, and left
    // uninitialized) to surface its diagnostics. the loop itself is a cooked dummy.
    const ast_stmt_t* body_stmt = stmt->stmt.for_in_stmt.body_stmt;
    const ast_param_t* each = stmt->stmt.for_in_stmt.each;
    const auto maybe_written_tid = resolve_type(fid, lctx, each->type);
    if (maybe_written_tid.empty()) {
        return {};
    }
    const TypeId tid = maybe_written_tid.as_id();
    const Span span{context, fid, stmt};
    const Span each_span{context, fid, each->first, each->last};
    const ExecId false_eid = context.emplace_compt_exec(ExecConst{ConstantValue{false}}, span);
    return solve_loop_core(fid, lctx, span, false_eid, true, nullptr, body_stmt,
                           [&](ExecId, LexicalCtx body_lctx, InProgressBlock& prelude) -> bool {
                               const SymbolId name = context.symbol_id(each->name);
                               const DefId did
                                   = context.register_def(name, false, false, 0, each_span, nullptr,
                                                          DefVariable{.type_id = tid});
                               context.insert_variable(body_lctx.scope, name, did);
                               prelude.push_back_def(did);
                               locals.push_back(did);
                               return false;
                           });
}

OptId<ExecId> RuntimeSolver::solve_range_for_in(FileId fid, LexicalCtx lctx, InProgressBlock& block,
                                                const ast_stmt_t* stmt, ExecId range_eid) {
    const ast_stmt_for_in_t for_in = stmt->stmt.for_in_stmt;
    const Span span{context, fid, stmt};
    const Span range_span = context.exec(range_eid).span;
    const ExecRange range = context.exec(range_eid).as<ExecRange>();
    const bool inclusive = range.inclusive;

    const auto maybe_written_tid = resolve_type(fid, lctx, for_in.each->type);
    if (maybe_written_tid.empty()) {
        return {};
    }
    OptId<ExecId> maybe_start_eid = range.start;
    OptId<ExecId> maybe_end_eid = range.end;
    if (const Type& written_ty = context.type(maybe_written_tid.as_id());
        written_ty.holds<TypeBuiltin>() && context.exec(range.start).holds<ExecConst>()
        && context.exec(range.end).holds<ExecConst>()) {
        const TypeId into_tid = context.emplace_type(written_ty.value, written_ty.span, false);
        const auto maybe_start_conv = context.try_convert_to(range.start, into_tid);
        const auto maybe_end_conv = context.try_convert_to(range.end, into_tid);
        if (maybe_start_conv.has_value() && maybe_end_conv.has_value()) {
            maybe_start_eid = maybe_start_conv;
            maybe_end_eid = maybe_end_conv;
        }
    }
    const auto maybe_start_tid = infer_type_from_exec(maybe_start_eid.as_id());
    if (maybe_start_tid.empty()) {
        return {};
    }
    const Type& start_ty = context.type(maybe_start_tid.as_id());
    const TypeId elem_tid = context.emplace_type(start_ty.value, start_ty.span, false);

    std::optional<ConstantValue> maybe_one{};
    if (const Type& elem_ty = context.type(elem_tid); elem_ty.holds<TypeBuiltin>()) {
        maybe_one = inc_dec_one_for(elem_ty.as<TypeBuiltin>().type);
    }
    if (!maybe_one.has_value() || !ExecConst{maybe_one.value()}.is_integral()) {
        diag_not_iterable(range_span, elem_tid,
                          diag_code::for_in_iterates_over_iterators_arrays_slices_strs_and_ranges);
        return solve_non_iterable_for_in(fid, lctx, stmt);
    }

    const Span start_span = context.exec(maybe_start_eid.as_id()).span;
    const Span end_span = context.exec(maybe_end_eid.as_id()).span;
    const TypeId idx_tid = context.emplace_type(context.type(elem_tid).value, start_span, true);
    const DefId idx_did = emplace_hidden_local(block, context.symbol_id<"for index">(), idx_tid,
                                               start_span, maybe_start_eid);
    const DefId end_did = emplace_hidden_local(block, context.symbol_id<"for end">(), elem_tid,
                                               end_span, maybe_end_eid);
    const auto idx_var = [&] {
        return context.emplace_exec(ExecVariable{.def_id = idx_did, .type_id = idx_tid},
                                    start_span);
    };
    const auto idx_val = [&] {
        return context.emplace_exec(ExecVariable{.def_id = idx_did, .type_id = elem_tid},
                                    start_span);
    };
    const auto end_var = [&] {
        return context.emplace_exec(ExecVariable{.def_id = end_did, .type_id = elem_tid}, end_span);
    };
    const auto inc_idx = [&] {
        return context.emplace_exec(
            ExecCompoundAssignment{
                .lhs = idx_var(),
                .rhs = context.emplace_compt_exec(ExecConst{maybe_one.value()}, range_span),
                .op = binary_op::plus,
                .yield_old = false},
            range_span);
    };
    const auto idx_less_than_end = [&](binary_op op) {
        return context.emplace_exec(ExecBinary{.lhs = idx_var(), .rhs = end_var(), .op = op},
                                    range_span);
    };

    if (!inclusive) {
        return solve_loop_core(
            fid, lctx, span, idx_less_than_end(binary_op::less_than), false, nullptr,
            for_in.body_stmt, [&](ExecId, LexicalCtx body_lctx, InProgressBlock& prelude) -> bool {
                const bool ok = bind_for_each_var(fid, body_lctx, prelude, for_in.each, idx_val(),
                                                  for_each_src::range, range_eid);
                prelude.push_back_exec(inc_idx());
                return ok;
            });
    }

    const TypeId go_tid = context.emplace_type(
        context.type(context.type_for_builtin(builtin_type::boolean)).value, range_span, true);
    const DefId go_did
        = emplace_hidden_local(block, context.symbol_id<"for continue">(), go_tid, range_span,
                               idx_less_than_end(binary_op::less_than_or_equal));
    const auto go_var = [&] {
        return context.emplace_exec(ExecVariable{.def_id = go_did, .type_id = go_tid}, range_span);
    };

    return solve_loop_core(
        fid, lctx, span, go_var(), false, nullptr, for_in.body_stmt,
        [&](ExecId, LexicalCtx body_lctx, InProgressBlock& prelude) -> bool {
            const bool ok = bind_for_each_var(fid, body_lctx, prelude, for_in.each, idx_val(),
                                              for_each_src::range, range_eid);
            InProgressBlock then_block{};
            then_block.push_back_exec(inc_idx());
            InProgressBlock else_block{};
            else_block.push_back_exec(context.emplace_exec(
                ExecAssignment{
                    .lhs = context.mark_as_mutated(go_var()),
                    .rhs = context.emplace_compt_exec(ExecConst{ConstantValue{false}}, range_span)},
                range_span));
            prelude.push_back_exec(context.emplace_exec(
                ExecBranch{.condition = idx_less_than_end(binary_op::less_than),
                           .then_block = emplace_block_exec(then_block, body_lctx, range_span),
                           .else_block = emplace_block_exec(else_block, body_lctx, range_span)},
                range_span));
            return ok;
        });
}

OptId<ExecId> RuntimeSolver::solve_indexable_for_in(FileId fid, LexicalCtx lctx,
                                                    InProgressBlock& block, const ast_stmt_t* stmt,
                                                    ExecId seq_eid) {
    const ast_stmt_for_in_t for_in = stmt->stmt.for_in_stmt;
    const Span span{context, fid, stmt};
    const Span seq_span = context.exec(seq_eid).span;
    const TypeId seq_tid = infer_type_from_exec(seq_eid).as_id();
    const Type seq_ty = context.type(seq_tid);

    TypeId var_tid = seq_tid;
    bool deref = false;
    OptId<ExecId> maybe_init_eid{};
    if (seq_ty.holds<TypeRef>()) {
        deref = true;
        maybe_init_eid = seq_eid;
    } else if (seq_ty.holds_any_of<TypeSlice, TypeBuiltin>() || !context.is_lvalue(seq_eid)) {
        maybe_init_eid = consume(lctx, seq_eid);
    } else {
        deref = true;
        maybe_init_eid = try_checked_borrow(context, seq_eid, seq_ty.mut, seq_span);
        if (maybe_init_eid.has_value()) {
            const auto maybe_ref_tid = infer_type_from_exec(maybe_init_eid.as_id());
            if (maybe_ref_tid.empty()) {
                return {};
            }
            var_tid = maybe_ref_tid.as_id();
        }
    }
    if (maybe_init_eid.empty()) {
        return {};
    }

    const DefId seq_did = emplace_hidden_local(block, context.symbol_id(seq_span), var_tid,
                                               seq_span, maybe_init_eid);
    const bool deref_mut = deref && context.type(var_tid).mut;
    const auto seq_base = [&] {
        ExecId eid
            = context.emplace_exec(ExecVariable{.def_id = seq_did, .type_id = var_tid}, seq_span);
        if (deref) {
            eid = context.emplace_exec(ExecDeref{.dereferenced = eid, .mut = deref_mut}, seq_span);
        }
        return eid;
    };

    const builtin_type usize_bt = context.builtin_usize_type();
    const auto usize_const = [&](uint64_t value) {
        return context.emplace_compt_exec(
            ExecConst{ConstantValue{value}}.try_cast_to(context, usize_bt).value(), seq_span);
    };

    const Type& container_ty = context.type(context.try_decay(var_tid));
    const ExecId len_eid
        = container_ty.holds<TypeArr>()
              ? usize_const(static_cast<uint64_t>(container_ty.as<TypeArr>().canonical_size))
              : context.emplace_exec(ExecBuiltinAccess{.from = seq_base(),
                                                       .kind = container_ty.holds<TypeSlice>()
                                                                   ? builtin_access::slice_len
                                                                   : builtin_access::str_len},
                                     seq_span);

    const TypeId idx_tid
        = context.emplace_type(context.type(context.usize_type()).value, seq_span, true);
    const DefId idx_did = emplace_hidden_local(block, context.symbol_id<"for index">(), idx_tid,
                                               seq_span, usize_const(0));
    const auto idx_var = [&] {
        return context.emplace_exec(ExecVariable{.def_id = idx_did, .type_id = idx_tid}, seq_span);
    };

    const ExecId cond_eid = context.emplace_exec(
        ExecBinary{.lhs = idx_var(), .rhs = len_eid, .op = binary_op::less_than}, seq_span);

    return solve_loop_core(fid, lctx, span, cond_eid, false, nullptr, for_in.body_stmt,
                           [&](ExecId, LexicalCtx body_lctx, InProgressBlock& prelude) -> bool {
                               const ExecId elem_eid = context.emplace_exec(
                                   ExecSubscript{.base = seq_base(), .index = idx_var()}, seq_span);
                               const bool ok
                                   = bind_for_each_var(fid, body_lctx, prelude, for_in.each,
                                                       elem_eid, for_each_src::indexable, seq_eid);
                               prelude.push_back_exec(context.emplace_exec(
                                   ExecCompoundAssignment{.lhs = idx_var(),
                                                          .rhs = usize_const(1),
                                                          .op = binary_op::plus,
                                                          .yield_old = false},
                                   seq_span));
                               return ok;
                           });
}

OptId<ExecId> RuntimeSolver::solve_iterator_for_in(FileId fid, LexicalCtx lctx,
                                                   InProgressBlock& block, const ast_stmt_t* stmt,
                                                   ExecId iterable_eid) {
    const ast_stmt_for_in_t for_in = stmt->stmt.for_in_stmt;
    const Span span{context, fid, stmt};
    const Span iterable_span = context.exec(iterable_eid).span;
    const TypeId iterable_tid = infer_type_from_exec(iterable_eid).as_id();
    const DefId struct_did
        = def_visitor.visit_as_independent(context.type(iterable_tid).as<TypeStruct>().def_id);
    if (!context.def(struct_did).holds<DefStruct>()) {
        return {};
    }

    const auto fail = [&] -> OptId<ExecId> {
        const bool is_range = context.def(struct_did).name == context.symbol_id<"Range">()
                              && context.is_intrinsic(struct_did);
        diag_not_iterable(
            iterable_span, iterable_tid,
            is_range ? diag_code::write_the_range_directly_in_the_for_statement
                     : diag_code::for_in_iterates_over_iterators_arrays_slices_strs_and_ranges);
        return solve_non_iterable_for_in(fid, lctx, stmt);
    };

    OptId<DefId> maybe_next_did = context.look_up_variable(
        context.def(struct_did).as<DefStruct>().scope, context.symbol_id<"next">());
    if (maybe_next_did.has_value()) {
        maybe_next_did
            = context.try_func_did(def_visitor.visit_and_resolve_if_needed(maybe_next_did.as_id()));
    }
    if (maybe_next_did.empty()) {
        return fail();
    }
    const DefId next_did = def_visitor.visit_as_dependent(maybe_next_did.as_id());
    if (!context.def(next_did).holds<DefFunction>()) {
        return fail();
    }
    const DefFunction next_fn = context.def(next_did).as<DefFunction>();
    if (next_fn.return_type.empty() || next_fn.params.len() != 1) {
        return fail();
    }
    const Type self_ty = context.type(context.def(next_fn.params.get(0)).as<DefVariable>().type_id);
    if (!self_ty.holds<TypeRef>()) {
        return fail();
    }

    const TypeId opt_tid = next_fn.return_type.as_id();
    if (!context.type(opt_tid).holds<TypeVariant>()) {
        return fail();
    }
    const DefId opt_did
        = def_visitor.visit_as_independent(context.type(opt_tid).as<TypeVariant>().def_id);
    if (!context.def(opt_did).holds<DefVariant>()) {
        return fail();
    }
    const auto maybe_some_did = context.look_up_scoped_type(
        context.def(opt_did).as<DefVariant>().scope,
        context.freeze_id_vec({context.symbol_id<"Some">()}), iterable_span);
    if (maybe_some_did.empty() || !context.def(maybe_some_did.as_id()).holds<DefVariantField>()
        || context.def(maybe_some_did.as_id()).as<DefVariantField>().members.len() != 1) {
        return fail();
    }
    const DefId some_did = maybe_some_did.as_id();
    const DefId payload_did
        = context.def_id(context.def(some_did).as<DefVariantField>().members.get(0));
    const TypeId item_tid = context.def(payload_did).as<DefVariable>().type_id;

    const OptId<DefId> maybe_iterator_did = context.look_up_scoped_type_generic(
        def_visitor, context.root_scope(),
        context.freeze_id_vec({context.symbol_id<"std">(), context.symbol_id<"Iterator">()}),
        Span::generated(),
        context.emplace_generic_arg_id_slice(
            context.freeze_id_vec({context.emplace_generic_arg(item_tid)})));
    if (maybe_iterator_did.empty()
        || !context.struct_has_contract(
            struct_did, def_visitor.visit_as_dependent(maybe_iterator_did.as_id()))) {
        return fail();
    }

    const bool thru_ref = context.exec(iterable_eid).holds<ExecDeref>();
    const auto maybe_init_eid
        = thru_ref ? try_checked_borrow(context, iterable_eid, self_ty.mut, iterable_span)
                   : consume(lctx, iterable_eid);
    if (maybe_init_eid.empty()) {
        return {};
    }
    const TypeId it_tid
        = thru_ref ? infer_type_from_exec(maybe_init_eid.as_id()).as_id()
                   : context.emplace_type(context.type(iterable_tid).value, iterable_span, true);
    const DefId it_did = emplace_hidden_local(block, context.symbol_id(iterable_span), it_tid,
                                              iterable_span, maybe_init_eid);
    const TypeId next_val_tid
        = context.emplace_type(context.type(opt_tid).value, iterable_span, true);

    const ExecId true_eid
        = context.emplace_compt_exec(ExecConst{ConstantValue{true}}, iterable_span);

    return solve_loop_core(
        fid, lctx, span, true_eid, false, nullptr, for_in.body_stmt,
        [&](ExecId loop_eid, LexicalCtx body_lctx, InProgressBlock& prelude) -> bool {
            const ExecId it_var = context.emplace_exec(
                ExecVariable{.def_id = it_did, .type_id = it_tid}, iterable_span);
            const auto maybe_self_eid
                = thru_ref ? it_var
                           : try_checked_borrow(context, it_var, self_ty.mut, iterable_span);
            if (maybe_self_eid.empty()) {
                return false;
            }
            const ExecId call_eid = context.emplace_exec(
                ExecFnCall{.callee = emplace_fn_ptr_exec(context, next_did, iterable_span),
                           .args = context.freeze_id_vec({maybe_self_eid.as_id()})},
                iterable_span);
            const DefId next_val_did = emplace_hidden_local(
                prelude, context.symbol_id<"for next">(), next_val_tid, iterable_span, call_eid);
            const auto next_val_var = [&] {
                return context.emplace_exec(
                    ExecVariable{.def_id = next_val_did, .type_id = next_val_tid}, iterable_span);
            };

            const ExecId discrim_eid = context.emplace_exec(
                ExecBuiltinAccess{.from = next_val_var(),
                                  .kind = builtin_access::variant_discriminant},
                iterable_span);
            const ExecId some_idx_eid = context.emplace_exec(
                ExecConst{context.def(some_did).member_idx}
                    .try_down_convert_to(
                        context.def(opt_did).as<DefVariant>().type_of_discriminant())
                    .value(),
                iterable_span);
            InProgressBlock exit_block{};
            exit_block.push_back_exec(context.emplace_exec(
                ExecJump{.block = loop_eid, .spot = jump_spot::end}, iterable_span));
            current_loop_has_break = true;
            prelude.push_back_exec(context.emplace_exec(
                ExecBranch{.condition
                           = context.emplace_exec(ExecBinary{.lhs = discrim_eid,
                                                             .rhs = some_idx_eid,
                                                             .op = binary_op::bool_not_equal},
                                                  iterable_span),
                           .then_block = emplace_block_exec(exit_block, body_lctx, iterable_span),
                           .else_block = {}},
                iterable_span));

            const ExecId payload_eid = context.emplace_exec(
                ExecMemberAccess{.owner = next_val_var(), .member = payload_did}, iterable_span);
            if (!bind_for_each_var(fid, body_lctx, prelude, for_in.each, payload_eid,
                                   for_each_src::iterator, iterable_eid)) {
                return false;
            }
            const DefId each_did = locals.back();
            if (!context.type(context.def(each_did).as<DefVariable>().type_id).holds<TypeRef>()
                && !context.type_is_copyable(item_tid)) {
                const auto maybe_move_eid = consume(body_lctx, next_val_var());
                if (maybe_move_eid.empty()) {
                    return false;
                }
                prelude.push_back_exec(maybe_move_eid.as_id());
            }
            return true;
        });
}

OptId<ExecId> RuntimeSolver::handle_for_compt(FileId fid, LexicalCtx lctx, InProgressBlock& block,
                                              const ast_stmt_t* stmt) {
    const ast_stmt_for_in_t for_in = stmt->stmt.for_in_stmt;
    const Span span{context, fid, stmt};
    const Span iterable_span{context, fid, for_in.iterator};

    const auto maybe_iterable_eid
        = ComptExprSolver{def_visitor}.solve_expr(fid, lctx.scope, for_in.iterator);
    if (maybe_iterable_eid.empty()) {
        return {};
    }
    const Exec iterable = context.exec(maybe_iterable_eid.as_id());

    llvm::SmallVector<ExecId> elems{};
    const auto exceeds_cap = [&] { return elems.size() > MAX_COMPT_FOR_ITERATIONS; };
    bool iterable_ok = false;
    if (iterable.holds<ExecListLiteral>()) {
        iterable_ok = true;
        const IdSlice<ExecId> list = iterable.as<ExecListLiteral>().elems;
        for (size_t i = 0; i < list.len() && !exceeds_cap(); ++i) {
            elems.push_back(context.exec_id(list.get(i)));
        }
    } else if (iterable.holds<ExecConst>() && iterable.as<ExecConst>().holds<SymbolId>()) {
        iterable_ok = true;
        const std::string chars{context.symbol(iterable.as<ExecConst>().as<SymbolId>())};
        for (size_t i = 0; i < chars.size() && !exceeds_cap(); ++i) {
            const char c = chars[i];
            elems.push_back(context.emplace_compt_exec(ExecConst{ConstantValue{c}}, iterable_span));
        }
    } else if (iterable.holds<ExecRange>()) {
        const ExecRange range = iterable.as<ExecRange>();
        const auto maybe_start = context.exec(range.start).try_as<ExecConst>();
        const auto maybe_end = context.exec(range.end).try_as<ExecConst>();
        if (maybe_start.has_value() && maybe_end.has_value() && maybe_start->is_integral()
            && maybe_end->is_integral()) {
            iterable_ok = true;
            const ExecConst start = maybe_start.value();
            const ExecConst end = maybe_end.value();
            const builtin_type bt = start.type_builtin();
            const auto push_elem = [&](ConstantValue value) {
                const auto maybe_elem = ExecConst{value}.try_cast_to(context, bt);
                if (maybe_elem.has_value()) {
                    elems.push_back(context.emplace_compt_exec(maybe_elem.value(), iterable_span));
                }
            };
            if (start.is_signed_integral()) {
                const int64_t last = end.to_i64();
                for (int64_t v = start.to_i64(); range.inclusive ? v <= last : v < last; ++v) {
                    push_elem(ConstantValue{v});
                    if (v == last || exceeds_cap()) {
                        break;
                    }
                }
            } else {
                const uint64_t last = end.to_size();
                for (uint64_t v = start.to_size(); range.inclusive ? v <= last : v < last; ++v) {
                    push_elem(ConstantValue{v});
                    if (v == last || exceeds_cap()) {
                        break;
                    }
                }
            }
        }
    }
    const bool exceeded = exceeds_cap();
    if (exceeded) {
        elems.resize(MAX_COMPT_FOR_ITERATIONS);
    }
    if (!iterable_ok) {
        diag_not_iterable(iterable_span, infer_type_from_exec(maybe_iterable_eid.as_id()),
                          diag_code::for_compt_iterates_over_compt_lists_strs_and_ranges);
        return {};
    }

    const auto maybe_written_tid = resolve_type(fid, lctx, for_in.each->type);
    if (maybe_written_tid.empty()) {
        return {};
    }
    const TypeId written_tid = maybe_written_tid.as_id();
    const bool infer = TypeTransformer<TypeContainsVar>{context}(written_tid);
    const SymbolId name = context.symbol_id(for_in.each->name);
    const Span var_span{context, fid, for_in.each->first, for_in.each->last};
    const Span body_span{context, fid, for_in.body_stmt};

    const bool outer_inside_compt_for = inside_compt_for;
    inside_compt_for = true;

    InProgressBlock expansion{};
    bool cooked = false;
    for (const ExecId elem_eid : elems) {
        OptId<ExecId> maybe_value_eid = elem_eid;
        if (!infer && context.exec(elem_eid).holds<ExecConst>()) {
            if (const auto maybe_conv = context.try_convert_to(elem_eid, written_tid)) {
                maybe_value_eid = maybe_conv;
            }
        }
        const auto maybe_elem_tid = infer_type_from_exec(maybe_value_eid.as_id());
        if (maybe_elem_tid.empty()) {
            cooked = true;
            break;
        }
        TypeId tid = written_tid;
        if (infer) {
            tid = maybe_elem_tid.as_id();
        } else if (!context.assignable_and_inferable_from_type_to(maybe_elem_tid.as_id(),
                                                                  written_tid)) {
            context.emplace_diagnostic_with_message_value(
                var_span, diag_code::cannot_convert_value_of_type, diag_type::error,
                DiagnosticTypeToType{.from = maybe_elem_tid.as_id(), .to = written_tid});
            cooked = true;
            break;
        }

        const LexicalCtx iter_lctx{.scope = context.make_small_scope(lctx.scope, body_span),
                                   .map = lctx.map};
        const DefId did
            = context.register_def(name, true, true, 0, var_span, nullptr,
                                   DefVariable{.type_id = tid, .compt_value = maybe_value_eid});
        context.insert_variable(iter_lctx.scope, name, did);
        InProgressBlock iteration{};
        iteration.push_back_def(did);
        const auto maybe_body_eid = solve_block(fid, iter_lctx, for_in.body_stmt);
        if (maybe_body_eid.empty()) {
            cooked = true;
            continue;
        }
        iteration.push_back_exec(maybe_body_eid.as_id());
        expansion.push_back_exec(emplace_block_exec(iteration, iter_lctx, body_span));
    }

    inside_compt_for = outer_inside_compt_for;

    if (exceeded && !cooked) {
        context.emplace_diagnostic_with_message_value(
            iterable_span, diag_code::maximum_compt_for_iterations_exceeded, diag_type::error,
            DiagnosticSymbolAfterMessage{
                .sid = context.symbol_id(std::to_string(MAX_COMPT_FOR_ITERATIONS))});
        return {};
    }

    if (cooked) {
        return {};
    }
    const ExecId eid = emplace_block_exec(expansion, lctx, span);
    block.push_back_exec(eid);
    return eid;
}

} // namespace hir
