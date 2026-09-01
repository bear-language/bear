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
#include "compiler/ast/type.h"
#include "compiler/hir/context.hpp"
#include "compiler/hir/def.hpp"
#include "compiler/hir/def_visitor.hpp"
#include "compiler/hir/diagnostic.hpp"
#include "compiler/hir/exec.hpp"
#include "compiler/hir/exec_ops.hpp"
#include "compiler/hir/exec_proving.hpp"
#include "compiler/hir/indexing.hpp"
#include "compiler/hir/layout.hpp"
#include "compiler/hir/matching.hpp"
#include "compiler/hir/scope.hpp"
#include "compiler/hir/span.hpp"
#include "compiler/hir/type.hpp"
#include "compiler/parser/token_eaters.h"
#include "compiler/token.h"
#include <cassert>
#include <cstddef>
#include <optional>
#include <utility>
namespace hir {

class ComptExprSolver {
    Context& context;
    DefVisitor& def_visitor;
    HirSize call_depth{0};

  public:
    static constexpr HirSize MAX_COMPT_CALL_FRAMES = 400;

    ComptExprSolver(Context& ctx, DefVisitor& def_visitor)
        : context{ctx}, def_visitor{def_visitor} {}

    [[nodiscard]] Context& get_context() { return this->context; }

    [[nodiscard]] OptId<ExecId> solve_expr(FileId fid, ScopeId scope, const ast_expr_t* expr) {
        return solve_expr(fid, scope, expr, std::nullopt);
    }

    [[nodiscard]] OptId<TypeId> infer_type_from_compt_expr(FileId fid, ScopeId scope,
                                                           const ast_expr_t* expr) {
        OptId<ExecId> maybe_eid = solve_expr(fid, scope, expr);

        if (maybe_eid.empty()) {
            return {}; // already an issue/poisoned
        }

        return infer_type_from_exec(maybe_eid.as_id());
    }

    [[nodiscard]] OptId<TypeId> infer_type_from_exec(ExecId eid);
    // solves a top level compt expr (this is primarily for array sizing & builtin types for top
    // level generic instantiation with compt parameterizations)
    [[nodiscard]] OptId<ExecId> solve_expr(FileId fid, ScopeId scope, const ast_expr_t* expr,
                                           OptId<TypeId> maybe_into_tid);
    [[nodiscard]] OptId<ExecId> handle_any_typed_expr(FileId fid, ScopeId scope,
                                                      const ast_expr_t* expr);

    [[nodiscard]] OptId<ExecId> solve_builtin_compt_expr(FileId fid, ScopeId scope,
                                                         const ast_expr_t* expr,
                                                         std::optional<builtin_type> into_builtin,
                                                         OptId<TypeId> into_tid = {});

    /// tries to make a function call at compile-time. internally, this memoizes all results inside
    /// of context. the memozation simply maps argument values to return value.
    ///
    /// internally, the logic works just like generic instantiation, but for regular function calls:
    ///
    /// args: {compt_exec1, compt_exec2, ...} -> canonicalize values inside context -> get a
    /// CanonicalArgsId back -> use that Id to key into a map (CanonicalArgsId -> ExecId) inside of
    /// context -> get an ExecId value for the given Id, or if no value exists yet for that Id,
    /// calculate the ExecId value and store it at that Id for future calls
    ///
    /// - arg_vec - should already be verified to be correct for this function (appropriate types of
    /// execs for functions params, all compt values)
    /// - prior_diag_cnt - just pass in context.diagnostic_count() if in doubt
    /// - span - span of the function call expression
    [[nodiscard]] OptId<ExecId> try_compt_fn_call(DefId func_did,
                                                  const llvm::SmallVectorImpl<ExecId>& arg_vec,
                                                  int prior_diag_cnt,
                                                  Span span = Span::generated());

  private:
    void enter_compt_fn() { ++call_depth; }
    void exit_compt_fn() { --call_depth; }

    ///
    /// solve a struct's value at compile-time, this essentially attempts a canonicalization down
    /// to a struct-init eexpression where each field is evaluatable at compile-time
    ///
    [[nodiscard]] OptId<ExecId> solve_struct_or_union(FileId fid, ScopeId scope,
                                                      const ast_expr_t* expr,
                                                      OptId<TypeId> into_tid);

