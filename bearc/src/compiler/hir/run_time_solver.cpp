//
//     /                              /
//    /                              /
//   /_____  _____  _____  _____    /  _____   _  _  _____
//  /     / /____  /____/ /____/   /  /____/  /\  / /  __
// /_____/ /____  /    / /   \    /  /    /  /  \/ /____/
// Copyright (C) 2025-2026 Zachary Mahan
// Licensed under the Apache License 2.0. See LICENSE for details.

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

[[nodiscard]] OptId<ExecId> RuntimeSolver::solve_expr(FileId fid, LexicalCtx lctx,
                                                      const ast_expr_t* expr, TypeId into_tid) {
    const Type& ty = context.type(into_tid);
    if (ty.holds<TypeVar>()) {
        return handle_any_typed_expr(fid, lctx, expr);
    }

    // TODO
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
    case AST_EXPR_SIZEOF:
    case AST_EXPR_TYPE_ID:
        return ComptExprSolver{def_visitor}.solve_expr(fid, lctx.scope, expr, into_tid);
        // ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
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
    case AST_EXPR_STRUCT_INIT:
    case AST_EXPR_STRUCT_MEMBER_INIT:
    case AST_EXPR_CLOSURE:
    case AST_EXPR_TERNARY_IF:
    case AST_EXPR_VARIANT_DECOMP:
    case AST_EXPR_BLOCK:
    case AST_EXPR_MATCH_BRANCH:
    case AST_EXPR_MATCH:
    case AST_EXPR_ELSE_MATCH_PATTERN:
    case AST_EXPR_TUPLE_INIT:
    case AST_EXPR_INVALID:
        break;
    }
    return {};
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

[[nodiscard]] OptId<ExecId> RuntimeSolver::solve_block(FileId fid, LexicalCtx parent_lctx,
                                                       ast_slice_of_stmts_t stmts, Span block_span,
                                                       bool require_return) {

    InProgressBlock in_prog_block{};
    DataArena move_arena{0x100}; // decently sized

    // move map stays the same as parent, but lexical scope nests
    LexicalCtx curr_lctx{.scope = context.make_small_scope(parent_lctx.scope, block_span),
                         .map = parent_lctx.map};

    bool hit_block_terminator{false};

    const auto must_return = [this, require_return]() {
        return this->current_return_tid.has_value() && require_return;
    };

    for (auto i = 0uz; i < stmts.len; ++i) {
        const auto maybe_eid = handle_stmt(fid, curr_lctx, in_prog_block, stmts.start[i]);

        // if this is the last statement (and we didn't already see a (premature) return) and we're
        // expecting a return type
        //
        // TODO: don't issue a false positive for nested blocks
        if (!hit_block_terminator && i == stmts.len - 1 && must_return()
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
        if (!hit_block_terminator && maybe_eid.has_value()
            && context.exec(maybe_eid.as_id()).holds_any_of<ExecReturn, ExecJump>()) {

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
                context.type(this->current_return_tid.as_id()).span,
                diag_code::function_has_return_type, diag_type::note,
                DiagnosticTypeAfterMessage{.tid = this->current_return_tid.as_id()}));
            dl.link(context.emplace_diagnostic(
                block_span, diag_code::end_function_body_with_a_return_statement, diag_type::help));
        } else {
            context.emplace_diagnostic(block_span, diag_code::empty_block, diag_type::warning);
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

OptId<ExecId> RuntimeSolver::handle_return(FileId fid, LexicalCtx lctx, InProgressBlock& block,
                                           const ast_stmt_t* stmt) {
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
            const auto eid = context.emplace_exec(
                ExecReturn{.return_value = solve_expr(fid, lctx, stmt->stmt.return_stmt.expr)},
                Span{context, fid, stmt});
            block.push_back_exec(eid);
            return eid;
        }
    } else {
        if (stmt->stmt.return_stmt.expr) {
            context.emplace_diagnostic(Span{context, fid, stmt->stmt.return_stmt.expr},
                                       diag_code::function_does_not_return_a_value,
                                       diag_type::error);
        }
    }
    const auto eid = context.emplace_exec(ExecReturn{.return_value = {}}, Span{context, fid, stmt});
    block.push_back_exec(eid);
    return eid;
}

