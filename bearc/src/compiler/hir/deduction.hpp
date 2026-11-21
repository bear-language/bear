//     /                              /
//    /                              /
//   /_____  _____  _____  _____    /  _____   _  _  _____
//  /     / /____  /____/ /____/   /  /____/  /\  / /  __
// /_____/ /____  /    / /   \    /  /    /  /  \/ /____/
// Copyright (C) 2025-2026 Zachary Mahan
// Licensed under the Apache License 2.0. See LICENSE for details.

#ifndef BEARC_COMPILER_HIR_DEDUCTION_HPP
#define BEARC_COMPILER_HIR_DEDUCTION_HPP

#include "compiler/ast/stmt.h"
#include "compiler/hir/indexing.hpp"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"

namespace hir {

struct DeductionStep {
    using id_type = DeductionStepId;
    /// the indexed order for this deduction step e.g. which argument we are
    HirSize order_idx{0};
    /// the depth inside the current type
    HirSize depth{0};
    /// subindex into nested generics or function pointers
    HirSize sub_idx{0};
    static constexpr HirSize RETURN_TYPE = HIR_SIZE_MAX;
    /// the absent of next implies this is the terminal step
    OptId<DeductionStepId> next{};
};

[[nodiscard]] const ast_stmt_t* try_unravel_instance_fields(const ast_stmt_t* stmt);

[[nodiscard]] OptId<GenericArgIdSliceId>
try_generic_args_from_deduction_guide(Context& context, DeductionGuideId guide_id,
                                      llvm::ArrayRef<OptId<TypeId>> tids_by_order_idx);

} // namespace hir

#endif // BEARC_COMPILER_HIR_DEDUCTION_HPP
