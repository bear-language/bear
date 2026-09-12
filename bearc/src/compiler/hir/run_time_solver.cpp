//
//     /                              /
//    /                              /
//   /_____  _____  _____  _____    /  _____   _  _  _____
//  /     / /____  /____/ /____/   /  /____/  /\  / /  __
// /_____/ /____  /    / /   \    /  /    /  /  \/ /____/
// Copyright (C) 2025-2026 Zachary Mahan
// Licensed under the GNU GPL v3. See LICENSE for details.

#include "compiler/hir/run_time_solver.hpp"
#include "compiler/ast/stmt.h"
#include "compiler/hir/compt_expr_solver.hpp"
#include "compiler/hir/def.hpp"
#include "compiler/hir/diagnostic.hpp"
#include "compiler/hir/exec.hpp"
#include "compiler/hir/indexing.hpp"
#include "compiler/hir/scope.hpp"
#include "compiler/hir/type.hpp"
#include "compiler/hir/type_resolver.hpp"
#include "utils/data_arena.hpp"
#include <bit>

namespace hir {

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
    return handle_any_typed_expr(fid, lctx, expr);
}

[[nodiscard]] OptId<ExecId> RuntimeSolver::solve_expr(FileId fid, LexicalCtx lctx,
                                                      const ast_expr_t* expr,
                                                      OptId<TypeId> maybe_into_tid) {
    if (maybe_into_tid) {
        return solve_expr(fid, lctx, expr, maybe_into_tid.as_id());
    }
    return solve_expr(fid, lctx, expr);
}