    [[nodiscard]] OptId<ExecId> handle_union_init(FileId fid, ScopeId scope, DefId union_did,
                                                  const ast_expr_t* expr);

    [[nodiscard]] OptId<ExecId> handle_struct_init(FileId fid, ScopeId scope, DefId struct_did,
                                                   const ast_expr_t* expr, OptId<TypeId> into_tid);

    [[nodiscard]] OptId<ExecId> solve_compt_cast(FileId fid, ScopeId scope, ExecId eid,
                                                 const ast_expr_t* into_expr);

    [[nodiscard]] OptId<ExecId> handle_cast(ExecId eid, TypeId into_tid);

    [[nodiscard]] OptId<ExecId> solve_binary_compt_exec(ExecId lhs_eid, binary_op op,
                                                        ExecId rhs_eid);
    [[nodiscard]] OptId<TypeId> resolve_type(FileId fid, ScopeId scope, const ast_type_t* type);

    [[nodiscard]] OptId<TypeId> resolve_type(FileId fid, ScopeId scope, const ast_type_t* type,
                                             bool need_layout_info);

    [[nodiscard]] bool guard_incompatible_types(const Exec& lhs, const Exec& rhs, ExecConst lhs_val,
                                                ExecConst rhs_val);

    [[nodiscard]] bool guard_op_not_viable_for_types(const Exec& lhs, const Exec& rhs,
                                                     ExecConst lhs_val, binary_op op,
                                                     ExecConst rhs_val);

    static void guard_try_converge_types(ExecConst& lhs_val, binary_op op, ExecConst& rhs_val);

    [[nodiscard]] OptId<ExecId> handle_binary_scalar(const Exec& lhs, binary_op op,
                                                     const Exec& rhs);

    [[nodiscard]] OptId<ExecId> handle_binary_bitwise(const Exec& lhs, binary_op op,
                                                      const Exec& rhs);

    [[nodiscard]] OptId<ExecId> handle_binary_range(ExecId lhs_eid, binary_op op, ExecId rhs_eid);

    [[nodiscard]] OptId<ExecId> handle_binary_bool_conj_disj(const Exec& lhs, binary_op op,
                                                             const Exec& rhs);

    [[nodiscard]] OptId<ExecId> solve_preunary_exec(unary_op op, Span op_span, ExecId eid);

    [[nodiscard]] OptId<ExecId> solve_ternary_if(FileId fid, ScopeId scope,
                                                 const ast_expr_t* tern_expr,
                                                 OptId<TypeId> maybe_into_tid);

    [[nodiscard]] OptId<ExecId> solve_list(FileId fid, ScopeId scope, const ast_expr_t* expr,
                                           OptId<TypeId> maybe_into_tid);

    [[nodiscard]] OptId<ExecId> handle_list_literal(FileId fid, ScopeId scope,
                                                    const ast_expr_t* list_expr,
                                                    OptId<TypeId> maybe_into_type);

    [[nodiscard]] OptId<ExecId> handle_any_id(FileId fid, ScopeId scope,
                                              token_ptr_slice_t id_slice) {
        return handle_any_id(fid, scope, id_slice, OptId<GenericArgIdSliceId>{});
    }

    [[nodiscard]] OptId<ExecId> handle_any_generic_id(FileId fid, ScopeId scope,
                                                      token_ptr_slice_t id_slice,
                                                      ast_slice_of_generic_args_t gen_args);

    // tries to get the const value corresponding to some variable's name, if it exists
    [[nodiscard]] OptId<ExecId> handle_any_id(FileId fid, ScopeId scope, token_ptr_slice_t id_slice,
                                              OptId<GenericArgIdSliceId> maybe_gen_args);

    [[nodiscard]] OptId<ExecId> handle_same_type(FileId fid, ScopeId scope,
                                                 const ast_expr_t* same_type_expr);

    [[nodiscard]] OptId<ExecId> handle_inferable_as(FileId fid, ScopeId scope,
                                                    const ast_expr_t* infas_expr);

    [[nodiscard]] OptId<ExecId> handle_type_to_str(FileId fid, ScopeId scope,
                                                   const ast_expr_t* tts_expr);

    [[nodiscard]] OptId<ExecId> handle_static_assert(FileId fid, ScopeId scope,
                                                     const ast_expr_t* sass_expr);

    [[nodiscard]] OptId<ExecId> handle_diag_expr(FileId fid, ScopeId scope, const ast_expr_t* expr);

