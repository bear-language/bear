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
#include "compiler/hir/exec.hpp"
#include "compiler/hir/exec_proving.hpp"
#include "compiler/hir/expr_solver.hpp"
#include "compiler/hir/fast_list.hpp"
#include "compiler/hir/id_set.hpp"
#include "compiler/hir/indexing.hpp"
#include "utils/data_arena.hpp"
#include <cassert>
#include <cstddef>
#include <iostream>

namespace hir {

/// validates the branches of a match expression for a variant, and emplaces any needed diagnostics
/// - checks for exhaustiveness
/// - checks that each branch is indeed a valid variant field for the provided variant
/// - checks for duplicates
/// - does NOT validate/evaluate individual variant decompositions
/// returns false when match is NOT exhaustive or is not valid due to some other more malformed
/// reason
template <IsExprSolver S>
bool valid_exhaustive_match_for_variant(S& solver, ScopeId containing_scope,
                                        ScopeId matched_variant_scope, FileId fid,
                                        DefId variant_did, const ast_expr_t* match_expr) {

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
            // try to look up inside the matched variant's scope, and then if it fails, look into
            // the containing scope to try to get more info (different variant, etc)
            auto maybe_variant_field_did
                = context.look_up_scoped_type(matched_variant_scope, sid_slice, id_span);
            if (maybe_variant_field_did.empty()) {
                maybe_variant_field_did
                    = context.look_up_scoped_type(containing_scope, sid_slice, id_span);
            }
            if (maybe_variant_field_did.empty()) {
                maybe_variant_field_did
                    = context.look_up_scoped_variable(containing_scope, sid_slice, id_span);
            }

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
                dl.link(context.emplace_diagnostic(
                    context.def(variant_field_did).span, diag_code::declared_here, diag_type::note,
                    DiagnosticSymbolBeforeMessage{.sid = context.def(variant_field_did).name},
                    DiagnosticSubCode{.sub_code = diag_code::not_a_variant_field}));

                valid = false;
                continue;
            }

