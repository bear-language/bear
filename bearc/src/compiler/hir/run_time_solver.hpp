//     /                              /
//    /                              /
//   /_____  _____  _____  _____    /  _____   _  _  _____
//  /     / /____  /____/ /____/   /  /____/  /\  / /  __
// /_____/ /____  /    / /   \    /  /    /  /  \/ /____/
// Copyright (C) 2025-2026 Zachary Mahan
// Licensed under the GNU GPL v3. See LICENSE for details.

#ifndef BEARC_COMPILER_HIR_RUN_TIME_EXPR_SOLVER_HPP
#define BEARC_COMPILER_HIR_RUN_TIME_EXPR_SOLVER_HPP

#include "compiler/ast/stmt_slice.h"
#include "compiler/hir/context.hpp"
#include "compiler/hir/def_visitor.hpp"
#include "compiler/hir/expr_solver.hpp"
#include "compiler/hir/indexing.hpp"
#include "compiler/hir/scope.hpp"

namespace hir {

class RuntimeSolver {
    DefVisitor& def_visitor;
    Context& context;
    OptId<TypeId> current_return_tid{};
    bool inside_loop{};
    bool inside_match_branch{};

    struct InProgressBlock {
        llvm::SmallVector<DefId> defs;
        llvm::SmallVector<ExecId> execs;
    };

  public:
    RuntimeSolver(Context& ctx, DefVisitor& def_visitor) : def_visitor{def_visitor}, context{ctx} {}

    [[nodiscard]] Context& get_context() { return this->context; }

    [[nodiscard]] OptId<ExecId> solve_expr(FileId fid, ScopeId scope, const ast_expr_t* expr,
                                           TypeId into_tid);

    [[nodiscard]] OptId<ExecId> solve_expr(FileId fid, LexicalCtx lctx, const ast_expr_t* expr);

    [[nodiscard]] OptId<ExecId> solve_expr(FileId fid, LexicalCtx lctx, const ast_expr_t* expr,
                                           TypeId into_tid);

    [[nodiscard]] OptId<ExecId> solve_expr(FileId fid, LexicalCtx lctx, const ast_expr_t* expr,
                                           OptId<TypeId> maybe_into_tid);

    [[nodiscard]] OptId<ExecId> solve_expr(FileId fid, ScopeId scope, const ast_expr_t* expr);

    [[nodiscard]] OptId<TypeId> infer_type_from_exec(ExecId eid);

    [[nodiscard]] OptId<ExecId> solve_block(FileId fid, LexicalCtx lctx, ast_slice_of_stmts_t stmts,
                                            OptId<TypeId> maybe_return_tid);

  private:
    /// internally handles all ast_stmt_t statement types
    ///
    /// - fid   - current file id
    /// - lctx  - current lexical context
    /// - block - an InProgressBlock& which is modified by reference where any newly introduced defs
    /// and/or execs will be emplaced.
    ///
    /// returns the ExecId corresponding to the statement (if one
    /// exists for the given statement; if more than one exec is produced, the final emitted exec
    /// will be returned; this is most helpful for determining issues where blocks should end with a
    /// return or yield statement)
    OptId<ExecId> handle_stmt(FileId fid, LexicalCtx lctx, InProgressBlock& block,
                              const ast_stmt_t* stmt);
    [[nodiscard]] OptId<ExecId> handle_any_typed_expr(FileId fid, LexicalCtx lctx,
                                                      const ast_expr_t* expr);
};

static_assert(IsExprSolver<RuntimeSolver>);

} // namespace hir

#endif // !BEARC_COMPILER_HIR_RUN_TIME_EXPR_SOLVER_HPP