[[nodiscard]] OptId<TypeId> RuntimeSolver::infer_type_from_exec(ExecId eid) {
    /// TODO use a smarter run-time aware inference here
    return ComptExprSolver{context, def_visitor}.infer_type_from_exec(eid);
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

[[nodiscard]] OptId<ExecId> RuntimeSolver::solve_block(FileId fid, LexicalCtx parent_lctx,
                                                       ast_slice_of_stmts_t stmts) {

    Span block_span{context, fid, stmts};
    InProgressBlock in_prog_block{};
    DataArena move_arena{0x100}; // decently sized

    // move map stays the same as parent, but lexical scope nests
    LexicalCtx curr_lctx{.scope = context.make_small_scope(parent_lctx.scope, block_span),
                         .map = parent_lctx.map};

    bool hit_return{false};

    for (auto i = 0uz; i < stmts.len; ++i) {
        const auto maybe_eid = handle_stmt(fid, curr_lctx, in_prog_block, stmts.start[i]);

        // if this is the last statement (and we didn't already see a (premature) return) and we're
        // expecting a return type
        if (!hit_return && i == stmts.len - 1 && this->current_return_tid.has_value()
            && (maybe_eid.empty()
                || (maybe_eid.has_value()
                    && !context.exec(maybe_eid.as_id()).holds<ExecReturn>()))) {

            Span span{context, fid, stmts.start[i]};

            DiagLinker dl{context};
            dl.link(context.emplace_diagnostic(
                span, diag_code::function_may_not_return_a_value_in_all_control_flow_paths,
                diag_type::error));
            dl.link(context.emplace_diagnostic_with_message_value(
                context.type(this->current_return_tid.as_id()).span,
                diag_code::function_has_return_type, diag_type::note,
                DiagnosticTypeAfterMessage{.tid = this->current_return_tid.as_id()}));
            dl.link(context.emplace_diagnostic(
                span, diag_code::end_function_body_with_a_return_statement, diag_type::help));
        }

        if (maybe_eid.empty()) {
            continue;
        }

        // update this so we don't get annoying repetitive diagnostics (we don't want to see a dead
        // code warning AND that function_may_not_return_a_value_in_all_control_flow_paths
        // diagnostic nor do we want to see this more than once after the first premature return)
        if (!hit_return && maybe_eid.has_value()
            && context.exec(maybe_eid.as_id()).holds<ExecReturn>()) {
            hit_return = true;

            if (i < stmts.len - 1) {
                // span is all statements following the CURRENT return statement (that's why it's i
                // + 1, and this is safe since we know i<len-1)
                Span span{context, fid, stmts.start[i + 1]->first,
                          stmts.start[stmts.len - 1]->last};

                DiagLinker dl{context};
                dl.link(context.emplace_diagnostic(
                    span, diag_code::code_is_unreachable_following_a_return, diag_type::error));
                dl.link(context.emplace_diagnostic(Span{context, fid, stmts.start[i]},
                                                   diag_code::return_statement_here,
                                                   diag_type::note));
            }
        }
    }

    return context.emplace_exec(
        ExecBlock{context.emplace_block(Block{.execs = context.freeze_id_vec(in_prog_block.execs),
                                              .defs = context.freeze_id_vec(in_prog_block.defs),
                                              .lctx = curr_lctx})},
        block_span);
}

OptId<ExecId> RuntimeSolver::handle_return(FileId fid, LexicalCtx lctx, const ast_stmt_t* stmt) {
    assert(stmt->type == AST_STMT_RETURN);

    if (this->current_return_tid.has_value()) {
        if (!stmt->stmt.return_stmt.expr) {
            DiagLinker dl{context};

            dl.link(context.emplace_diagnostic(Span{context, fid, stmt},
                                               diag_code::function_expected_return_value,
                                               diag_type::error));
            dl.link(context.emplace_diagnostic_with_message_value(
                context.type(this->current_return_tid.as_id()).span,
                diag_code::function_has_return_type, diag_type::note,
                DiagnosticTypeAfterMessage{.tid = this->current_return_tid.as_id()}));
        } else {
            return context.emplace_exec(
                ExecReturn{.return_value = solve_expr(fid, lctx, stmt->stmt.return_stmt.expr)},
                Span{context, fid, stmt});
        }
    } else {
        if (stmt->stmt.return_stmt.expr) {
            context.emplace_diagnostic(Span{context, fid, stmt->stmt.return_stmt.expr},
                                       diag_code::function_does_not_return_a_value,
                                       diag_type::error);
        }
    }

    return context.emplace_exec(ExecReturn{.return_value = {}}, Span{context, fid, stmt});
}

OptId<ExecId> RuntimeSolver::handle_stmt(FileId fid, LexicalCtx lctx, InProgressBlock& block,
                                         const ast_stmt_t* stmt, storage storage, compt compt,
                                         uint8_t align) {
    switch (stmt->type) {
    case AST_STMT_USE: {
        def_visitor.resolve_use_stmt(fid, lctx.scope, stmt);
        return {};
    }
    case AST_STMT_RETURN: {
        return handle_return(fid, lctx, stmt);
    }

    case AST_STMT_COMPT_MODIFIER: {
        return handle_compt(fid, lctx, block, stmt, align);
    }
    case AST_STMT_STATIC_MODIFIER: {
        return handle_static(fid, lctx, block, stmt, storage, compt, align);
    }

    case AST_STMT_ALIGNAS_MODIFIER: {
        return handle_alignas(fid, lctx, block, stmt, storage, compt, align);
    }

    case AST_STMT_VAR_DECL: {
        return handle_var_decl(fid, lctx, block, stmt, storage, compt, align);
    }

    case AST_STMT_VAR_INIT_DECL: {
        return handle_var_init_decl(fid, lctx, block, stmt, storage, compt, align);
    }

        // TODO:
    case AST_STMT_DEFTYPE:
    case AST_STMT_BLOCK:
    case AST_STMT_EXPR:
    case AST_STMT_EMPTY:
    case AST_STMT_BREAK:
    case AST_STMT_IF:
    case AST_STMT_ELSE:
    case AST_STMT_WHILE:
    case AST_STMT_FOR:
    case AST_STMT_FOR_IN:
    case AST_STMT_YIELD:
    case AST_STMT_CONTINUE:

        // poisoned/malformed:
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
            context.type(maybe_tid.as_id()).span, diag_code::should_have_explicit_type,
            diag_type::error, DiagnosticSymbolBeforeMessage{.sid = name});
    }

    OptId<ExecId> maybe_compt_eid{};
    OptId<ExecId> maybe_runtime_eid{};

    if (compt == compt::compt) {
        maybe_compt_eid
            = context.try_default_value_for_type(maybe_tid.as_id(), span, /*compt=*/true);
    } else {
        maybe_runtime_eid
            = context.try_default_value_for_type(maybe_tid.as_id(), span, /*compt=*/false);
    }

    const DefId did = context.register_def(
        name, compt == compt::compt, storage == storage::statik, align, span, stmt,
        DefVariable{.type_id = maybe_tid.as_id(), .compt_value = maybe_compt_eid, .moved = false},
        {});

    // record in scope
    context.insert_variable(lctx.scope, name, did);

    // always emplace the did
    block.defs.push_back(did);

    if (maybe_runtime_eid.has_value()) {
        block.execs.push_back(maybe_runtime_eid.as_id());
    }

    // TODO: do something special for non-compt static variables using a guard variable

    return maybe_runtime_eid;
}