    [[nodiscard]] OptId<ExecId> solve_expr_binary(FileId fid, ScopeId scope,
                                                  const ast_expr_t* expr);

    [[nodiscard]] OptId<ExecId> handle_defined(FileId fid, ScopeId scope, const ast_expr_t* expr);

    static bool exec_is_compt_viable(const Exec& exec) {
        return exec.holds_any_of<ExecConst, ExecExprStructInit, ExecListLiteral, ExecUnionInit,
                                 ExecExprVariantInit>();
    }

    struct FuncLookUp {
        DefId func_did;
        OptId<ExecId> maybe_variant_init_res;
        bool needs_generic_deduction{false};
        bool did_variant_initializer{false};
    };

    /// tries to look up a function from an expression, and optionally a "self" value, if it exists
    ///
    /// self values are just the the left side of this: self.do_something()
    [[nodiscard]] std::optional<FuncLookUp>
    try_fn_look_up_from_expr(FileId fid, ScopeId scope, const ast_expr_t* expr,
                             OptId<ExecId> maybe_self_val = std::nullopt);

    /// mutates out_args and func_did as needed
    ///
    /// - func_did is mutated only if we use the args to deduce generic arguments and thus update
    /// the func_did from a raw DefGenericFunction to a concrete, instantiated DefFunction
    ///
    /// returns true on success, else false
    [[nodiscard]] bool try_fn_args_from_expr_and_preliminary_function_def_id(
        FileId fid, ScopeId scope, const ast_expr_t* expr, OptId<ExecId> maybe_self_val,
        DefId& func_did, bool needs_generic_deduction, llvm::SmallVectorImpl<ExecId>& out_args);

    [[nodiscard]] OptId<ExecId> solve_fn_call(FileId fid, ScopeId scope, const ast_expr_t* expr,
                                              OptId<ExecId> maybe_self_val = std::nullopt);

    [[nodiscard]] OptId<ExecId> try_convert_to(ExecId eid, TypeId into_tid);

    [[nodiscard]] OptId<ExecId> solve_expr_borrow(FileId fid, ScopeId scope, const ast_expr_t* expr,
                                                  OptId<TypeId> maybe_into_tid);

    [[nodiscard]] OptId<ExecId> solve_expr_subscript(FileId fid, ScopeId scope,
                                                     const ast_expr_t* expr);

    // TODO finish moving these definitions over to the source file

