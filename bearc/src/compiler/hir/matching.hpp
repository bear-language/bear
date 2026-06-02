//     /                              /
//    /                              /
//   /_____  _____  _____  _____    /  _____   _  _  _____
//  /     / /____  /____/ /____/   /  /____/  /\  / /  __
// /_____/ /____  /    / /   \    /  /    /  /  \/ /____/
// Copyright (C) 2025-2026 Zachary Mahan
// Licensed under the GNU GPL v3. See LICENSE for details.

#ifndef BEARC_COMPILER_HIR_MATCHING_HPP
#define BEARC_COMPILER_HIR_MATCHING_HPP

#include "compiler/ast/expr.h"
#include "compiler/hir/context.hpp"
#include "compiler/hir/def.hpp"
#include "compiler/hir/diagnostic.hpp"
#include "compiler/hir/expr_solver.hpp"
#include "compiler/hir/id_set.hpp"
#include "compiler/hir/indexing.hpp"
#include "utils/data_arena.hpp"
#include <cassert>

namespace hir {

/// validates the branches of a match expression for a variant, and emplaces any needed diagnostics
/// - checks for exhaustiveness
/// - checks that each branch is indeed a valid variant field for the provided variant
/// - checks for duplicates
/// - does NOT validate/evaluate individual variant decompositions
/// returns false when match is NOT exhaustive or is not valid due to some other more malformed
/// reason
template <IsExprSolver S>
bool valid_exhaustive_match_for_variant(S& solver, ScopeId scope, FileId fid, DefId variant_did,
                                        const ast_expr_t* match_expr) {

    assert(match_expr->type == AST_EXPR_MATCH);

    Context& context = solver.get_context();

    DataArena arena(0x200); // resonably sized

    Def variant_def = context.def(variant_did);

    assert(variant_def.holds<DefVariant>());

    IdSet<DefId> used_variant_fields{arena, 2 * variant_def.as<DefVariant>().ordered_members.len()};

    const ast_slice_of_exprs_t branches = match_expr->expr.match_expr.branches;

    DiagLinker dl{context};

    const ast_expr_t* else_pattern = nullptr;

    bool valid = true;

    for (size_t i = 0; i < branches.len; ++i) {

        const ast_expr_t* branch = branches.start[i];

        assert(branch->type == AST_EXPR_MATCH_BRANCH);

        const ast_slice_of_exprs_t patterns = branch->expr.match_branch.patterns;

        for (size_t j = 0; j < patterns.len; ++j) {

            const ast_expr_t* pattern = patterns.start[j];

            if (pattern->type == AST_EXPR_ELSE_MATCH_PATTERN) {
                if (else_pattern) {
                    dl.link(context.emplace_diagnostic(Span{context, fid, pattern},
                                                       diag_code::duplicate_else_match_branch,
                                                       diag_type::error));
                    dl.link(context.emplace_diagnostic(Span{context, fid, else_pattern},
                                                       diag_code::previous_def_here,
                                                       diag_type::note));
                    valid = false;
                }
                else_pattern = pattern;
                if (i != branches.len - 1) {
                    dl.link(context.emplace_diagnostic(
                        Span{context, fid, pattern}, diag_code::else_match_branch_should_come_last,
                        diag_type::warning));
                }
                continue;
            }
            std::optional<IdSlice<SymbolId>> maybe_sid_slice{};
            Span id_span{context, fid, pattern};
            if (pattern->type == AST_EXPR_ID) {
                maybe_sid_slice = context.symbol_slice(pattern->expr.id.slice);
            } else if (pattern->type == AST_EXPR_VARIANT_DECOMP) {
                maybe_sid_slice = context.symbol_slice(pattern->expr.variant_decomp.id);
                id_span = Span{context, fid, pattern->expr.variant_decomp.id};
            }
            if (!maybe_sid_slice.has_value()) {
                dl.link(context.emplace_diagnostic(
                    id_span, diag_code::invalid_pattern, diag_type::error,
                    DiagnosticSubCode{.sub_code = diag_code::not_a_variant_field}));

                valid = false;
                continue;
            }
            const auto sid_slice = maybe_sid_slice.value();
            const auto maybe_variant_field_did
                = context.look_up_scoped_type(scope, sid_slice, id_span);
            if (maybe_variant_field_did.empty()) {
                dl.link(context.emplace_diagnostic(
                    id_span, diag_code::use_of_undeclared_identifier, diag_type::error,
                    DiagnosticSubCode{.sub_code = diag_code::not_declared_in_this_scope}));

                valid = false;
                continue;
            }

            const auto variant_field_did = maybe_variant_field_did.as_id();

            if (!context.def(variant_field_did).holds<DefVariantField>()) {
                dl.link(context.emplace_diagnostic(
                    id_span, diag_code::invalid_pattern, diag_type::error,
                    DiagnosticSubCode{.sub_code = diag_code::not_a_variant_field}));
                valid = false;
                continue;
            }

            if (!context.check_variant_field_has_parent(variant_field_did, variant_did, id_span)) {
                valid = false;
                continue;
            }

            // already contained
            if (used_variant_fields.insert(variant_field_did)) {
                dl.link(context.emplace_diagnostic(Span{context, fid, pattern},
                                                   diag_code::duplicate_match_pattern,
                                                   diag_type::error));
                valid = false;
            }
        }
    }

    const auto variant_fields = variant_def.as<DefVariant>().ordered_members;

    if (!else_pattern && !(used_variant_fields.size() == variant_fields.len())) {
        Span span{context, fid, match_expr};
        dl.link(context.emplace_diagnostic(span, diag_code::match_expression_is_not_exhaustive,
                                           diag_type::error));

        valid = false;

        for (auto didx = variant_fields.begin(); didx != variant_fields.end(); ++didx) {
            const auto field_did = context.def_id(didx);
            if (!used_variant_fields.contains(field_did)) {
                dl.link(context.emplace_diagnostic(
                    span, diag_code::does_not_consider, diag_type::note,
                    DiagnosticIdentifierAfterMessage{
                        .sid_slice = context.try_singly_qualified_name(field_did)},
                    DiagnosticInfoNoPreview{}));
            }
        }
    }

    return valid;
}

} // namespace hir

#endif // !BEARC_COMPILER_HIR_MATCHING_HPP
