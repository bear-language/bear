//
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
#include "compiler/hir/expr_solver_common.hpp"
#include "compiler/hir/indexing.hpp"
#include "compiler/hir/scope.hpp"
#include "compiler/hir/span.hpp"
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
    if (context.type(into_tid).holds<TypeVar>()) {
        return handle_any_typed_expr(fid, lctx, expr);
    }

    OptId<ExecId> maybe_eid{};

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
    case AST_EXPR_SAME_TYPE:
    case AST_EXPR_SIZEOF:
    case AST_EXPR_TYPE_ID:
    case AST_EXPR_TYPE_TO_STR:
        return ComptExprSolver{def_visitor}.solve_expr(fid, lctx.scope, expr, into_tid);
        // ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
    case AST_EXPR_LITERAL:
        maybe_eid = handle_literal(fid, expr, into_tid); // tries conversion if possible internally
        break;
    case AST_EXPR_ID:
        maybe_eid = handle_any_id(fid, lctx, expr);
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
    case AST_EXPR_FN_CALL:
    case AST_EXPR_TYPE:
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

    if (maybe_eid.empty()) {
        return {};
    }

    // empty list literal is not inferable, so do this here
    if (const auto& exec = context.exec(maybe_eid.as_id());
        exec.holds<ExecListLiteral>() && !exec.as<ExecListLiteral>().len()
        && context.type(into_tid).holds_any_of<TypeSlice, TypeArr>()) {
        return maybe_eid;
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
            && context.is_lvalue(maybe_eid.as_id())
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
        // recommend an address of
        if (into_ty.holds<TypePtr>() && into_ty.try_inner().has_value()
            && context.is_lvalue(maybe_eid.as_id())
            && context.assignable_and_inferable_from_type_to(maybe_tid.as_id(),
                                                             into_ty.try_inner().as_id())) {

            context.prev_diagnostic()->value = DiagnosticInfoValue{
                DiagnosticSubCode{.sub_code = diag_code::did_you_mean_to_take_the_address}};

            const Exec& exec = context.exec(maybe_eid.as_id());
            std::string replacement_str{'^'};
            replacement_str += context.symbol(context.symbol_id(exec.span));

            context.force_link_diagnostic(context.emplace_diagnostic_with_message_value(
                exec.span, diag_code::replace_with, diag_type::help,
                DiagnosticSymbolAfterMessage{
                    .sid = context.symbol_id(replacement_str),
                }));
        }

        return {};
    }

    return maybe_eid;
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
    DataArena move_arena{0x100}; // decently sized

    // move map stays the same as parent, but lexical scope nests
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
            const auto eid = context.emplace_exec(
                ExecReturn{.return_value = solve_expr(fid, lctx, stmt->stmt.return_stmt.expr)},
                Span{context, fid, stmt});
            block.push_back_exec(eid);
            return eid;
        }
    } else {
        if (stmt->stmt.return_stmt.expr) {
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
            && maybe_runtime_eid.has_value()) {
            bool mut = context.type(maybe_tid.as_id()).mut;
            maybe_tid = infer_type_from_exec(maybe_runtime_eid.as_id());
            maybe_tid = context.emplace_type(context.type(maybe_tid.as_id()).value,
                                             context.type(maybe_tid.as_id()).span, mut);
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
    case AST_EXPR_SAME_TYPE:
    case AST_EXPR_SIZEOF:
    case AST_EXPR_TYPE_ID:
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
    case AST_EXPR_FN_CALL:
    case AST_EXPR_TYPE:
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

OptId<ExecId> RuntimeSolver::handle_any_id(FileId fid, LexicalCtx lctx, const ast_expr_t* expr) {
    assert(expr->type == AST_EXPR_ID);
    return handle_any_id_impl<compt_or_runtime::runtime>(context, def_visitor, fid, lctx.scope,
                                                         expr->expr.id.slice, {});
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
        context.emplace_diagnostic_with_message_value(
            Span{context, fid, expr}, diag_code::cannot_convert_expression_to_type,
            diag_type::error, DiagnosticTypeAfterMessage{.tid = into_tid});
        return {};
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

OptId<ExecId> RuntimeSolver::handle_borrow(FileId fid, LexicalCtx lctx, const ast_expr_t* expr) {
    assert(expr->type == AST_EXPR_BORROW);

    const auto maybe_inner_eid = solve_expr(fid, lctx, expr->expr.borrow.borrowed);

    if (maybe_inner_eid.empty()) {
        return {};
    }

    const bool mut_borrow = expr->expr.borrow.mut;

    // make sure either:
    // - inner is an lvalue
    // or:
    // - inner is a promotable compt constant and this is an immut borrow

    const bool inner_is_lvalue = context.is_lvalue(maybe_inner_eid.as_id());
    const bool inner_is_compt = context.exec(maybe_inner_eid.as_id()).compt;

    if (!inner_is_lvalue && !inner_is_compt) {
        const Exec& exec = context.exec(maybe_inner_eid.as_id());
        context.emplace_diagnostic(exec.span, diag_code::cannot_borrow_temporary_value,
                                   diag_type::error);
        return {};
    }

    const auto maybe_inner_tid = context.infer_type_from_exec(maybe_inner_eid.as_id());
    if (maybe_inner_tid.empty()) {
        return {};
    }

    const Type& ty = context.type(maybe_inner_tid.as_id());

    // cannot mutably borrow immutable data
    if (!ty.mut && mut_borrow) {
        auto d0 = context.emplace_diagnostic_with_message_value(
            Span{context.exec(maybe_inner_eid.as_id()).span},
            diag_code::cannot_take_a_mutable_reference_to_immutable_value_of_type, diag_type::error,
            DiagnosticTypeAfterMessage{.tid = maybe_inner_tid.as_id()});
        const auto maybe_did = context.try_def_for_exec(maybe_inner_eid.as_id());

        if (maybe_did.has_value()) {
            const Def& def = context.def(maybe_did.as_id());
            auto d1 = context.emplace_diagnostic_with_message_value(
                def.span, diag_code::declared_here, diag_type::note,
                DiagnosticSymbolBeforeMessage{.sid = def.name});
            context.link_diagnostic(d0, d1);
        }

        // don't return none since this isn't such a bad error that it really poisons context beyond
        // the extent of this borrow
    }

    return context.emplace_exec(ExecBorrow{.borrowee = maybe_inner_eid.as_id(), .mut = mut_borrow},
                                Span{context, fid, expr});
}

OptId<ExecId> RuntimeSolver::handle_addr_of(FileId fid, LexicalCtx lctx, const ast_expr_t* expr) {
    assert(expr->type == AST_EXPR_ADDR_OF);

    const auto maybe_inner_eid = solve_expr(fid, lctx, expr->expr.addr_of.inner);

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

    const Type& ty = context.type(maybe_inner_tid.as_id());

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
    case builtin_type::charr:
        return ConstantValue{char{1}};
    case builtin_type::f32:
        return ConstantValue{float{1}};
    case builtin_type::f64:
        return ConstantValue{double{1}};
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

    const auto maybe_inner_eid = solve_expr(fid, lctx, expr->expr.unary.expr);
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
    bool ptr_mut{};
    {
        const Type& ty = context.type(inner_tid);
        if (ty.holds<TypePtr>()) {
            const Type& pointee = context.type(ty.as<TypePtr>().inner);
            ptr_viable = !pointee.holds<TypeBuiltin>()
                         || pointee.as<TypeBuiltin>().type != builtin_type::voidd;
            // pointer mutability lives on the pointer layer, like references
            ptr_mut = ty.mut;
        }
    }

    if (!ptr_viable) {
        diag_invalid_unary_operand(context, Span{context, fid, expr->expr.unary.op},
                                   context.exec(inner_eid).span, inner_tid);
        return {};
    }

    return context.emplace_exec(ExecDeref{.dereferenced = inner_eid, .mut = ptr_mut},
                                Span{context, fid, expr});
}

OptId<ExecId> RuntimeSolver::handle_inc_dec(FileId fid, LexicalCtx lctx, const ast_expr_t* expr,
                                            unary_op op, bool post) {
    assert(expr->type == AST_EXPR_PRE_UNARY || expr->type == AST_EXPR_POST_UNARY);
    assert(op == unary_op::inc || op == unary_op::dec);

    const auto maybe_lhs_eid = solve_expr(fid, lctx, expr->expr.unary.expr);
    if (maybe_lhs_eid.empty()) {
        return {};
    }
    const ExecId lhs_eid = maybe_lhs_eid.as_id();

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
            // increment by the pointed to type's width
            maybe_one = ConstantValue{
                context.pointer_size_bytes() == 8
                    // try_inner safe here cuz TypePtr
                    ? static_cast<uint64_t>(context.layout_for_type(ty.try_inner().as_id()).width)
                    : static_cast<uint32_t>(context.layout_for_type(ty.try_inner().as_id()).width)};
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

OptId<ExecId> RuntimeSolver::handle_assign(FileId fid, LexicalCtx lctx, const ast_expr_t* expr) {
    assert(expr->type == AST_EXPR_BINARY);

    const auto maybe_lhs_eid = solve_expr(fid, lctx, expr->expr.binary.lhs);
    if (maybe_lhs_eid.empty()) {
        return {};
    }
    const auto maybe_lhs_tid = infer_type_from_exec(maybe_lhs_eid.as_id());
    if (maybe_lhs_tid.empty()) {
        return {};
    }

    const auto maybe_rhs_eid = solve_expr(fid, lctx, expr->expr.binary.rhs);
    if (maybe_rhs_eid.empty()) {
        return {};
    }
    const auto maybe_rhs_tid = infer_type_from_exec(maybe_rhs_eid.as_id());
    if (maybe_rhs_tid.empty()) {
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
    if (!context.assignable_from_type_to(maybe_rhs_tid.as_id(), maybe_lhs_tid.as_id())) {
        DiagLinker dl{context};
        dl.link(context.emplace_diagnostic_with_message_value(
            Span{context, fid, expr}, diag_code::cannot_convert_value_of_type, diag_type::error,
            DiagnosticTypeToType{.from = maybe_rhs_tid.as_id(), .to = maybe_lhs_tid.as_id()}));
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
    return context.emplace_exec(
        ExecAssignment{.lhs = maybe_lhs_eid.as_id(), .rhs = maybe_rhs_eid.as_id()},
        Span{context, fid, expr});
}

OptId<ExecId> RuntimeSolver::handle_is(FileId fid, LexicalCtx lctx, const ast_expr_t* expr) {
    // TODO
    return {};
}
OptId<ExecId> RuntimeSolver::handle_as(FileId fid, LexicalCtx lctx, const ast_expr_t* expr) {
    assert(expr->type == AST_EXPR_BINARY);
    return {};
}

OptId<ExecId> RuntimeSolver::handle_access(FileId fid, LexicalCtx lctx, const ast_expr_t* expr,
                                           access_op op) {
    assert(expr->type == AST_EXPR_BINARY);

    // !!!!!!!!!!!!!!!!!!!!!!
    return {}; // TODO
    // !!!!!!!!!!!!!!!!!!!!!!

    OptId<ExecId> maybe_lhs = solve_expr(fid, lctx, expr->expr.binary.lhs);
    if (!maybe_lhs.has_value()) {
        return {}; // poisoned
    }
    const auto lhs_eid = maybe_lhs.as_id();
    const Exec& lhs_exec = context.exec(lhs_eid);

    if (op == access_op::rarrow) {
        // TODO deref
    }

    const ast_expr_t* rhs = expr->expr.binary.rhs;

    auto matches_len_builtin = [this](token_ptr_slice_t id_slice) {
        return id_slice.len == 1
               && context.symbol_id(id_slice.start[0]) == context.symbol_id<"len">();
    };

    if (lhs_exec.holds<ExecListLiteral>() && rhs->type == AST_EXPR_ID) {
        const auto id_slice = rhs->expr.id.slice;
        if (matches_len_builtin(id_slice)) {
            // TODO
            return {};
        }
    }

    if (lhs_exec.holds<ExecConst>() && lhs_exec.as<ExecConst>().holds<SymbolId>()
        && rhs->type == AST_EXPR_ID) {
        const auto id_slice = rhs->expr.id.slice;
        if (matches_len_builtin(id_slice)) {
            // TODO
            return {};
        }
    }

    // TODO: use types instead of Exec checks for this
    //
    // be helpful if we do this:
    // [1, 2, 3].len() => should be just `len`
    // "123123123".len() => should be just `len`
    if ((lhs_exec.holds<ExecListLiteral>()
         || (lhs_exec.holds<ExecConst>() && lhs_exec.as<ExecConst>().holds<SymbolId>()))
        && rhs->type == AST_EXPR_FN_CALL && rhs->expr.fn_call.left_expr->type == AST_EXPR_ID) {
        const auto id_slice = rhs->expr.fn_call.left_expr->expr.id.slice;
        if (matches_len_builtin(id_slice)) {
            const auto sid = context.symbol_id<"len">();
            auto d0 = context.emplace_diagnostic_with_message_value(
                Span{context, fid, rhs}, diag_code::tried_to_call_property, diag_type::error,
                DiagnosticSymbolAfterMessage{.sid = sid});
            auto d1 = context.emplace_diagnostic_with_message_value(
                Span{context, fid, rhs}, diag_code::replace_with, diag_type::help,
                DiagnosticSymbolAfterMessage{.sid = sid});
            context.link_diagnostic(d0, d1);
            return std::nullopt;
        }
    }

    const ast_expr_t* rhs_expr = expr->expr.binary.rhs;
    Span rhs_span{context, fid, rhs_expr};

    // TODO: update to use types
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

        auto maybe_mem_var = context.look_up_variable(
            context.def(lhs_exec.as<ExecUnionInit>().union_def_id).template as<DefUnion>().scope,
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
                Span{context, fid, id_slice}, diag_code::compt_union_holds_field, diag_type::note,
                DiagnosticSymbolAfterMessage{curr_member.name}));
            dlinker.link(context.emplace_diagnostic_with_message_value(
                union_def.span, diag_code::declared_here, diag_type::note,
                DiagnosticSymbolBeforeMessage{.sid = union_def.name}));
            return {};
        }

        const Exec& mem_exec = context.exec(lhs_exec.as<ExecUnionInit>().member_init);

        return context.emplace_exec(mem_exec.value, Span{context, fid, expr});
    }

    if (rhs_expr->type == AST_EXPR_FN_CALL) {
        // return solve_fn_call(fid, lctx, rhs_expr, lhs_eid);
        // TODO
        return {};
    }

    // TODO use types / make this not compt-only
    if (!lhs_exec.holds<ExecStructInit>()) {
        auto d0
            = context.emplace_diagnostic(Span{context, fid, expr->expr.binary.op},
                                         diag_code::cannot_access_a_member_value, diag_type::error);
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
    const Def& struct_def = context.def(lhs_exec.as<ExecStructInit>().struct_def_id);

    if (rhs_expr->type == AST_EXPR_ID) {
        assert(struct_def.holds<DefStruct>());
        token_ptr_slice_t id_slice = rhs_expr->expr.id.slice;
        if (id_slice.len > 1) {
            context.emplace_diagnostic(rhs_span, diag_code::scoped_identifer_not_allowed_here,
                                       diag_type::error);
        }

        auto maybe_mem_var = context.look_up_member_var_guarding_hid(
            struct_def, context.symbol_id(id_slice.start[0]), rhs_span, lctx.scope);

        if (maybe_mem_var.empty()) {
            return std::nullopt; // posioned
        }

        const Def& var_def = context.def(maybe_mem_var.as_id());

        const Exec& mem_exec
            = context.exec(lhs_exec.as<ExecStructInit>().member_inits.get(var_def.member_idx));

        return context.emplace_compt_exec(mem_exec.value, Span{context, fid, expr});
    }

    const auto prior_diag_cnt = context.diagnostic_count();

    const ExecStructInit lhs_struct_init = lhs_exec.as<ExecStructInit>();

    // try a member index look-up: foo.0, foo.1, etc
    const auto maybe_rhs = ComptExprSolver{def_visitor}.solve_builtin_compt_expr(
        fid, lctx.scope, rhs_expr, builtin_type::u32);

    if (maybe_rhs.has_value()) {
        const std::optional<HirSize> maybe_mem_idx
            = context.try_member_index(lhs_struct_init.struct_def_id, maybe_rhs.as_id());
        if (maybe_mem_idx.has_value()) {
            const Exec& mem_exec
                = context.exec(lhs_struct_init.member_inits.get(maybe_mem_idx.value()));

            return context.emplace_compt_exec(mem_exec.value, Span{context, fid, expr});
        }
    }

    // force link this if diagnostics were just emitted
    if (context.diagnostic_count() > prior_diag_cnt) {
        context.force_link_diagnostic(context.emplace_diagnostic(
            Span{context, fid, rhs_expr}, diag_code::value_does_not_refer_to_a_member,
            diag_type::note));
    } else {
        context.emplace_diagnostic(Span{context, fid, rhs_expr},
                                   diag_code::value_does_not_refer_to_a_member, diag_type::error);
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

    // TODO: warn here for always true/always false
    if (lhs.has_value() && (op == binary_op::bool_or || op == binary_op::bool_and)) {
        const Exec& lhs_exec = context.exec(lhs.as_id());
        if (lhs_exec.holds<ExecConst>()) {
            const std::optional<ExecConst> econst
                = lhs_exec.as<ExecConst>().try_safe_convert_to(context, builtin_type::boolean);
            if (econst.has_value()) {
                const bool bval = econst.value().as<bool>();
                // false && ____ => always false
                if ((op == binary_op::bool_and) && !bval) {
                    // TODO: warn
                    const auto _ = solve_expr(fid, lctx, expr->expr.binary.rhs);
                    return context.emplace_compt_exec(ExecConst{false}, Span{context, fid, expr});
                }
                // true || ____ => always true
                if ((op == binary_op::bool_or) && bval) {
                    // TODO: warn
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

    // TODO: actually create an ExecBinary, make a solve_binary_exec
    return {};
}

} // namespace hir
