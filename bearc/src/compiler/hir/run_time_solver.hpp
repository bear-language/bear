//     /                              /
//    /                              /
//   /_____  _____  _____  _____    /  _____   _  _  _____
//  /     / /____  /____/ /____/   /  /____/  /\  / /  __
// /_____/ /____  /    / /   \    /  /    /  /  \/ /____/
// Copyright (C) 2025-2026 Zachary Mahan
// Licensed under the Apache License 2.0. See LICENSE for details.

#ifndef BEARC_COMPILER_HIR_RUN_TIME_EXPR_SOLVER_HPP
#define BEARC_COMPILER_HIR_RUN_TIME_EXPR_SOLVER_HPP

#include "compiler/ast/stmt_slice.h"
#include "compiler/hir/context.hpp"
#include "compiler/hir/def_visitor.hpp"
#include "compiler/hir/expr_solver.hpp"
#include "compiler/hir/indexing.hpp"
#include "compiler/hir/scope.hpp"
#include <cassert>
#include <cstdint>

namespace hir {

class RuntimeSolver {
    DefVisitor& def_visitor;
    Context& context;
    llvm::SmallVector<DefId> locals;
    OptId<TypeId> return_tid{};
    OptId<ExecId> current_loop_block_eid{};
    /// used for jumping to the update exec for a for loop
    OptId<ExecId> current_loop_update_eid{};
    bool inside_match_branch{};

    struct InProgressBlock {
        llvm::SmallVector<DefId> defs;
        llvm::SmallVector<ExecId> execs;
        constexpr void push_back_exec(ExecId eid) { execs.push_back(eid); }
        constexpr void push_back_def(DefId did) { defs.push_back(did); }
    };

  public:
    [[nodiscard]] RuntimeSolver(Context& ctx, DefVisitor& def_visitor)
        : def_visitor{def_visitor}, context{ctx} {}

    [[nodiscard]] Context& get_context() { return this->context; }

    [[nodiscard]] DefVisitor& get_def_visitor() { return this->def_visitor; }

    [[nodiscard]] llvm::SmallVector<DefId>& get_locals() { return this->locals; }

    void set_return_type(OptId<TypeId> maybe_tid) { this->return_tid = maybe_tid; }

    void reset_return_type() { this->return_tid = {}; }

    [[nodiscard]] OptId<ExecId> solve_expr(FileId fid, ScopeId scope, const ast_expr_t* expr,
                                           TypeId into_tid);

    [[nodiscard]] OptId<ExecId> solve_expr(FileId fid, LexicalCtx lctx, const ast_expr_t* expr);

    [[nodiscard]] OptId<ExecId> solve_expr(FileId fid, LexicalCtx lctx, const ast_expr_t* expr,
                                           TypeId into_tid);

    [[nodiscard]] OptId<ExecId> solve_expr(FileId fid, LexicalCtx lctx, const ast_expr_t* expr,
                                           OptId<TypeId> maybe_into_tid);

    [[nodiscard]] OptId<ExecId> solve_expr(FileId fid, ScopeId scope, const ast_expr_t* expr);

    [[nodiscard]] OptId<TypeId> infer_type_from_exec(ExecId eid) {
        return context.infer_type_from_exec(eid);
    }

    [[nodiscard]] OptId<ExecId> solve_block_requiring_return(FileId fid, LexicalCtx lctx,
                                                             ast_slice_of_stmts_t stmts, Span span);

    [[nodiscard]] OptId<ExecId> solve_block(FileId fid, LexicalCtx lctx, ast_slice_of_stmts_t stmts,
                                            Span span, bool require_return = false);

    [[nodiscard]] OptId<ExecId> solve_block(FileId fid, LexicalCtx lctx, const ast_stmt_t* stmt,
                                            bool require_return = false);

    /// lowers generic args, like ::<i32, 123> or ::i32, etc.
    ///
    /// fid - FileId containing the lexical generic args
    /// scope - ScopeId containing the lexical generic args
    /// gen_args - generic arg ast nodes
    /// need_layout_info = false - bool indicating if the nested types inside the args need
    /// need_layout_info
    [[nodiscard]] OptId<GenericArgIdSliceId>
    lower_generic_args(FileId fid, ScopeId scope, ast_slice_of_generic_args_t gen_args,
                       bool need_layout_info = false);

  private:
    enum class storage : uint8_t {
        non_static = 0,
        statik,
    };

    enum class compt : uint8_t {
        non_compt = 0,
        compt,
    };