OptId<ExecId> RuntimeSolver::handle_var_init_decl(FileId fid, LexicalCtx lctx,
                                                  InProgressBlock& block, const ast_stmt_t* stmt,
                                                  storage storage, compt compt, uint8_t align) {
    assert(stmt->type == AST_STMT_VAR_INIT_DECL);

    const SymbolId name = context.symbol_id(stmt->stmt.var_init_decl.name);
    const Span span{context, fid, stmt};

    OptId<TypeId> maybe_tid = TypeResolver{context, def_visitor}.resolve_type(
        fid, lctx.scope, stmt->stmt.var_init_decl.type);

    if (maybe_tid.empty()) {
        return {};
    }

    TypeId fall_back_tid_which_may_contain_var = maybe_tid.as_id();

    OptId<ExecId> maybe_compt_eid{};
    OptId<ExecId> maybe_runtime_eid{};

    if (compt == compt::compt) {
        maybe_compt_eid = ComptExprSolver{def_visitor}.solve_expr(
            fid, lctx.scope, stmt->stmt.var_init_decl.rhs, maybe_tid);
        if (TypeTransformer<TypeContainsVar>{context}(maybe_tid.as_id())
            && maybe_compt_eid.has_value()) {
            maybe_tid = infer_type_from_exec(maybe_compt_eid.as_id());
        }
    } else {
        maybe_runtime_eid = solve_expr(fid, lctx, stmt->stmt.var_init_decl.rhs, maybe_tid);
        if (TypeTransformer<TypeContainsVar>{context}(maybe_tid.as_id())
            && maybe_compt_eid.has_value()) {
            maybe_tid = infer_type_from_exec(maybe_compt_eid.as_id());
        }
    }

    const DefId did = context.register_def(
        name, compt == compt::compt, storage == storage::statik, align, span, stmt,
        DefVariable{.type_id = maybe_tid ? maybe_tid.as_id() : fall_back_tid_which_may_contain_var,
                    .compt_value = maybe_compt_eid,
                    .moved = false},
        {});

    // record in scope
    context.insert_variable(lctx.scope, name, did);

    // always emplace the did
    block.defs.push_back(did);

    if (maybe_runtime_eid.has_value()) {
        block.execs.push_back(maybe_runtime_eid.as_id());
    }

    // TODO: do something special for non-compt static variables using a guard variable

    return {};
}

[[nodiscard]] OptId<ExecId> RuntimeSolver::handle_any_typed_expr(FileId fid, LexicalCtx lctx,
                                                                 const ast_expr_t* expr) {
    // TODO
    switch (expr->type) {
    case AST_EXPR_COMPT: {
        return ComptExprSolver{context, def_visitor}.solve_expr(fid, lctx.scope, expr);
    }
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
