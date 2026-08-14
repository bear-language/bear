//     /                              /
//    /                              /
//   /_____  _____  _____  _____    /  _____   _  _  _____
//  /     / /____  /____/ /____/   /  /____/  /\  / /  __
// /_____/ /____  /    / /   \    /  /    /  /  \/ /____/
// Copyright (C) 2025-2026 Zachary Mahan
// Licensed under the GNU GPL v3. See LICENSE for details.

#ifndef BEARC_COMPILER_HIR_DEDUCTION_HPP
#define BEARC_COMPILER_HIR_DEDUCTION_HPP

#include "compiler/hir/indexing.hpp"

namespace hir {

struct DeductionStep {
    using id_type = DeductionStepId;
    /// the indexed order for this deduction step e.g. which argument we are
    HirSize order_idx{0};
    /// how deep inside a type to look
    HirSize depth{0};
    /// subindex into nested generics or function pointers
    HirSize sub_idx{0};
    /// the absent of next implies this is the terminal step
    OptId<DeductionStepId> next{};
};

} // namespace hir

#endif // BEARC_COMPILER_HIR_DEDUCTION_HPP
