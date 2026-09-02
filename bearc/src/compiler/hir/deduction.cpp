//     /                              /
//    /                              /
//   /_____  _____  _____  _____    /  _____   _  _  _____
//  /     / /____  /____/ /____/   /  /____/  /\  / /  __
// /_____/ /____  /    / /   \    /  /    /  /  \/ /____/
// Copyright (C) 2025-2026 Zachary Mahan
// Licensed under the GNU GPL v3. See LICENSE for details.

#include "compiler/hir/deduction.hpp"
#include "compiler/hir/compt_expr_solver.hpp"
#include "compiler/hir/context.hpp"
#include "compiler/hir/def_visitor.hpp"
#include "compiler/hir/expr_solver.hpp"
#include "compiler/hir/indexing.hpp"
#include "compiler/hir/run_time_solver.hpp"

namespace hir {

[[nodiscard]] OptId<TypeId> deduction_step_helper(Context& context, TypeId tid,
                                                  DeductionStep step) {
    if (step.next.empty()) {
        TypeId curr = tid;
        while (step.depth) {
            const auto& ty = context.type(curr);
            if (ty.try_inner().empty()) {
                // we're at the canonical base
                break;
            }
            curr = ty.try_inner().as_id();
            --step.depth;
        }
        return curr;
    }

    if (step.next.empty()) {
        return {};
    }

    const auto try_nested_generic_tid
        = [&context, &step] [[nodiscard]] (GenericArgIdSliceId gen_args_slice_id) -> OptId<TypeId> {
        const auto args = context.gen_arg_id_slice(gen_args_slice_id);
        if (step.sub_idx >= args.len()) {
            return {};
        }
        const GenericArg arg = context.gen_arg(args.get(step.sub_idx));
        if (!arg.holds<TypeId>()) {
            return {};
        }
        return deduction_step_helper(context, arg.as<TypeId>(),
                                     context.deduction_step(step.next.as_id()));
    };

    const auto& ty = context.type(tid);
    if (ty.template holds<TypeStruct>()
        && ty.template as<TypeStruct>().gen_args_slice.has_value()) {
        return try_nested_generic_tid(ty.template as<TypeStruct>().gen_args_slice.as_id());
    }
    if (ty.template holds<TypeVariant>()
        && ty.template as<TypeVariant>().gen_args_slice.has_value()) {
        return try_nested_generic_tid(ty.template as<TypeVariant>().gen_args_slice.as_id());
    }
    if (ty.template holds<TypeFnPtr>()) {
        TypeFnPtr tfnp = ty.as<TypeFnPtr>();
        if (step.sub_idx == DeductionStep::RETURN_TYPE && tfnp.return_type.has_value()) {
            return deduction_step_helper(context, tfnp.return_type.as_id(),
                                         context.deduction_step(step.next.as_id()));
        }
        if (step.sub_idx >= tfnp.param_types.len()) {
            return {};
        }
        const auto sub_tid = context.type_id(tfnp.param_types.get(step.sub_idx));
        return deduction_step_helper(context, sub_tid, context.deduction_step(step.next.as_id()));
    }

    return {};
}

[[nodiscard]] OptId<GenericArgIdSliceId>
try_generic_args_from_deduction_guide(IsExprSolver auto& solver,
                                      const llvm::SmallVectorImpl<ExecId>& eids,
                                      DeductionGuideId guide_id) {
    Context& context = solver.get_context();
    llvm::SmallVector<GenericArgId> gen_args{};

    const IdSlice<DeductionStepId> guide = context.deduction_guide(guide_id);

    for (auto step_idx = guide.begin(); step_idx != guide.end(); ++step_idx) {
        const auto step = context.deduction_step(step_idx);
        if (step.order_idx >= eids.size()) {
            return {};
        }
        const auto maybe_tid = solver.infer_type_from_exec(eids[step.order_idx]);
        if (maybe_tid.empty()) {
            return {};
        }
        const auto tid = maybe_tid.as_id();
        const auto maybe_deduced_tid = deduction_step_helper(context, tid, step);
        if (maybe_deduced_tid.empty()) {
            return {};
        }
        gen_args.push_back(context.emplace_generic_arg(maybe_deduced_tid.as_id()));
    }

    const auto gargs = context.emplace_generic_arg_id_slice(context.freeze_id_vec(gen_args));
    return gargs;
}

template OptId<GenericArgIdSliceId> try_generic_args_from_deduction_guide<ComptExprSolver>(
    ComptExprSolver& solver, const llvm::SmallVectorImpl<ExecId>& eids, DeductionGuideId guide_id);
template OptId<GenericArgIdSliceId> try_generic_args_from_deduction_guide<RuntimeSolver>(
    RuntimeSolver& solver, const llvm::SmallVectorImpl<ExecId>& eids, DeductionGuideId guide_id);

} // namespace hir
