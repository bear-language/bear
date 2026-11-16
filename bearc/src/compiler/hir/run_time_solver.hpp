//     /                              /
//    /                              /
//   /_____  _____  _____  _____    /  _____   _  _  _____
//  /     / /____  /____/ /____/   /  /____/  /\  / /  __
// /_____/ /____  /    / /   \    /  /    /  /  \/ /____/
// Copyright (C) 2025-2026 Zachary Mahan
// Licensed under the Apache License 2.0. See LICENSE for details.

#ifndef BEARC_COMPILER_HIR_RUN_TIME_EXPR_SOLVER_HPP
#define BEARC_COMPILER_HIR_RUN_TIME_EXPR_SOLVER_HPP

#include "compiler/ast/expr.h"
#include "compiler/ast/stmt_slice.h"
#include "compiler/hir/context.hpp"
#include "compiler/hir/def_visitor.hpp"
#include "compiler/hir/exec_ops.hpp"
#include "compiler/hir/expr_solver.hpp"
#include "compiler/hir/indexing.hpp"
#include "compiler/hir/scope.hpp"
#include "llvm/ADT/STLFunctionalExtras.h"
#include <cassert>
#include <cstdint>

namespace hir {

class RuntimeSolver {
    DefVisitor& def_visitor;
    Context& context;
    llvm::SmallVector<DefId> locals;
    OptId<TypeId> return_tid{};
    IdSlice<DefId> params{};
    OptId<ExecId> current_loop_block_eid{};
    bool current_loop_has_break{};
    OptId<MoveMapId> current_loop_base_map{};
    llvm::SmallVector<MoveMapId>* current_loop_exit_maps{};
    llvm::SmallVector<MoveMapId>* current_loop_back_edge_maps{};
    bool inside_loop_header{};
    bool inside_compt_for{};
    static constexpr size_t MAX_COMPT_FOR_ITERATIONS = 512;

    struct MatchCtx {
        OptId<TypeId> result_tid;
        MoveMapId base_map;
        llvm::SmallVector<MoveMapId> exit_maps;
        bool poisoned;
    };
    MatchCtx* current_match{};

    struct InProgressBlock {
        llvm::SmallVector<DefId> defs;
        llvm::SmallVector<ExecId> execs;
        constexpr void push_back_exec(ExecId eid) { execs.push_back(eid); }
        constexpr void push_back_def(DefId did) { defs.push_back(did); }
    };

    struct IsDecompTarget {
        LexicalCtx then_lctx;
        InProgressBlock& outer_block;
        InProgressBlock& then_block;
    };

  public:
    [[nodiscard]] RuntimeSolver(Context& ctx, DefVisitor& def_visitor)
        : def_visitor{def_visitor}, context{ctx} {}

    [[nodiscard]] Context& get_context() { return this->context; }

    [[nodiscard]] DefVisitor& get_def_visitor() { return this->def_visitor; }

    [[nodiscard]] llvm::SmallVector<DefId>& get_locals() { return this->locals; }

    void set_return_type(OptId<TypeId> maybe_tid) { this->return_tid = maybe_tid; }

    void reset_return_type() { this->return_tid = {}; }

    void set_params(IdSlice<DefId> params) { this->params = params; }

    void ban_returning_ref_to_local(ExecId returnee_eid);

    [[nodiscard]] OptId<TypeId> resolve_type(FileId fid, LexicalCtx lctx, const ast_type_t* type);

    [[nodiscard]] OptId<ExecId> solve_expr(FileId fid, ScopeId scope, const ast_expr_t* expr,
                                           TypeId into_tid);

    [[nodiscard]] OptId<ExecId> solve_expr(FileId fid, LexicalCtx lctx, const ast_expr_t* expr);

    [[nodiscard]] OptId<ExecId> solve_expr(FileId fid, LexicalCtx lctx, const ast_expr_t* expr,
                                           TypeId into_tid);

    [[nodiscard]] OptId<ExecId> solve_expr(FileId fid, LexicalCtx lctx, const ast_expr_t* expr,
                                           OptId<TypeId> maybe_into_tid);