    /// internally handles all ast_stmt_t statement types
    ///
    /// - fid   - current file id
    /// - lctx  - current lexical context
    /// - block - an InProgressBlock& which is modified by reference where any newly introduced defs
    /// and/or execs will be emplaced.
    /// - storage - indicates static vs non_static
    /// - compt - indicates compt vs non_compt
    /// - align - indicates desired alignment (0 is default alignment)
    ///
    /// returns the ExecId corresponding to the statement (if one
    /// exists for the given statement; if more than one exec is produced, the final emitted exec
    /// will be returned; this is most helpful for determining issues where blocks should end with a
    /// return or yield statement)
    OptId<ExecId> handle_stmt(FileId fid, LexicalCtx lctx, InProgressBlock& block,
                              const ast_stmt_t* stmt, storage storage = storage::non_static,
                              compt compt = compt::non_compt, uint8_t align = 0);
    OptId<ExecId> handle_return(FileId fid, LexicalCtx lctx, InProgressBlock& block,
                                const ast_stmt_t* stmt);
    OptId<ExecId> handle_use(FileId fid, LexicalCtx lctx, const ast_stmt_t* stmt);
    [[nodiscard]] OptId<ExecId> handle_any_typed_expr(FileId fid, LexicalCtx lctx,
                                                      const ast_expr_t* expr);
    [[nodiscard]] OptId<ExecId> handle_any_id(FileId fid, LexicalCtx lctx, const ast_expr_t* expr);
    [[nodiscard]] OptId<ExecId> handle_any_generic_id(FileId fid, LexicalCtx lctx,
                                                      const ast_expr_t* expr);
    OptId<ExecId> handle_compt(FileId fid, LexicalCtx lctx, InProgressBlock& block,
                               const ast_stmt_t* stmt, uint8_t align = 0);
    OptId<ExecId> handle_static(FileId fid, LexicalCtx lctx, InProgressBlock& block,
                                const ast_stmt_t* stmt, storage storage = storage::non_static,
                                compt compt = compt::non_compt, uint8_t align = 0);
    OptId<ExecId> handle_alignas(FileId fid, LexicalCtx lctx, InProgressBlock& block,
                                 const ast_stmt_t* stmt, storage storage, compt compt,
                                 uint8_t align);
    OptId<ExecId> handle_var_decl(FileId fid, LexicalCtx lctx, InProgressBlock& block,
                                  const ast_stmt_t* stmt, storage storage, compt compt,
                                  uint8_t align);
    OptId<ExecId> handle_var_init_decl(FileId fid, LexicalCtx lctx, InProgressBlock& block,
                                       const ast_stmt_t* stmt, storage storage, compt compt,
                                       uint8_t align);
    OptId<ExecId> handle_deftype(FileId fid, LexicalCtx lctx, const ast_stmt_t* stmt);
    /// block statement handler that emplaces the block's ExecId (when valid) inside of a current
    /// in-progress block
    OptId<ExecId> handle_block(FileId fid, LexicalCtx lctx, InProgressBlock& block,
                               const ast_stmt_t* stmt);

    OptId<ExecId> handle_expr_stmt(FileId fid, LexicalCtx lctx, InProgressBlock& block,
                                   const ast_stmt_t* stmt);
    OptId<ExecId> handle_break(FileId fid, InProgressBlock& block, const ast_stmt_t* stmt);
    OptId<ExecId> handle_continue(FileId fid, InProgressBlock& block, const ast_stmt_t* stmt);

    [[nodiscard]] OptId<ExecId> handle_literal(FileId fid, const ast_expr_t* expr);

    /// tries to convert to the corret type if possible, otherwise returns the value of the
    /// incorrect type (the behavior is done this way so diagnostics can be externally managed)
    [[nodiscard]] OptId<ExecId> handle_literal(FileId fid, const ast_expr_t* expr, TypeId into_tid);

    [[nodiscard]] OptId<ExecId> handle_struct_or_union_init(FileId fid, LexicalCtx lctx,
                                                            const ast_expr_t* expr,
                                                            OptId<TypeId> maybe_into_tid);
    [[nodiscard]] OptId<ExecId> handle_list_literal(FileId fid, LexicalCtx lctx,
                                                    const ast_expr_t* expr,
                                                    OptId<TypeId> maybe_into_tid);
    [[nodiscard]] OptId<ExecId> handle_grouping(FileId fid, LexicalCtx lctx, const ast_expr_t* expr,
                                                OptId<TypeId> maybe_into_tid) {
        assert(expr->type == AST_EXPR_GROUPING);
        return solve_expr(fid, lctx, expr->expr.grouping.expr, maybe_into_tid);
    }
    [[nodiscard]] OptId<ExecId> handle_tuple_init(FileId fid, LexicalCtx lctx,
                                                  const ast_expr_t* expr);
};

static_assert(IsExprSolver<RuntimeSolver>);

} // namespace hir

#endif // !BEARC_COMPILER_HIR_RUN_TIME_EXPR_SOLVER_HPP
