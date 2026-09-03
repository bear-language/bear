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
#include "compiler/hir/def_visitor.hpp"
#include "compiler/hir/diagnostic.hpp"
#include "compiler/hir/exec.hpp"
#include "compiler/hir/exec_ops.hpp"
#include "compiler/hir/exec_proving.hpp"
#include "compiler/hir/indexing.hpp"
#include "compiler/hir/layout.hpp"
#include "compiler/hir/scope.hpp"
#include "compiler/hir/span.hpp"
#include "compiler/hir/type.hpp"
#include "compiler/token.h"
#include <cassert>
#include <cstddef>
#include <optional>
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
                                                    const ast_expr_t* expr);

    [[nodiscard]] OptId<ExecId> handle_variant_init(FileId fid, ScopeId scope,
                                                    const ast_expr_t* fn_call_expr,
                                                    DefId variant_field_did);

    [[nodiscard]] OptId<ExecId> solve_is(FileId fid, ScopeId scope, ExecId eid,
                                         const ast_expr_t* pattern_expr);

    [[nodiscard]] OptId<ExecId> handle_match(FileId fid, ScopeId scope,
                                             const ast_expr_t* match_expr);

    [[nodiscard]] bool pattern_matches(FileId fid, ScopeId scope, const ast_expr_t* pattern_expr,
                                       ExecId matched_eid);

    /// returns true on variant decomp, else false
    bool try_variant_decomp(FileId fid, ScopeId pattern_scope, ScopeId branch_scope,
                            const ast_expr_t* pattern_expr,
                            ExecId variant_eid); // both ExecIds must be list literals

    [[nodiscard]] OptId<GenericArgId> lower_generic_arg(FileId fid, ScopeId scope,
                                                        const ast_generic_arg_t* gen_arg,
                                                        bool need_layout_info);

    [[nodiscard]] OptId<ExecId> solve_list_concat(ExecId lhs_eid, ExecId rhs_eid);

    [[nodiscard]] OptId<ExecId> solve_members_of(FileId fid, ScopeId scope,
                                                 const ast_expr_t* mems_of_expr);

    [[nodiscard]] OptId<ExecId> solve_statics_of(FileId fid, ScopeId scope,
                                                 const ast_expr_t* mems_of_expr);

    [[nodiscard]] OptId<ExecId> try_compt_constant_from_did(FileId fid, ScopeId scope, DefId did,
                                                            SymbolId sid, Span span);

    [[nodiscard]] OptId<ExecId> solve_reflected_id(FileId fid, ScopeId scope,
                                                   const ast_expr_t* expr);

    [[nodiscard]] OptId<ExecId> solve_reflected_scoped_id(FileId fid, ScopeId scope,
                                                          const ast_expr_t* expr);

    [[nodiscard]] OptId<ExecId> solve_sizeof(FileId fid, ScopeId scope, const ast_expr_t* expr);

    [[nodiscard]] OptId<ExecId> solve_alignof(FileId fid, ScopeId scope, const ast_expr_t* expr);
};

} // namespace hir
#endif