    [[nodiscard]] OptId<ExecId> solve_expr(FileId fid, ScopeId scope, const ast_expr_t* expr);

    [[nodiscard]] OptId<ExecId> solve_place(FileId fid, LexicalCtx lctx, const ast_expr_t* expr);

    [[nodiscard]] OptId<ExecId> consume(LexicalCtx lctx, ExecId eid);

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

    enum class for_each_src : uint8_t {
        iterator,
        indexable,
        range,
    };

    using LoopPrelude
        = llvm::function_ref<bool(ExecId loop_eid, LexicalCtx body_lctx, InProgressBlock& prelude)>;

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
    [[nodiscard]] OptId<ExecId> handle_any_id(FileId fid, LexicalCtx lctx, const ast_expr_t* expr,
                                              bool check_moved = true);
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
    OptId<ExecId> handle_break(FileId fid, LexicalCtx lctx, InProgressBlock& block,
                               const ast_stmt_t* stmt);
    OptId<ExecId> handle_continue(FileId fid, LexicalCtx lctx, InProgressBlock& block,
                                  const ast_stmt_t* stmt);
    OptId<ExecId> handle_yield(FileId fid, LexicalCtx lctx, InProgressBlock& block,
                               const ast_stmt_t* stmt);
    [[nodiscard]] std::optional<bool> try_compt_bool(ExecId eid);
    [[nodiscard]] std::optional<bool> solve_compt_condition(FileId fid, LexicalCtx lctx,
                                                            const ast_expr_t* cond);
    /// solves a loop (condition, body, and optional update) and returns the ExecLoop's ExecId
    /// without emplacing it into any block
    OptId<ExecId> solve_loop(FileId fid, LexicalCtx lctx, Span span, const ast_expr_t* cond_expr,
                             const ast_expr_t* update_expr, const ast_stmt_t* body_stmt);
    OptId<ExecId> solve_loop_core(FileId fid, LexicalCtx lctx, Span span,
                                  OptId<ExecId> maybe_cond_eid, bool cooked,
                                  const ast_expr_t* update_expr, const ast_stmt_t* body_stmt,
                                  LoopPrelude prelude = {});
    OptId<ExecId> handle_for_in(FileId fid, LexicalCtx lctx, InProgressBlock& block,
                                const ast_stmt_t* stmt);
    OptId<ExecId> handle_for_compt(FileId fid, LexicalCtx lctx, InProgressBlock& block,
                                   const ast_stmt_t* stmt);
    OptId<ExecId> solve_range_for_in(FileId fid, LexicalCtx lctx, InProgressBlock& block,
                                     const ast_stmt_t* stmt, const ast_expr_t* range_expr,
                                     bool inclusive);
    OptId<ExecId> solve_indexable_for_in(FileId fid, LexicalCtx lctx, InProgressBlock& block,
                                         const ast_stmt_t* stmt, ExecId seq_eid);
    OptId<ExecId> solve_iterator_for_in(FileId fid, LexicalCtx lctx, InProgressBlock& block,
                                        const ast_stmt_t* stmt, ExecId iterable_eid);
    [[nodiscard]] bool bind_for_each_var(FileId fid, LexicalCtx body_lctx,
                                         InProgressBlock& prelude, const ast_param_t* param,
                                         ExecId elem_eid, for_each_src src);
    DefId emplace_hidden_local(InProgressBlock& block, SymbolId name, TypeId tid, Span span,
                               OptId<ExecId> maybe_init_eid);
    [[nodiscard]] ExecId emplace_block_exec(const InProgressBlock& block, LexicalCtx lctx,
                                            Span span);
    void diag_not_iterable(Span span, OptId<TypeId> maybe_tid, diag_code help);
    OptId<ExecId> handle_while(FileId fid, LexicalCtx lctx, InProgressBlock& block,
                               const ast_stmt_t* stmt);
    OptId<ExecId> handle_for(FileId fid, LexicalCtx lctx, InProgressBlock& block,
                             const ast_stmt_t* stmt);
    OptId<ExecId> handle_if(FileId fid, LexicalCtx lctx, InProgressBlock& block,
                            const ast_stmt_t* stmt);

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
    [[nodiscard]] OptId<ExecId> handle_fn_call(FileId fid, LexicalCtx lctx, const ast_expr_t* expr);
    [[nodiscard]] OptId<ExecId> handle_match(FileId fid, LexicalCtx lctx, const ast_expr_t* expr,
                                             OptId<TypeId> maybe_into_tid);
    [[nodiscard]] OptId<ExecId> handle_ternary_if(FileId fid, LexicalCtx lctx,
                                                  const ast_expr_t* expr,
                                                  OptId<TypeId> maybe_into_tid);
    [[nodiscard]] bool decompose_variant(FileId fid, LexicalCtx branch_lctx, InProgressBlock& block,
                                         const ast_expr_t* decomp_expr, DefId variant_field_did,
                                         ExecId variant_eid);
    [[nodiscard]] OptId<ExecId> handle_borrow(FileId fid, LexicalCtx lctx, const ast_expr_t* expr);
    void diag_not_yet_implemented(Span span);
    DiagRange warn_short_circuit(FileId fid, const ast_expr_t* expr, bool always);
    std::optional<DiagRange> try_warn_suspicious_assignment_in_cond(ExecId eid);
    [[nodiscard]] OptId<ExecId> handle_addr_of(FileId fid, LexicalCtx lctx, const ast_expr_t* expr);
    [[nodiscard]] OptId<ExecId> handle_preunary(FileId fid, LexicalCtx lctx,
                                                const ast_expr_t* expr);
    [[nodiscard]] OptId<ExecId> handle_postunary(FileId fid, LexicalCtx lctx,
                                                 const ast_expr_t* expr);
    /// handles explicit pointer dereference, ptr.*
    [[nodiscard]] OptId<ExecId> handle_deref(FileId fid, LexicalCtx lctx, const ast_expr_t* expr);
    /// lowers ++/-- (pre or post) into an ExecCompoundAssignment of the operand with a constant 1
    [[nodiscard]] OptId<ExecId> handle_inc_dec(FileId fid, LexicalCtx lctx, const ast_expr_t* expr,
                                               unary_op op, bool post);
    [[nodiscard]] OptId<ExecId> handle_binary_expr(FileId fid, LexicalCtx lctx,
                                                   const ast_expr_t* expr);