    [[nodiscard]] OptId<ExecId> solve_list_len(const Exec& list_exec, Span len_span) {
        assert(list_exec.holds<ExecListLiteral>());
        return context.emplace_exec(ExecConst{list_exec.as<ExecListLiteral>().len()},
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
                                              binary_op eq_neq);

    [[nodiscard]] OptId<ExecId> solve_struct_eq(const Exec& list1, const Exec& list2,
                                                binary_op eq_neq);

    [[nodiscard]] OptId<ExecId> solve_fn_ptr_eq(const Exec& fnp1, const Exec& fnp2,
                                                binary_op eq_neq) {
        return context.emplace_compt_exec(
            ExecConst{((eq_neq == binary_op::bool_equal)
                           ? fnp1.as<ExecFnPtr>().func_def_id == fnp2.as<ExecFnPtr>().func_def_id
                           : fnp1.as<ExecFnPtr>().func_def_id != fnp2.as<ExecFnPtr>().func_def_id)},
            Span::combine(fnp1.span, fnp2.span));
    }

    [[nodiscard]] OptId<ExecId> solve_any_eq(ExecId eid1, ExecId eid2, binary_op eq_neq) {
        const bool equiv = equivalent_exec(context, eid1, eid2);
        return context.emplace_compt_exec(
            ExecConst{(eq_neq == binary_op::bool_equal) ? equiv : !equiv},
            Span::combine(context.exec(eid1).span, context.exec(eid2).span));
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
        DiagLinker dl{context};
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
        scope = context.def(var_init.variant_def_id).template as<DefVariant>().scope;
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
            dl.link(context.emplace_diagnostic(
                span, diag_code::variant_decomposition_not_allowed_here, diag_type::error));
            dl.link(context.emplace_diagnostic_with_message_value(
                span, diag_code::replace_with, diag_type::help,
                DiagnosticIdentifierAfterMessage{.sid_slice = sid_slice}));
            // don't return here, since we can safely just move on here
        }

        if (!context.check_variant_field_has_parent(dl, hopefully_var_field,
                                                    var_init.variant_def_id,
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
                *this, scope, maybe_pattern_scope.as_id(), fid, variant_did, match_expr);
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

        const OptId<ExecId> maybe_pattern_eid = solve_expr(fid, scope, pattern_expr);

        if (maybe_pattern_eid.empty()) {
            return {};
        }

        // handle range case
        if (context.exec(maybe_pattern_eid.as_id()).template holds<ExecRange>()
            && matched_exec.holds<ExecConst>() && matched_exec.as<ExecConst>().is_integral()) {
            if (value_inside_range(context, maybe_pattern_eid.as_id(), matched_eid)) {
                return true;
            }
        }

        return equivalent_exec(context, maybe_pattern_eid.as_id(), matched_eid);
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
    // both ExecIds must be list literals
    [[nodiscard]] OptId<ExecId> solve_list_concat(ExecId lhs_eid, ExecId rhs_eid) {
        const Exec& lhs_exec = context.exec(lhs_eid);
        const Exec& rhs_exec = context.exec(rhs_eid);

        if (lhs_exec.as<ExecListLiteral>().elem_type_id.has_value()
            && rhs_exec.as<ExecListLiteral>().elem_type_id.has_value()
            && !context.equivalent_type(lhs_exec.as<ExecListLiteral>().elem_type_id.as_id(),
                                        rhs_exec.as<ExecListLiteral>().elem_type_id.as_id())) {
            auto d0 = context.emplace_diagnostic(Span::combine(lhs_exec.span, rhs_exec.span),
                                                 diag_code::invalid_operands_for_binary_expression,
                                                 diag_type::error);
            auto d1 = context.emplace_diagnostic_with_message_value(
                lhs_exec.span, diag_code::value_is_of_type, diag_type::note,
                DiagnosticTypeAfterMessage{.tid = infer_type_from_exec(lhs_eid).as_id()});
            auto d2 = context.emplace_diagnostic_with_message_value(
                rhs_exec.span, diag_code::value_is_of_type, diag_type::note,
                DiagnosticTypeAfterMessage{.tid = infer_type_from_exec(rhs_eid).as_id()});
            context.link_diagnostic(d0, d1);
            context.link_diagnostic(d1, d2);
            return {};
        }
        llvm::SmallVector<ExecId> list_vec{};

        const auto lhs_slice = lhs_exec.as<ExecListLiteral>().elems;

        for (auto eidx = lhs_slice.begin(); eidx != lhs_slice.end(); ++eidx) {
            list_vec.push_back(context.exec_id(eidx));
        }

        const auto rhs_slice = rhs_exec.as<ExecListLiteral>().elems;

        for (auto eidx = rhs_slice.begin(); eidx != rhs_slice.end(); ++eidx) {
            list_vec.push_back(context.exec_id(eidx));
        }

        IdSlice<ExecId> combined_slice = context.freeze_id_vec(list_vec);

        OptId<TypeId> maybe_elem_type_id{};

        if (lhs_exec.as<ExecListLiteral>().elem_type_id.has_value()) {
            maybe_elem_type_id = lhs_exec.as<ExecListLiteral>().elem_type_id;
        } else if (rhs_exec.as<ExecListLiteral>().elem_type_id.has_value()) {
            maybe_elem_type_id = rhs_exec.as<ExecListLiteral>().elem_type_id;
        }

        return context.emplace_compt_exec(
            ExecListLiteral{.elems = combined_slice, .elem_type_id = maybe_elem_type_id},
            Span::combine(lhs_exec.span, rhs_exec.span));
    }

    [[nodiscard]] OptId<ExecId> solve_members_of(FileId fid, ScopeId scope,
                                                 const ast_expr_t* mems_of_expr) {
        assert(mems_of_expr->type == AST_EXPR_MEMBERS_OF);

        OptId<TypeId> maybe_tid = resolve_type(fid, scope, mems_of_expr->expr.members_of.type);

        if (maybe_tid.empty()) {
            return {}; // poisoned (some other issue)
        }

        const Type& ty = context.type(context.try_decay(maybe_tid.as_id()));

        const Span span{context, fid, mems_of_expr};

        const TypeId elem_tid = context.emplace_type(TypeBuiltin{.type = builtin_type::str},
                                                     Span::generated(), false);

        if (ty.holds<TypeStruct>()) {

            llvm::SmallVector<ExecId> strs{};

            DefId struct_did = ty.as<TypeStruct>().def_id;

            const IdSlice<DefId> ordered_members = context.ordered_defs_for(struct_did);

            for (auto didx = ordered_members.begin(); didx != ordered_members.end(); ++didx) {
                DefId did = context.def_id(didx);
                strs.push_back(context.emplace_compt_exec(ExecConst{context.def(did).name},
                                                          Span::generated()));
            }

            const IdSlice<ExecId> elems = context.freeze_id_vec(strs);

            return context.emplace_compt_exec(
                ExecListLiteral{.elems = elems, .elem_type_id = elem_tid}, span);
        }

        return context.emplace_compt_exec(ExecListLiteral{.elems = {}, .elem_type_id = elem_tid},
                                          span);
    }

    [[nodiscard]] OptId<ExecId> solve_statics_of(FileId fid, ScopeId scope,
                                                 const ast_expr_t* mems_of_expr) {
        assert(mems_of_expr->type == AST_EXPR_STATICS_OF);

        OptId<TypeId> maybe_tid = resolve_type(fid, scope, mems_of_expr->expr.statics_of.type);

        if (maybe_tid.empty()) {
            return {}; // poisoned (some other issue)
        }

        const Type& ty = context.type(context.try_decay(maybe_tid.as_id()));

        const Span span{context, fid, mems_of_expr};

        const TypeId elem_tid = context.emplace_type(TypeBuiltin{.type = builtin_type::str},
                                                     Span::generated(), false);

        if (ty.holds<TypeStruct>()) {

            llvm::SmallVector<ExecId> strs{};

            DefId struct_did = ty.as<TypeStruct>().def_id;

            const IdSlice<DefId> ordered_members = context.static_defs_for(struct_did);

            for (auto didx = ordered_members.begin(); didx != ordered_members.end(); ++didx) {
                DefId did = context.def_id(didx);
                strs.push_back(context.emplace_compt_exec(ExecConst{context.def(did).name},
                                                          Span::generated()));
            }

            const IdSlice<ExecId> elems = context.freeze_id_vec(strs);

            return context.emplace_compt_exec(
                ExecListLiteral{.elems = elems, .elem_type_id = elem_tid}, span);
        }

        return context.emplace_compt_exec(ExecListLiteral{.elems = {}, .elem_type_id = elem_tid},
                                          span);
    }

    [[nodiscard]] OptId<ExecId> try_compt_constant_from_did(FileId fid, ScopeId scope, DefId did,
                                                            SymbolId sid, Span span) {
        const Def& def = context.def(did);
        if (def.holds<DefVariable>() && def.as<DefVariable>().compt_value.has_value()) {
            return def.as<DefVariable>().compt_value;
        }
        if (def.holds<DefFunction>()) {
            const DefFunction& func_def = def.as<DefFunction>();
            return context.emplace_compt_exec(
                ExecFnPtr{.func_def_id = did,
                          .fn_ptr_tid
                          = context.emplace_type(TypeFnPtr{.param_types = func_def.param_types,
                                                           .return_type = func_def.return_type},
                                                 Span::generated(), false)},
                span);
        }
        context.emplace_diagnostic_with_message_value(
            span, diag_code::is_not_a_compile_time_constant, diag_type::error,
            DiagnosticSymbolBeforeMessage{.sid = sid});
        return {};
    }

    [[nodiscard]] OptId<ExecId> solve_reflected_id(FileId fid, ScopeId scope,
                                                   const ast_expr_t* expr) {
        assert(expr->type == AST_EXPR_REFLECTED_ID);

        OptId<ExecId> maybe_symbol_eid = solve_builtin_compt_expr(
            fid, scope, expr->expr.reflected_id.inner, builtin_type::str);

        if (maybe_symbol_eid.empty()) {
            return {}; // poisoned
        }

        SymbolId sid = context.exec(maybe_symbol_eid.as_id())
                           .template as<ExecConst>()
                           .template as<SymbolId>();

        const auto maybe_existing_did = context.look_up_variable(scope, sid);

        if (maybe_existing_did.empty()) {
            context.emplace_diagnostic_with_message_value(
                Span{context, fid, expr->expr.reflected_id.inner},
                diag_code::use_of_undeclared_identifier, diag_type::error,
                DiagnosticSymbolAfterMessage{.sid = sid});
            return {};
        }
        return try_compt_constant_from_did(fid, scope, maybe_existing_did.as_id(), sid,
                                           Span{context, fid, expr->expr.reflected_id.inner});
    }

    [[nodiscard]] OptId<ExecId> solve_reflected_scoped_id(FileId fid, ScopeId scope,
                                                          const ast_expr_t* expr) {
        assert(expr->type == AST_EXPR_REFLECTED_SCOPED_ID);

        llvm::SmallVector<SymbolId> sid_vec{};

        token_ptr_slice_t id_slice = expr->expr.reflected_scoped_id.scoped_id_prefix;

        for (size_t i = 0; i < id_slice.len; ++i) {
            sid_vec.push_back(context.symbol_id(id_slice.start[i]));
        }

        OptId<ExecId> maybe_symbol_eid = solve_builtin_compt_expr(
            fid, scope, expr->expr.reflected_scoped_id.reflected_id, builtin_type::str);

        if (maybe_symbol_eid.empty()) {
            return {}; // poisoned
        }

        SymbolId sid = context.exec(maybe_symbol_eid.as_id())
                           .template as<ExecConst>()
                           .template as<SymbolId>();

        sid_vec.push_back(sid);

        const auto sid_slice = context.freeze_id_vec(sid_vec);

        const auto maybe_existing_did
            = context.look_up_scoped_variable(scope, sid_slice, Span{context, fid, expr});

        if (maybe_existing_did.empty()) {
            context.emplace_diagnostic_with_message_value(
                Span{context, fid, expr->expr.reflected_scoped_id.reflected_id},
                diag_code::use_of_undeclared_identifier, diag_type::error,
                DiagnosticIdentifierAfterMessage{.sid_slice = sid_slice});
            return {};
        }
        const Def& def = context.def(maybe_existing_did.as_id());
        if (def.holds<DefVariable>() && def.as<DefVariable>().compt_value.has_value()) {
            return def.as<DefVariable>().compt_value;
        }
        if (def.holds<DefFunction>()) {
            const DefFunction& func_def = def.as<DefFunction>();
            return context.emplace_compt_exec(
                ExecFnPtr{.func_def_id = maybe_existing_did.as_id(),
                          .fn_ptr_tid
                          = context.emplace_type(TypeFnPtr{.param_types = func_def.param_types,
                                                           .return_type = func_def.return_type},
                                                 Span::generated(), false)},
                Span{context, fid, expr->expr.reflected_scoped_id.reflected_id});
        }
        context.emplace_diagnostic_with_message_value(
            Span{context, fid, expr->expr.reflected_scoped_id.reflected_id},
            diag_code::is_not_a_compile_time_constant, diag_type::error,
            DiagnosticIdentifierBeforeMessage{.sid_slice = sid_slice});
        return {};
    }
    [[nodiscard]] OptId<ExecId> solve_sizeof(FileId fid, ScopeId scope, const ast_expr_t* expr) {
        assert(expr->type == AST_EXPR_SIZEOF);

        OptId<TypeId> maybe_tid
            = resolve_type(fid, scope, expr->expr.size_of.type, true); // need layout info

        if (maybe_tid.empty()) {
            return {}; // poisoned
        }

        Layout lay = context.layout(layout_for_type(context, maybe_tid.as_id()));

        return context.emplace_compt_exec(
            ExecConst{static_cast<size_t>(lay.width)}, // ensure this is size
            Span{context, fid, expr});
    }
    [[nodiscard]] OptId<ExecId> solve_alignof(FileId fid, ScopeId scope, const ast_expr_t* expr) {
        assert(expr->type == AST_EXPR_ALIGNOF);

        OptId<TypeId> maybe_tid
            = resolve_type(fid, scope, expr->expr.align_of.type, true); // need layout info

        if (maybe_tid.empty()) {
            return {}; // poisoned
        }

        Layout lay = context.layout(layout_for_type(context, maybe_tid.as_id()));

        return context.emplace_compt_exec(
            ExecConst{static_cast<size_t>(lay.alignment)}, // ensure this is size
            Span{context, fid, expr});
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

} // namespace hir
#endif
