//     /                              /
//    /                              /
//   /_____  _____  _____  _____    /  _____   _  _  _____
//  /     / /____  /____/ /____/   /  /____/  /\  / /  __
// /_____/ /____  /    / /   \    /  /    /  /  \/ /____/
// Copyright (C) 2025-2026 Zachary Mahan
// Licensed under the GNU GPL v3. See LICENSE for details.

#include "compiler/hir/run_time_solver.hpp"
#include "compiler/hir/compt_expr_solver.hpp"
#include "compiler/hir/indexing.hpp"
#include "compiler/hir/scope.hpp"
#include "compiler/hir/type.hpp"

namespace hir {

[[nodiscard]] OptId<ExecId> RuntimeSolver::solve_expr(FileId fid, ScopeId scope,
                                                      const ast_expr_t* expr, TypeId into_tid) {
    return solve_expr(fid, LexicalCtx{.scope = scope, .map = context.make_move_map({})}, expr,
                      into_tid);
}

[[nodiscard]] OptId<ExecId> RuntimeSolver::solve_expr(FileId fid, ScopeId scope,
                                                      const ast_expr_t* expr) {
    return solve_expr(fid, LexicalCtx{.scope = scope, .map = context.make_move_map({})}, expr);
}

[[nodiscard]] OptId<ExecId> RuntimeSolver::solve_expr(FileId fid, LexicalCtx lctx,
                                                      const ast_expr_t* expr) {
    return handle_any_typed_expr(fid, lctx, expr);
}

[[nodiscard]] OptId<ExecId> RuntimeSolver::solve_expr(FileId fid, LexicalCtx lctx,
                                                      const ast_expr_t* expr,
                                                      OptId<TypeId> into_tid) {
    if (into_tid) {
        return solve_expr(fid, lctx, expr, into_tid.as_id());
    }
    return solve_expr(fid, lctx, expr);
}

[[nodiscard]] OptId<TypeId> RuntimeSolver::infer_type_from_exec(ExecId eid) {
    return ComptExprSolver{context, def_visitor}.infer_type_from_exec(eid); // TODO
}

[[nodiscard]] OptId<ExecId> RuntimeSolver::solve_expr(FileId fid, LexicalCtx lctx,
                                                      const ast_expr_t* expr, TypeId into_tid) {
    const Type& ty = context.type(into_tid);
    if (ty.holds<TypeVar>()) {
        return handle_any_typed_expr(fid, lctx, expr);
    }

    // TODO
    switch (expr->type) {
    case AST_EXPR_ID:
    case AST_EXPR_GENERIC_ID:
    case AST_EXPR_LITERAL:
    case AST_EXPR_LIST_LITERAL:
    case AST_EXPR_BINARY:
    case AST_EXPR_GROUPING:
    case AST_EXPR_PRE_UNARY:
    case AST_EXPR_POST_UNARY:
    case AST_EXPR_SUBSCRIPT:
    case AST_EXPR_FN_CALL:
    case AST_EXPR_TYPE:
    case AST_EXPR_COMPT:
    case AST_EXPR_BORROW:
    case AST_EXPR_ADDR_OF:
    case AST_EXPR_SAME_TYPE:
    case AST_EXPR_TYPE_TO_STR:
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
    case AST_EXPR_SIZEOF:
    case AST_EXPR_STRUCT_INIT:
    case AST_EXPR_STRUCT_MEMBER_INIT:
    case AST_EXPR_CLOSURE:
    case AST_EXPR_TERNARY_IF:
    case AST_EXPR_VARIANT_DECOMP:
    case AST_EXPR_BLOCK:
    case AST_EXPR_MATCH_BRANCH:
    case AST_EXPR_MATCH:
    case AST_EXPR_ELSE_MATCH_PATTERN:
    case AST_EXPR_INVALID:
        break;
    }
    return {};
}

[[nodiscard]] OptId<ExecId> RuntimeSolver::handle_any_typed_expr(FileId fid, LexicalCtx lctx,
                                                                 const ast_expr_t* expr) {
    // TODO
    switch (expr->type) {
    case AST_EXPR_ID:
    case AST_EXPR_GENERIC_ID:
    case AST_EXPR_LITERAL:
    case AST_EXPR_LIST_LITERAL:
    case AST_EXPR_BINARY:
    case AST_EXPR_GROUPING:
    case AST_EXPR_PRE_UNARY:
    case AST_EXPR_POST_UNARY:
    case AST_EXPR_SUBSCRIPT:
    case AST_EXPR_FN_CALL:
    case AST_EXPR_TYPE:
    case AST_EXPR_COMPT:
    case AST_EXPR_BORROW:
    case AST_EXPR_ADDR_OF:
    case AST_EXPR_SAME_TYPE:
    case AST_EXPR_TYPE_TO_STR:
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
    case AST_EXPR_SIZEOF:
    case AST_EXPR_STRUCT_INIT:
    case AST_EXPR_STRUCT_MEMBER_INIT:
    case AST_EXPR_CLOSURE:
    case AST_EXPR_TERNARY_IF:
    case AST_EXPR_VARIANT_DECOMP:
    case AST_EXPR_BLOCK:
    case AST_EXPR_MATCH_BRANCH:
    case AST_EXPR_MATCH:
    case AST_EXPR_ELSE_MATCH_PATTERN:
    case AST_EXPR_INVALID:
        break;
    }
    return {};
}

} // namespace hir