    [[nodiscard]] OptId<ExecId> handle_assign(FileId fid, LexicalCtx lctx, const ast_expr_t* expr);
    [[nodiscard]] OptId<ExecId> handle_is(FileId fid, LexicalCtx lctx, const ast_expr_t* expr,
                                          IsDecompTarget* decomp_target = nullptr);
    [[nodiscard]] OptId<ExecId> handle_as(FileId fid, LexicalCtx lctx, const ast_expr_t* expr);
    [[nodiscard]] OptId<ExecId> handle_subscript(FileId fid, LexicalCtx lctx,
                                                 const ast_expr_t* expr);
    [[nodiscard]] OptId<ExecId> handle_access(FileId fid, LexicalCtx lctx, const ast_expr_t* expr,
                                              access_op op);
    [[nodiscard]] OptId<ExecId> scale_ptr_offset(TypeId ptr_tid, ExecId offset_eid);
    [[nodiscard]] bool guard_compt_rhs_operand(builtin_type lhs_bt, binary_op op, ExecId rhs_eid);
    [[nodiscard]] OptId<ExecId> solve_binary_exec(ExecId lhs_eid, binary_op op, ExecId rhs_eid,
                                                  Span span);
    [[nodiscard]] OptId<ExecId> handle_arithmetic_logical_or_bool_binary(FileId fid,
                                                                         LexicalCtx lctx,
                                                                         const ast_expr_t* expr,
                                                                         binary_op op);
};

static_assert(IsExprSolver<RuntimeSolver>);

} // namespace hir

#endif // !BEARC_COMPILER_HIR_RUN_TIME_EXPR_SOLVER_HPP