OptId<ExecId> RuntimeSolver::handle_stmt(FileId fid, LexicalCtx lctx, InProgressBlock& block,
                                         const ast_stmt_t* stmt, storage storage, compt compt,
                                         uint8_t align) {

    // TODO, finish:
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
        return handle_break(fid, block, stmt);
    case AST_STMT_CONTINUE:
        return handle_continue(fid, block, stmt);
    case AST_STMT_IF:
    case AST_STMT_ELSE:
    case AST_STMT_WHILE:
    case AST_STMT_FOR:
    case AST_STMT_FOR_IN:
    case AST_STMT_YIELD:

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
    block.defs.push_back(did);

    if (maybe_runtime_eid.has_value()) {
        ExecId assign_eid = context.emplace_exec(
            ExecAssignment{.lhs = context.emplace_exec(
                               ExecVariable{.def_id = did, .type_id = maybe_tid.as_id()}, span),
                           .rhs = maybe_runtime_eid.as_id()},
            span);
        block.push_back_exec(assign_eid);
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
        DefVariable{
            .type_id = maybe_tid ? maybe_tid.as_id() : fall_back_tid_which_may_contain_var,
            .compt_value = maybe_compt_eid,
        },
        {});

    // record in scope
    context.insert_variable(lctx.scope, name, did);

    // always emplace the did
    block.defs.push_back(did);

    if (maybe_runtime_eid.has_value()) {
        block.push_back_exec(maybe_runtime_eid.as_id());
    }

    // TODO: do something special for non-compt static variables using a guard variable

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

    SymbolId name = context.symbol_id(stmt->stmt.deftype.alias_id);

    const DefId did
        = context.register_def(name, /*compt=*/false, /*statik=*/false, 0, Span{context, fid, stmt},
                               stmt, DefDeftype{.type = maybe_tid.as_id()}, {});

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
    if (maybe_eid.has_value()) {
        /// TODO: warn unused based on the type of exec (fine for mutation / discardable functions,
        /// bad for everything else)
        block.push_back_exec(maybe_eid.as_id());
    }
    return maybe_eid;
}

OptId<ExecId> RuntimeSolver::handle_break(FileId fid, InProgressBlock& block,
                                          const ast_stmt_t* stmt) {
    assert(stmt->type == AST_STMT_BREAK);
    if (current_loop_block_eid.empty()) {
        context.emplace_diagnostic(Span{context, fid, stmt},
                                   diag_code::break_statement_outside_of_loop, diag_type::error);
        return {};
    }
    const auto eid = context.emplace_exec(
        ExecJump{.block = current_loop_block_eid.as_id(), .spot = jump_spot::end},
        Span{context, fid, stmt});
    block.push_back_exec(eid);
    return eid;
}

OptId<ExecId> RuntimeSolver::handle_continue(FileId fid, InProgressBlock& block,
                                             const ast_stmt_t* stmt) {
    assert(stmt->type == AST_STMT_CONTINUE);
    if (current_loop_block_eid.empty()) {
        context.emplace_diagnostic(Span{context, fid, stmt},
                                   diag_code::continue_statement_outside_of_loop, diag_type::error);
        return {};
    }
    const auto eid = context.emplace_exec(
        // basically, if this is a for loop, we want to execute the update exec at the end of this
        // iteration before continuing
        current_loop_update_eid.has_value()
            ? ExecJump{.block = current_loop_update_eid.as_id(), .spot = jump_spot::start}
            : ExecJump{.block = current_loop_block_eid.as_id(), .spot = jump_spot::start},
        Span{context, fid, stmt});

    block.push_back_exec(eid);
    return eid;
}

[[nodiscard]] OptId<ExecId> RuntimeSolver::handle_any_typed_expr(FileId fid, LexicalCtx lctx,
                                                                 const ast_expr_t* expr) {
    // TODO
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
    case AST_EXPR_SIZEOF:
    case AST_EXPR_TYPE_ID:
        return ComptExprSolver{def_visitor}.solve_expr(fid, lctx.scope, expr);
        // ^^^^^^^^^^^^^^^^^^^^^^^ ^^^^^^^^^^^^^^^^^^^^^ ^^^^^^^^^^^^^^^^^^^^^
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
    case AST_EXPR_STRUCT_INIT:
    case AST_EXPR_STRUCT_MEMBER_INIT:
    case AST_EXPR_CLOSURE:
    case AST_EXPR_TERNARY_IF:
    case AST_EXPR_VARIANT_DECOMP:
    case AST_EXPR_BLOCK:
    case AST_EXPR_MATCH_BRANCH:
    case AST_EXPR_MATCH:
    case AST_EXPR_ELSE_MATCH_PATTERN:
    case AST_EXPR_TUPLE_INIT:
    case AST_EXPR_INVALID:
        break;
    }
    return {};
}

} // namespace hir