            if (!context.check_variant_field_has_parent(dl, variant_field_did, variant_did,
                                                        id_span)) {
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

/// only ExecId's corresponds to ExecRanges should be provided
class RangeList {
    FastList<ExecId> list{};
    const Context& ctx;

  public:
    RangeList(const Context& ctx) : ctx{ctx} {}
    /// only ExecId's corresponds to ExecRanges should be provided
    void put(ExecId elem) { list.put_head(elem); }
    OptId<ExecId> search_overlapping(ExecId eid) {

        const Exec& range_exec = ctx.exec(eid);

        assert(range_exec.holds<ExecRange>());

        ExecId start = range_exec.as<ExecRange>().start;

        ExecConst start_val = ctx.exec(start).as<ExecConst>();

        bool is_signed = start_val.is_signed_integral();

        auto less_than_or_eq = [is_signed](ExecConst l, ExecConst r) {
            return (is_signed) ? l.less_than_or_equal_signed(r) : l.less_than_or_equal_unsigned(r);
        };

        auto greater_than_or_eq = [is_signed](ExecConst l, ExecConst r) {
            return (is_signed) ? l.greater_than_or_equal_signed(r)
                               : l.greater_than_or_equal_unsigned(r);
        };

        for (const ExecId eid : list) {
            const Exec& curr_range_exec = ctx.exec(eid);

            assert(curr_range_exec.holds<ExecRange>());

            ExecId curr_start = curr_range_exec.as<ExecRange>().start;
            ExecId curr_end = curr_range_exec.as<ExecRange>().end;

            ExecConst curr_start_val = ctx.exec(curr_start).as<ExecConst>();
            ExecConst curr_end_val = ctx.exec(curr_end).as<ExecConst>();

            // meaning overlapping
            if ((greater_than_or_eq(start_val, curr_start_val)
                 && (less_than_or_eq(start_val, curr_end_val)))
                || (less_than_or_eq(start_val, curr_start_val)
                    && (greater_than_or_eq(start_val, curr_end_val)))

            ) {
                return eid;
            }
        }
        return {};
    }
};

template <IsExprSolver S>
bool valid_exhaustive_match_for_non_variant(S& solver, ScopeId scope, FileId fid,
                                            const ast_expr_t* match_expr) {
    // TODO make this de-duplicate branches using exec hashing and an exec hash table!
    assert(match_expr->type == AST_EXPR_MATCH);

    Context& context = solver.get_context();

    const ast_slice_of_exprs_t branches = match_expr->expr.match_expr.branches;

    const OptId<ExecId> maybe_matched_eid
        = solver.solve_expr(fid, scope, match_expr->expr.match_expr.matched);

    if (maybe_matched_eid.empty()) {
        return false;
    }

    const auto matched_eid = maybe_matched_eid.as_id();

    {
        const Exec& matched_exec = context.exec(matched_eid);
        if (matched_exec.holds<ExecConst>()
            && (matched_exec.as<ExecConst>().holds<f32>()
                || matched_exec.as<ExecConst>().holds<f64>())) {

            const auto maybe_matched_tid = solver.infer_type_from_exec(matched_eid);

            if (maybe_matched_tid.empty()) {
                return false;
            }

            const auto matched_tid = maybe_matched_tid.as_id();

            auto d0 = context.emplace_diagnostic(
                matched_exec.span, diag_code::matching_on_floats_may_cause_unintended_behavior,
                diag_type::warning);
            auto d1 = context.emplace_diagnostic(
                matched_exec.span, diag_code::value_is_of_type, diag_type::note,
                DiagnosticTypeAfterMessage{.tid = matched_tid}, DiagnosticInfoNoPreview{});
            context.link_diagnostic(d0, d1);
        }
    }
    DiagLinker dl{context};

    const ast_expr_t* else_pattern = nullptr;

    bool valid = true;

    // decently sized
    DataArena arena{0x400};

    ExecHashMap exec_map{context, arena, 0x200};

    // optional so we can lazy init
    std::optional<RangeList> ranges{};

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

            const OptId<ExecId> maybe_pattern_eid = solver.solve_expr(fid, scope, pattern);
            if (maybe_pattern_eid.empty()) {
                continue;
            }
            const ExecId pattern_eid = maybe_pattern_eid.as_id();
            Context& ctx = solver.get_context();
            if (!possibly_equivalent_exec(ctx, matched_eid, pattern_eid)) {
                const auto& matched = ctx.exec(matched_eid);
                const auto& pattern = ctx.exec(pattern_eid);
                dl.link(ctx.emplace_diagnostic(pattern.span,
                                               diag_code::pattern_can_never_match_matched_value,
                                               diag_type::error));
                if (const OptId<TypeId> maybe_tid = solver.infer_type_from_exec(pattern_eid);
                    maybe_tid.has_value()) {
                    dl.link(ctx.emplace_diagnostic_with_message_value(
                        pattern.span, diag_code::pattern_is_of_type, diag_type::note,
                        DiagnosticTypeAfterMessage{.tid = maybe_tid.as_id()}));
                }
                if (const OptId<TypeId> maybe_tid = solver.infer_type_from_exec(matched_eid);
                    maybe_tid.has_value()) {
                    dl.link(ctx.emplace_diagnostic_with_message_value(
                        matched.span, diag_code::matched_value_is_of_type, diag_type::note,
                        DiagnosticTypeAfterMessage{.tid = maybe_tid.as_id()}));
                }
                continue;
            }
            if (context.exec(pattern_eid).holds<ExecRange>()) {
                // lazy init
                if (!ranges) {
                    ranges.emplace(context);
                }
                const auto maybe_overlapping = ranges.value().search_overlapping(pattern_eid);
                if (maybe_overlapping.has_value()) {
                    dl.link(context.emplace_diagnostic(context.exec(pattern_eid).span,
                                                       diag_code::overlapping_range_patern,
                                                       diag_type::error));
                    dl.link(context.emplace_diagnostic(
                        context.exec(maybe_overlapping.as_id()).span,
                        diag_code::existing_pattern_with_overlapping_range_here, diag_type::note));
                } else {
                    ranges->put(pattern_eid);
                    std::cout << "YAHOO: " << exec_to_string(ctx, pattern_eid)
                              << "\n"; // TODO debug
                }
                continue;
            }
            const OptId<ExecId> already_eid = exec_map.at(pattern_eid);
            if (already_eid.has_value()) {
                dl.link(context.emplace_diagnostic(context.exec(pattern_eid).span,
                                                   diag_code::duplicate_match_pattern,
                                                   diag_type::error));
                dl.link(context.emplace_diagnostic(context.exec(already_eid.as_id()).span,
                                                   diag_code::existing_pattern_of_same_value_here,
                                                   diag_type::note));
                continue;
            }
            exec_map.insert(pattern_eid);
        }
    }

    if (!else_pattern) {
        valid = false;
        Span span{context, fid, match_expr};
        const auto d0 = context.emplace_diagnostic(
            span, diag_code::match_expression_may_not_be_exhaustive, diag_type::error);
        const auto d1
            = context.emplace_diagnostic(span, diag_code::add_an_else_branch, diag_type::help);
        context.link_diagnostic(d0, d1);
    }

    return valid;
}

// returns true on issue, else false
bool check_for_multi_decomps_on_match_branch(Context& ctx, FileId fid,
                                             ast_expr_match_branch_t match_branch);

} // namespace hir

#endif // !BEARC_COMPILER_HIR_MATCHING_HPP
