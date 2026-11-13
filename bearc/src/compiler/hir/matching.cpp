//     /                              /
//    /                              /
//   /_____  _____  _____  _____    /  _____   _  _  _____
//  /     / /____  /____/ /____/   /  /____/  /\  / /  __
// /_____/ /____  /    / /   \    /  /    /  /  \/ /____/
// Copyright (C) 2025-2026 Zachary Mahan
// Licensed under the Apache License 2.0. See LICENSE for details.

#include <compiler/hir/matching.hpp>
namespace hir {
bool check_for_multi_decomps_on_match_branch(Context& ctx, FileId fid,
                                             ast_expr_match_branch_t match_branch) {

    if (match_branch.patterns.len < 2) {
        return false;
    }

    for (size_t i = 0; i < match_branch.patterns.len; ++i) {
        if (match_branch.patterns.start[i]->type == AST_EXPR_VARIANT_DECOMP) {
            ctx.emplace_diagnostic(
                Span{ctx, fid, match_branch.patterns.start[0]->first,
                     match_branch.patterns.start[match_branch.patterns.len - 1]->last},
                diag_code::cannot_decompose_multiple_variants_on_the_same_match_arm,
                diag_type::error);
            return true;
        }
    }
    return false;
}
} // namespace hir
